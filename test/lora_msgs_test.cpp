// The decoded-message ring -- src/lora_msgs.cpp -- on a desktop.
//
// One shared ring filtered by channel on the way out, which is a design with an
// off-by-one in every direction: newest-first indexing over a ring that wraps,
// a per-channel filter that has to skip rows without losing count, and fixed
// fields that a long line must not walk off the end of. None of those fail
// loudly on a device -- they show the wrong message under the wrong channel.
#include "lora_msgs.h"
#include "test_util.h"
#include <cstdlib>
#include <cstring>
#include <cstdio>

using namespace Lora;

static Packet pk;

// port 5 is MeshCore::TYPE_GRP_TXT, which is what a group message is; the value
// is the network's, not this ring's.
static void say(Proto p, uint8_t chan, const char* who, const char* what, int16_t rssi, uint32_t ms,
                bool direct = true, uint8_t port = 5) {
    memset(&pk, 0, sizeof pk);
    pk.rssi = rssi;
    pk.snr4 = 24;          // +6.00 dB
    pk.epoch = 1750000000u;
    Msgs::note(p, chan, port, who, 0, what, pk, ms, direct, direct ? 0 : 2);
}

int main() {
    void* mem = malloc(Msgs::bytesNeeded());
    printf("ring: %u bytes (%u kB) for %u messages\n", (unsigned)Msgs::bytesNeeded(),
           (unsigned)(Msgs::bytesNeeded() / 1024), (unsigned)Msgs::CAP);

    suite("Nothing is kept before a store is handed over");
    {
        ck("not ready", !Msgs::ready());
        say(Proto::MESHCORE, 0, "someone", "hello", -80, 1000);
        ck("the message is dropped, not crashed", Msgs::count() == 0);
        ck("a block one byte short is refused", !Msgs::begin(mem, Msgs::bytesNeeded() - 1));
        ck("an exact one is taken", Msgs::begin(mem, Msgs::bytesNeeded()));
    }

    suite("Newest first, with the signal the copy was heard at");
    {
        Msgs::clear();
        say(Proto::MESHCORE, 0, "DL1ABC", "first", -95, 1000);
        say(Proto::MESHCORE, 0, "DL2DEF", "second", -70, 2000);
        ck("two held", Msgs::count() == 2);
        Msgs::Msg m;
        ck("index 0 is the newest", Msgs::at(0, m) && strcmp(m.text, "second") == 0);
        ck("index 1 is the older", Msgs::at(1, m) && strcmp(m.text, "first") == 0);
        ck("past the end is false and zeroed", !Msgs::at(2, m) && m.text[0] == '\0');
        Msgs::at(0, m);
        ck("the signal travels with it", m.rssi == -70 && m.snr4 == 24);
        ck("and the wall clock when it was trusted", m.epoch == 1750000000u);
        ck("the sender is what it claimed", strcmp(m.sender, "DL2DEF") == 0);
        ck("heard first-hand", m.direct && m.hops == 0);
        ck("and it carries the network's own type byte", m.port == 5);
    }

    suite("A relayed copy says so, because its signal is the relay's");
    {
        Msgs::clear();
        say(Proto::MESHCORE, 0, "far away", "over the hill", -60, 1000, false);
        Msgs::Msg m;
        Msgs::at(0, m);
        // -60 dBm looks like a neighbour and is not one: it is how loud the last
        // repeater was. The same distinction the node table makes.
        ck("marked as not direct", !m.direct && m.hops == 2);
        ck("but the reading is kept", m.rssi == -60);
    }

    suite("One channel at a time, over a ring every channel shares");
    {
        Msgs::clear();
        say(Proto::MESHCORE, 0, "a", "public one", -80, 1000);
        say(Proto::MESHCORE, 3, "b", "ping one", -80, 2000);
        say(Proto::MESHCORE, 0, "c", "public two", -80, 3000);
        say(Proto::MESHTASTIC, 0, "d", "a different network's channel 0", -80, 4000);
        say(Proto::MESHCORE, 3, "e", "ping two", -80, 5000);
        ck("five held in total", Msgs::count() == 5);
        ck("two on MeshCore channel 0", Msgs::countFor(Proto::MESHCORE, 0) == 2);
        ck("two on MeshCore channel 3", Msgs::countFor(Proto::MESHCORE, 3) == 2);
        // The proto is part of the identity: Meshtastic channel 0 and MeshCore
        // channel 0 are different channels on different networks, and a filter
        // on the index alone would mix them.
        ck("one on Meshtastic channel 0, not three", Msgs::countFor(Proto::MESHTASTIC, 0) == 1);
        ck("none on a channel nothing arrived on", Msgs::countFor(Proto::MESHCORE, 9) == 0);
        Msgs::Msg m;
        ck("newest first within the channel",
           Msgs::atFor(Proto::MESHCORE, 3, 0, m) && strcmp(m.text, "ping two") == 0);
        ck("and the one before it",
           Msgs::atFor(Proto::MESHCORE, 3, 1, m) && strcmp(m.text, "ping one") == 0);
        ck("past that channel's end is false", !Msgs::atFor(Proto::MESHCORE, 3, 2, m));
        // A page in one pass over the ring, the shape a view wants.
        Msgs::Msg page[4];
        ck("a page of that channel is two rows", Msgs::pageFor(Proto::MESHCORE, 0, page, 4) == 2);
        ck("newest first", strcmp(page[0].text, "public two") == 0 && strcmp(page[1].text, "public one") == 0);
        ck("and it pages", Msgs::pageFor(Proto::MESHCORE, 0, page, 4, 1) == 1 &&
                           strcmp(page[0].text, "public one") == 0);
    }

    suite("An alert and a chat line on one channel stay tellable apart");
    {
        Msgs::clear();
        // Meshtastic PORT_TEXT is 1 and PORT_ALERT is 11. Both decode into the
        // same `text`, and without the port a view would show a critical alert
        // as an ordinary chat line -- so the port rides along rather than the
        // alert being dropped to keep the ring pure.
        say(Proto::MESHTASTIC, 2, "!3b1c9a2e", "on my way", -80, 1000, true, 1);
        say(Proto::MESHTASTIC, 2, "!3b1c9a2e", "SMOKE DETECTED", -80, 2000, true, 11);
        ck("both on the same channel", Msgs::countFor(Proto::MESHTASTIC, 2) == 2);
        Msgs::Msg m;
        ck("the alert is the newest and says which port it came on",
           Msgs::atFor(Proto::MESHTASTIC, 2, 0, m) && m.port == 11 && strcmp(m.text, "SMOKE DETECTED") == 0);
        ck("and the chat line keeps its own", Msgs::atFor(Proto::MESHTASTIC, 2, 1, m) && m.port == 1);
    }

    suite("A busy channel does push a quiet one out, and the count says so");
    {
        Msgs::clear();
        say(Proto::MESHCORE, 3, "quiet", "the one somebody wanted to read", -80, 1);
        for (uint16_t i = 0; i < Msgs::CAP; i++) {
            char t[32]; snprintf(t, sizeof t, "chatter %u", (unsigned)i);
            say(Proto::MESHCORE, 0, "busy", t, -80, (uint32_t)(1000 + i));
        }
        ck("the ring is full and not overfull", Msgs::count() == Msgs::CAP);
        ck("everything that arrived is counted", Msgs::total() == (uint32_t)Msgs::CAP + 1);
        ck("one was pushed out", Msgs::dropped() == 1);
        // The honest consequence, written down in include/lora_msgs.h: the quiet
        // channel's message is gone, and dropped() is how a view says so instead
        // of showing an empty channel with no explanation.
        ck("and it was the quiet channel's", Msgs::countFor(Proto::MESHCORE, 3) == 0);
        Msgs::Msg m;
        ck("the newest is the last of the chatter",
           Msgs::at(0, m) && strcmp(m.text, "chatter 255") == 0);
        ck("the oldest held is the second of it",
           Msgs::at((uint16_t)(Msgs::CAP - 1), m) && strcmp(m.text, "chatter 0") == 0);
    }

    suite("Long fields are cut, never run past");
    {
        Msgs::clear();
        char sender[120], text[400];
        memset(sender, 'S', sizeof sender - 1); sender[sizeof sender - 1] = '\0';
        memset(text, 'T', sizeof text - 1);     text[sizeof text - 1] = '\0';
        say(Proto::MESHCORE, 0, sender, text, -80, 1000);
        Msgs::Msg m;
        Msgs::at(0, m);
        ck("the sender fills its field and is terminated", strlen(m.sender) == sizeof m.sender - 1);
        ck("the text likewise", strlen(m.text) == sizeof m.text - 1);
        // 201 bytes, because MeshCore's GroupText carries 160 and Meshtastic's
        // text 200: nothing real arrives truncated by this ring.
        ck("the text field holds Meshtastic's full 200 characters", sizeof m.text >= 201);
    }

    suite("A frame that opened but said nothing is not a row");
    {
        Msgs::clear();
        // A GRP_DATA never reaches note() at all (lora_nodes.cpp only calls it
        // for a parsed GRP_TXT), and an empty line would fill the ring with
        // blanks and push words out of it.
        say(Proto::MESHCORE, 0, "someone", "", -80, 1000);
        ck("an empty line is not kept", Msgs::count() == 0);
        Msgs::note(Proto::MESHCORE, 0, 5, "someone", 0, nullptr, pk, 1000, true, 0);
        ck("nor is a null one", Msgs::count() == 0);
        ck("and nothing is counted as dropped", Msgs::dropped() == 0 && Msgs::total() == 0);
    }

    suite("Clearing forgets the counters too");
    {
        Msgs::clear();
        say(Proto::MESHCORE, 0, "a", "one", -80, 1000);
        Msgs::clear();
        // "256 held, 40 dropped" after a clear would be a report about messages
        // nobody can read any more.
        ck("held, total and dropped all reset", Msgs::count() == 0 && Msgs::total() == 0 && Msgs::dropped() == 0);
    }

    free(mem);
    return report();
}
