// The node table -- src/lora_nodes.cpp -- fed constructed frames from three
// networks, and read back the way the NODES view reads it: one row per
// transmitter, the newest first, with the flags a sysop would want.
#include "lora_nodes.h"
#include "lora_meshtastic.h"
#include "lora_meshcore.h"
#include "lora_classify.h"
#include "lora_crypto.h"
#include "test_util.h"
#include <cstring>
#include <cstdio>

using namespace Lora;

static Packet pk;

// A MeshCore flood advert: 32 bytes of key, a timestamp, a signature nobody
// checks, then a name. `hops` path bytes in front of it say how many repeaters
// this copy crossed -- on a flood the path is the hops already taken, so zero
// means we heard the advertiser's own transmitter.
static void mcAdvert(uint8_t key0, uint8_t hops, const uint8_t* path, int16_t rssi) {
    memset(&pk, 0, sizeof pk);
    pk.sync = 0x12; pk.sf = 8; pk.bwKhz10 = 625; pk.freqHz = 869618000; pk.cr = 8;
    pk.flags = PK_CRC_PRESENT | PK_CRC_OK;
    pk.rssi = rssi; pk.snr4 = 48;
    uint8_t pay[160]; uint8_t n = 0;
    for (int i = 0; i < 32; i++) pay[n++] = (uint8_t)(key0 + i);
    LoraCrypto::wr32le(pay + n, 1750000000u); n = (uint8_t)(n + 4);
    for (int i = 0; i < 64; i++) pay[n++] = 0xEE;
    pay[n++] = (uint8_t)(0x80 | MeshCore::NODE_REPEATER);       // a name follows
    memcpy(pay + n, "Hoher Ast", 9); n = (uint8_t)(n + 9);
    uint8_t d = 0;
    pk.data[d++] = 0x11;                                        // flood ADVERT
    pk.data[d++] = (uint8_t)hops;                               // one-byte hashes
    if (hops) { memcpy(pk.data + d, path, hops); d = (uint8_t)(d + hops); }
    memcpy(pk.data + d, pay, n); d = (uint8_t)(d + n);
    pk.len = d;
    pk.toaUs = timeOnAirUs(8, 625, 8, 32, pk.len, true, false, false);
    classify(pk);
}

static void mtFrame(uint32_t from, uint8_t flags, const uint8_t* data, uint8_t dlen, uint32_t id) {
    memset(&pk, 0, sizeof pk);
    pk.sync = 0x2B; pk.sf = 11; pk.bwKhz10 = 2500; pk.freqHz = 869525000; pk.cr = 5;
    pk.flags = PK_CRC_PRESENT | PK_CRC_OK;
    LoraCrypto::wr32le(pk.data, Meshtastic::BROADCAST); LoraCrypto::wr32le(pk.data + 4, from); LoraCrypto::wr32le(pk.data + 8, id);
    pk.data[12] = flags; pk.data[13] = 0x08; pk.data[14] = 0; pk.data[15] = (uint8_t)from;
    memcpy(pk.data + 16, data, dlen);
    pk.len = (uint8_t)(16 + dlen);
    Meshtastic::Header h; Meshtastic::parseHeader(pk.data, pk.len, h);
    Meshtastic::Channel c = Meshtastic::channel(0);
    Meshtastic::decrypt(h, c, pk.data + 16, dlen);
    pk.toaUs = timeOnAirUs(11, 2500, 5, 16, pk.len, true, false, false);
    classify(pk);
}

int main() {
    Nodes::clear();

    suite("A Meshtastic node, named by its NodeInfo");
    {
        const uint8_t text[] = { 0x08, 0x01, 0x12, 0x02, 'h', 'i' };
        mtFrame(0x3b1c9a2e, 0x63, text, sizeof text, 1);
        ck("classified as Meshtastic", pk.proto == Proto::MESHTASTIC);
        Nodes::note(pk, 1000, 0);
        ck("one node", Nodes::count() == 1);
        const Nodes::Node* n = Nodes::at(0);
        ck("tag is the node id", n && strcmp(n->tag, "!3b1c9a2e") == 0);
        ck("no flags on a default-hop text", n && n->flags == 0 && n->hops == 0);
        const uint8_t user[] = { 0x0A, 9, '!', '3','b','1','c','9','a','2','e', 0x12, 3, 'B','o','b', 0x1A, 3, 'B','O','B', 0x38, 4 };
        uint8_t data[40] = { 0x08, 0x04, 0x12, (uint8_t)sizeof user };
        memcpy(data + 4, user, sizeof user);
        mtFrame(0x3b1c9a2e, 0xE7, data, (uint8_t)(4 + sizeof user), 2);   // hop_start 7, hop_limit 7: too many
        Nodes::note(pk, 2000, 0);
        n = Nodes::at(0);
        ck("still one node", Nodes::count() == 1 && n->packets == 2);
        ck("named Bob, tagged BOB", strcmp(n->name, "Bob") == 0 && strcmp(n->tag, "BOB") == 0);
        ck("REPEATER is a deprecated role", strcmp(Nodes::roleText(*n), "REPEATER") == 0 && (n->flags & Nodes::NF_DEPRECATED));
        ck("a hop limit of 7 is flagged", n->flags & Nodes::NF_HOPS_HIGH);
        char f[48]; Nodes::flagsText(*n, f, sizeof f);
        ck("flags text", strcmp(f, "hops oldrole") == 0);
        ck("airtime counted", n->airtimeMs > 0);
    }

    suite("A LoRaWAN device counts its frames");
    {
        auto wan = [](uint16_t fcnt) {
            memset(&pk, 0, sizeof pk);
            pk.sync = 0x34; pk.sf = 7; pk.bwKhz10 = 1250; pk.freqHz = 868100000; pk.cr = 5;
            pk.flags = PK_CRC_PRESENT | PK_CRC_OK;
            const uint8_t d[] = { 0x40, 0x2C, 0x1B, 0x01, 0x26, 0x80, (uint8_t)fcnt, (uint8_t)(fcnt >> 8), 0x01, 0xAA, 0xBB, 0x11, 0x22, 0x33, 0x44 };
            memcpy(pk.data, d, sizeof d); pk.len = sizeof d;
            pk.toaUs = 60000;
            classify(pk);
        };
        wan(10); ck("classified as LoRaWAN", pk.proto == Proto::LORAWAN); Nodes::note(pk, 3000, 0);
        wan(11); Nodes::note(pk, 4000, 0);
        wan(14); Nodes::note(pk, 5000, 0);
        ck("two nodes now", Nodes::count() == 2);
        const Nodes::Node* n = Nodes::at(1);
        ck("tagged by DevAddr, named by operator", strcmp(n->tag, "26011b2c") == 0 && strcmp(n->name, "The Things Network") == 0);
        ck("two frames lost between 11 and 14", n->lost == 2 && n->counter == 14);
        wan(3); Nodes::note(pk, 6000, 0);
        ck("a counter going backwards is a reset", Nodes::at(1)->flags & Nodes::NF_RESETS);
    }

    suite("Order and duty");
    {
        uint8_t idx[8];
        ck("two in order", Nodes::order(idx, 8) == 2);
        ck("the LoRaWAN device, heard last, is first", Nodes::at(idx[0])->proto == Proto::LORAWAN);
        const Nodes::Node* n = Nodes::at(0);
        // 2 frames of about 0.3 s each in the first 2 s: well over 10 % of that window
        ck("duty per mille is airtime over the window", Nodes::dutyPermille(*n, 3000) > 100);
        Nodes::clear();
        ck("cleared", Nodes::count() == 0);
    }

    suite("A relayed advert is not a link measurement");
    {
        Nodes::clear();
        mcAdvert(0x4e, 0, nullptr, -63);
        ck("classified as MeshCore", pk.proto == Proto::MESHCORE);
        Nodes::note(pk, 1000, 0);
        const Nodes::Node* n = Nodes::at(0);
        ck("one node, named by its advert", Nodes::count() == 1 && strcmp(n->name, "Hoher Ast") == 0);
        ck("zero hops: a measured link at -63 dBm", n->rssi == -63 && n->snr4 == 48);
        ck("counted as direct", n->directPackets == 1 && n->viaPackets == 0);
        const uint32_t airDirect = n->airtimeMs;
        ck("its airtime is its own", airDirect > 0);

        // The same advertiser, eleven hops away. The signal is the last
        // repeater's; the bench saw paths this long routinely.
        const uint8_t path[11] = { 0xb4, 0x6f, 0x1f, 0xd8, 0xed, 0x52, 0x40, 0x10, 0x77, 0x49, 0x23 };
        mcAdvert(0x4e, 11, path, -99);
        Nodes::note(pk, 2000, 0);
        n = Nodes::at(0);
        ck("still one node, two frames", Nodes::count() == 1 && n->packets == 2);
        ck("the -63 link stands: it was not overwritten", n->rssi == -63);
        ck("the relayed copy is kept as via, -99", n->viaRssi == -99 && n->viaPackets == 1);
        ck("eleven hops recorded", n->hops == 11);
        ck("a relay's transmission is not the advertiser's airtime", n->airtimeMs == airDirect);
    }

    suite("A node heard only through repeaters has no RSSI of its own");
    {
        Nodes::clear();
        const uint8_t path[3] = { 0xb4, 0x6f, 0x1f };
        mcAdvert(0x7a, 3, path, -88);
        Nodes::note(pk, 1000, 0);
        const Nodes::Node* n = Nodes::at(0);
        ck("the row exists: the node does", Nodes::count() == 1);
        ck("but rssi is the never-heard sentinel", n->rssi == -200 && n->directPackets == 0);
        ck("and the number we did measure is on via", n->viaRssi == -88 && n->viaPackets == 1);
        ck("no airtime attributed at all", n->airtimeMs == 0);
    }

    suite("The repeater that transmitted a relayed frame IS heard directly");
    {
        Nodes::clear();
        // A flood group text with a one-byte path: the newest byte is the
        // repeater whose transmission we received. Nothing decrypts here, and
        // nothing needs to.
        memset(&pk, 0, sizeof pk);
        pk.sync = 0x12; pk.sf = 8; pk.bwKhz10 = 625; pk.freqHz = 869618000; pk.cr = 8;
        pk.flags = PK_CRC_PRESENT | PK_CRC_OK;
        pk.rssi = -71; pk.snr4 = 44;
        pk.data[0] = 0x15;              // flood GRP_TXT
        pk.data[1] = 0x02;              // two one-byte hashes
        pk.data[2] = 0xb4; pk.data[3] = 0x6f;
        for (uint8_t i = 0; i < 40; i++) pk.data[4 + i] = (uint8_t)(i * 7);
        pk.len = 44;
        pk.toaUs = timeOnAirUs(8, 625, 8, 32, pk.len, true, false, false);
        classify(pk);
        Nodes::note(pk, 1000, 0);
        const Nodes::Node* n = Nodes::at(0);
        ck("a row for the last relay", Nodes::count() == 1 && strcmp(n->tag, "rpt 6f") == 0);
        ck("we received its transmitter, so -71 is a link to it", n->rssi == -71 && n->directPackets == 1);
        ck("and the airtime was its own", n->airtimeMs > 0);
    }

    suite("A relayed Meshtastic frame does not credit the originator");
    {
        Nodes::clear();
        const uint8_t text[] = { 0x08, 0x01, 0x12, 0x02, 'h', 'i' };
        // flags 0x63: hop_start 3, hop_limit 3 -- nothing has relayed it.
        mtFrame(0x44f00001, 0x63, text, sizeof text, 7);
        pk.rssi = -70;
        Nodes::note(pk, 1000, 0);
        ck("direct at -70", Nodes::at(0)->rssi == -70 && Nodes::at(0)->directPackets == 1);
        // flags 0x61: hop_start 3, hop_limit 1 -- two hops consumed.
        mtFrame(0x44f00001, 0x61, text, sizeof text, 8);
        pk.rssi = -95;
        Nodes::note(pk, 2000, 0);
        const Nodes::Node* n = Nodes::at(0);
        ck("two hops away: via, and -70 stands", n->rssi == -70 && n->viaRssi == -95);
        ck("hops counted", n->hops == 2 && n->viaPackets == 1);
        // flags 0x03: hop_start 0 -- firmware before 2.3, no way to tell.
        mtFrame(0x44f00002, 0x03, text, sizeof text, 9);
        pk.rssi = -80;
        Nodes::note(pk, 3000, 0);
        const Nodes::Node* old = nullptr;
        for (uint8_t i = 0; i < Nodes::count(); i++)
            if (Nodes::at(i)->id == 0x44f00002) old = Nodes::at(i);
        ck("no hop_start: the old firmware is flagged", old && (old->flags & Nodes::NF_OLD_FW));
        ck("and its signal is not claimed as a link", old && old->rssi == -200 && old->viaRssi == -80);
    }

    suite("The duty ceiling follows the band the frame was heard in");
    {
        // Five one-second frames over 400 s is 12.5 per-mille: over the 1 %
        // that 868.0-868.6 MHz allows, well under the 10 % of the mesh band.
        auto device = [](uint32_t devAddr, uint32_t freqHz, uint16_t fcnt) {
            memset(&pk, 0, sizeof pk);
            pk.sync = 0x34; pk.sf = 12; pk.bwKhz10 = 1250; pk.freqHz = freqHz; pk.cr = 5;
            pk.flags = PK_CRC_PRESENT | PK_CRC_OK;
            const uint8_t d[] = { 0x40, (uint8_t)devAddr, (uint8_t)(devAddr >> 8), (uint8_t)(devAddr >> 16),
                                  (uint8_t)(devAddr >> 24), 0x80, (uint8_t)fcnt, (uint8_t)(fcnt >> 8),
                                  0x01, 0xAA, 0xBB, 0x11, 0x22, 0x33, 0x44 };
            memcpy(pk.data, d, sizeof d); pk.len = sizeof d;
            pk.toaUs = 1000000;      // one second on the air
            classify(pk);
        };
        auto pound = [&](uint32_t devAddr, uint32_t freqHz) {
            for (uint16_t k = 0; k < 5; k++) { device(devAddr, freqHz, (uint16_t)(k + 1)); Nodes::note(pk, 1000 + 100000u * k, 0); }
        };
        auto rowOf = [](uint32_t devAddr) -> const Nodes::Node* {
            for (uint8_t i = 0; i < Nodes::count(); i++)
                if (Nodes::at(i)->proto == Proto::LORAWAN && Nodes::at(i)->id == devAddr) return Nodes::at(i);
            return nullptr;
        };

        Nodes::clear();
        pound(0x26011b2c, 868100000u);          // 1 %
        const Nodes::Node* a = rowOf(0x26011b2c);
        ck("five frames, 5 s of air", a && a->packets == 5 && a->hourAirMs == 5000);
        ck("the window is 400 s, so 12 per-mille", a && Nodes::dutyPermille(*a, 401000) == 12);
        ck("868.1 allows 1 %: flagged", a && a->dutyLimit == 10 && (a->flags & Nodes::NF_DUTY));

        Nodes::clear();
        pound(0x26011b2d, 869525000u);          // 10 %, the mesh band
        const Nodes::Node* b = rowOf(0x26011b2d);
        ck("the same traffic on 869.525 is well inside 10 %",
           b && b->dutyLimit == 100 && !(b->flags & Nodes::NF_DUTY));

        Nodes::clear();
        pound(0x26011b2e, 868650000u);          // an untabulated sliver
        const Nodes::Node* c = rowOf(0x26011b2e);
        ck("no figure for the band, so no flag either",
           c && c->dutyLimit == 0 && !(c->flags & Nodes::NF_DUTY));
    }

    suite("A fresh hour bucket has no duty figure to show");
    {
        Nodes::clear();
        memset(&pk, 0, sizeof pk);
        pk.sync = 0x34; pk.sf = 12; pk.bwKhz10 = 1250; pk.freqHz = 868100000u; pk.cr = 5;
        pk.flags = PK_CRC_PRESENT | PK_CRC_OK;
        const uint8_t d[] = { 0x40, 0x99, 0x00, 0x00, 0x00, 0x80, 0x01, 0x00, 0x01, 0xAA, 0xBB, 0x11, 0x22, 0x33, 0x44 };
        memcpy(pk.data, d, sizeof d); pk.len = sizeof d;
        pk.toaUs = 700000;              // 0.7 s, the frame the sawtooth was measured with
        classify(pk);
        Nodes::note(pk, 1000, 0);
        const Nodes::Node* n = Nodes::at(0);
        // 700 ms of a 1.5 s window: the raw ratio really is 466 per-mille.
        ck("1.5 s into the bucket the raw ratio is 46.6 %", Nodes::dutyPermille(*n, 2500) == 466);
        ck("and it is not a figure yet", !Nodes::dutyKnown(*n, 2500));
        ck("nor at 4 minutes", !Nodes::dutyKnown(*n, 241000));
        ck("at 5 minutes it becomes one", Nodes::dutyKnown(*n, 301001));
        ck("and 0.7 s of 300 s is 2 per-mille", Nodes::dutyPermille(*n, 301000) == 2);
        ck("so nothing is flagged", !(n->flags & Nodes::NF_DUTY));
    }

    return report();
}
