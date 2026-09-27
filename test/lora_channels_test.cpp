// The channel list across a reboot -- src/lora_channels.cpp, through
// src/settings.cpp and the emulator's Preferences shim.
//
// What this guards: thirteen MeshCore hashtag channels were typed into a
// console at a bench on 2026-09-26 and a power cycle threw every one of them
// away. The list now goes to the settings store, and "a restart" below means
// what it says -- SQUACHSIM_NVS puts a real file behind the store, the RAM
// tables are cleared the way a boot clears them, and the blob is read back
// from disk. Everything a power cycle cannot be asked about on a desk is
// asked about here instead.
//
// The two claims worth the most scrutiny, because they are the reason the
// format has two kinds of record at all:
//   - a HASHTAG channel is stored as its TAG, and its key is re-derived at
//     restore. So the blob must contain no key bytes for it, and the key that
//     comes back must equal SHA256("#tag")[0..15] computed fresh.
//   - a KEYED channel has nothing to derive from, so its sixteen (or
//     thirty-two) bytes must come back byte for byte.
#include "lora_channels.h"
#include "lora_meshcore.h"
#include "lora_meshtastic.h"
#include "lora_crypto.h"
#include "settings.h"
#include "theme.h"
#include "clock.h"
#include "test_util.h"
#include <cstdlib>
#include <cstring>
#include <strings.h>   // strcasecmp: the group names are matched the way a console matches them
#include <cstdio>

// The two things settings.cpp reaches for that are not under test.
namespace Theme { void applyPalette(uint8_t) {} }
namespace Clock {
uint8_t     zoneCount()        { return 1; }
const char* zoneName(uint8_t)  { return "UTC"; }
void        applyZone(uint8_t) {}
}

using Lora::Chan::BLOB_MAX;

// The list the owner entered by hand at the bench, in that order.
static const char* const TAGS[] = {
    "#wardriving", "#hamradio", "#nrw", "#bochum", "#dortmund", "#essen",
    "#gelsenkirchen", "#muenster", "#muensterland", "#darc-i21", "#rheine",
    "#test", "#ping",
};
static const int N_TAGS = (int)(sizeof TAGS / sizeof TAGS[0]);

// A key nobody can derive: sixteen bytes somebody would paste in.
static const char* PRIVATE_HEX = "00112233445566778899aabbccddeeff";
// Meshtastic: 32 bytes of base64, the shape a channel QR carries.
static const char* MT_KEY = "AQIDBAUGBwgJCgsMDQ4PEBESExQVFhcYGRobHB0eHyA=";

// What a boot does to the decoders: the tables are RAM and come up empty.
// Nothing else in the firmware clears them, which is exactly why this is the
// interesting moment.
static void powerCycle() {
    MeshCore::clearUserChannels();
    Meshtastic::clearUserChannels();
    Settings::load();          // re-reads the file, as setup() does
}

// Seal a plaintext the way MeshCore does, so a muted channel can be shown to
// stop decrypting rather than merely to look switched off. Same helper as
// test/lora_meshcore_test.cpp.
static uint8_t seal(const uint8_t key[16], const uint8_t* plain, uint8_t len, uint8_t* out) {
    uint8_t padded[192] = {0};
    memcpy(padded, plain, len);
    const uint8_t clen = (uint8_t)((len + 15) & ~15);
    LoraCrypto::Aes128 a; a.setKey(key);
    for (uint8_t off = 0; off < clen; off = (uint8_t)(off + 16)) a.encryptBlock(padded + off, out + 2 + off);
    uint8_t secret[32] = {0}; memcpy(secret, key, 16);
    uint8_t mac[32];
    LoraCrypto::hmacSha256(secret, 32, out + 2, clen, mac);
    out[0] = mac[0]; out[1] = mac[1];
    return (uint8_t)(2 + clen);
}

// A flood GRP_TXT on the channel whose key this is, one hop.
static void groupFrame(Lora::Packet& pk, const uint8_t key[16], const char* text) {
    uint8_t plain[160];
    LoraCrypto::wr32le(plain, 1750000000u);
    plain[4] = 0;
    const size_t tl = strlen(text);
    memcpy(plain + 5, text, tl);
    uint8_t sealed[192];
    const uint8_t sl = seal(key, plain, (uint8_t)(5 + tl), sealed);

    memset(&pk, 0, sizeof pk);
    pk.sync = 0x12; pk.sf = 8; pk.bwKhz10 = 625; pk.freqHz = 869618000;
    pk.flags = Lora::PK_CRC_PRESENT | Lora::PK_CRC_OK;
    uint8_t n = 0;
    pk.data[n++] = 0x15;                       // flood GRP_TXT
    pk.data[n++] = 0x01;                       // one hop, one-byte hashes
    pk.data[n++] = 0xA1;                       // the relay
    pk.data[n++] = MeshCore::channelHash(key); // the channel hash hint
    memcpy(pk.data + n, sealed, sl); n = (uint8_t)(n + sl);
    pk.len = n;
}

static int indexOfName(const char* name) { return MeshCore::findChannel(name); }

// memmem is not everywhere; a blob this small does not need it to be.
static bool contains(const uint8_t* hay, size_t n, const void* needle, size_t m) {
    if (m > n) return false;
    for (size_t i = 0; i + m <= n; i++) if (memcmp(hay + i, needle, m) == 0) return true;
    return false;
}

int main() {
    setenv("SQUACHSIM_NVS", "out", 1);
    remove("out/settings.nvs");

    suite("A fresh device");
    Settings::load();
    ck("nothing stored, nothing restored", Lora::Chan::restore() == 0);
    ck("only the built-in MeshCore channel", MeshCore::channelCount() == 1);
    ck("only the built-in Meshtastic presets", Meshtastic::userChannelCount() == 0);

    suite("Thirteen hashtags and two keys, entered by hand");
    {
        int added = 0;
        for (int i = 0; i < N_TAGS; i++) if (MeshCore::addChannel(TAGS[i], nullptr)) added++;
        ck("all thirteen took", added == N_TAGS);
        ck("a second #bochum is refused, not given a slot", !MeshCore::addChannel("#bochum", nullptr));
        ck("a MeshCore key can be pasted in", MeshCore::addChannel("Vereinsheim", PRIVATE_HEX));
        ck("a Meshtastic key too", Meshtastic::addChannel("Ruhr", MT_KEY));
        ck("fourteen of the user's, of twenty-four", MeshCore::userChannelCount() == 14);
        Lora::Chan::save();
    }

    suite("What the blob actually holds");
    {
        uint8_t blob[BLOB_MAX];
        const size_t n = Lora::Chan::encode(blob, sizeof blob);
        // 13 tag records of 3 + strlen(tag), one keyed MeshCore record of
        // 3 + 11 + 16, one Meshtastic record of 4 + 4 + 32, and the format byte.
        size_t want = 1;
        for (int i = 0; i < N_TAGS; i++) want += 3 + strlen(TAGS[i]);
        want += 3 + strlen("Vereinsheim") + 16;
        want += 4 + strlen("Ruhr") + 32;
        ck("every record is exactly as long as its kind needs", n == want);
        ck("it fits in one NVS entry with room to spare", n < BLOB_MAX);

        // A hashtag's key is nowhere in the blob: the tag is, and the key is
        // derived from it again at the next boot.
        uint8_t k[16]; MeshCore::hashtagKey("#bochum", k);
        ck("#bochum's key is not in the blob", !contains(blob, n, k, 16));
        ck("#bochum's tag is", contains(blob, n, "#bochum", 7));

        // The pasted key has no derivation, so it must be there in full.
        uint8_t priv[16]; LoraCrypto::fromHex(PRIVATE_HEX, priv, sizeof priv);
        ck("the pasted key is stored in full", contains(blob, n, priv, 16));
    }

    suite("And then the power goes");
    {
        powerCycle();
        ck("the tables really are empty first", MeshCore::userChannelCount() == 0);
        const uint8_t got = Lora::Chan::restore();
        ck("fifteen channels came back", got == 15);
        ck("fourteen MeshCore", MeshCore::userChannelCount() == 14);
        ck("one Meshtastic", Meshtastic::userChannelCount() == 1);

        bool order = true, keys = true;
        for (int i = 0; i < N_TAGS; i++) {
            const MeshCore::Channel c = MeshCore::channel((uint8_t)(1 + i));
            if (strcmp(c.name, TAGS[i]) != 0) order = false;
            uint8_t k[16]; MeshCore::hashtagKey(TAGS[i], k);
            if (memcmp(c.key, k, 16) != 0 || c.hash != MeshCore::channelHash(k)) keys = false;
            if (!c.derived) keys = false;
        }
        ck("in the order they were typed", order);
        ck("every hashtag key re-derived and its hash with it", keys);

        uint8_t priv[16]; LoraCrypto::fromHex(PRIVATE_HEX, priv, sizeof priv);
        const MeshCore::Channel v = MeshCore::channel((uint8_t)indexOfName("Vereinsheim"));
        ck("the pasted key came back byte for byte", memcmp(v.key, priv, 16) == 0 && !v.derived);

        const Meshtastic::Channel m = Meshtastic::channel((uint8_t)Meshtastic::findChannel("Ruhr"));
        uint8_t mt[32]; const size_t mtn = LoraCrypto::fromBase64(MT_KEY, mt, sizeof mt);
        ck("the Meshtastic key too, all 32 bytes",
           mtn == 32 && m.keyLen == 32 && memcmp(m.key, mt, 32) == 0);
    }

    suite("A restored key decrypts the first frame after the boot");
    {
        uint8_t k[16]; MeshCore::hashtagKey("#dortmund", k);
        Lora::Packet pk;
        groupFrame(pk, k, "Eddi: moin");
        MeshCore::Decoded d;
        ck("it opens", MeshCore::decode(pk, d) && d.haveGroup);
        ck("on #dortmund", d.haveGroup && strcmp(MeshCore::channel(d.channelIdx).name, "#dortmund") == 0);
        ck("and reads back", d.haveGroup && strcmp(d.grp.sender, "Eddi") == 0 && strcmp(d.grp.text, "moin") == 0);
    }

    suite("Muting one, and keeping it muted");
    {
        const int ix = indexOfName("#ping");
        MeshCore::setChannelEnabled((uint8_t)ix, false);
        Lora::Chan::save();

        uint8_t k[16]; MeshCore::hashtagKey("#ping", k);
        Lora::Packet pk;
        groupFrame(pk, k, "bot: ping");
        MeshCore::Decoded d;
        ck("a muted channel's frames stop opening", MeshCore::decode(pk, d) && !d.haveGroup);

        powerCycle();
        ck("restored", Lora::Chan::restore() == 15);
        const int back = indexOfName("#ping");
        ck("#ping is still in the list", back > 0);
        ck("and still muted", back > 0 && !MeshCore::channel((uint8_t)back).enabled);
        ck("its neighbours are not", MeshCore::channel((uint8_t)indexOfName("#test")).enabled);

        MeshCore::setChannelEnabled((uint8_t)back, true);
        Lora::Chan::save();
        powerCycle();
        Lora::Chan::restore();
        ck("unmuting sticks as well", MeshCore::channel((uint8_t)indexOfName("#ping")).enabled);
    }

    suite("Dropping one");
    {
        const int ix = indexOfName("#essen");
        ck("it refuses to drop the built-in Public", !MeshCore::removeChannel(0));
        ck("dropped", MeshCore::removeChannel((uint8_t)ix));
        Lora::Chan::save();
        powerCycle();
        ck("fourteen came back", Lora::Chan::restore() == 14);
        ck("#essen is gone for good", indexOfName("#essen") < 0);
        ck("the one after it shifted up, not lost",
           strcmp(MeshCore::channel((uint8_t)ix).name, "#gelsenkirchen") == 0);
        ck("the one before it is untouched",
           strcmp(MeshCore::channel((uint8_t)(ix - 1)).name, "#dortmund") == 0);
    }

    suite("CLEAR, the case that used to come back from the dead");
    {
        MeshCore::clearUserChannels();
        Meshtastic::clearUserChannels();
        Lora::Chan::save();
        // putBytes() ignores a zero-length value and leaves the old blob in
        // place, so an emptied list has to REMOVE the entry. This is the check
        // that the store does.
        uint8_t blob[BLOB_MAX];
        ck("the entry is gone, not merely overwritten", Settings::loraChannels(blob, sizeof blob) == 0);
        powerCycle();
        ck("nothing comes back", Lora::Chan::restore() == 0);
        ck("the list is the built-in one", MeshCore::channelCount() == 1);
    }

    suite("A full table");
    {
        char name[16];
        int added = 0;
        for (int i = 0; i < 30; i++) {
            snprintf(name, sizeof name, "#c%d", i);
            if (MeshCore::addChannel(name, nullptr)) added++;
        }
        ck("it stops at twenty-four", added == 24 && MeshCore::userChannelCount() == 24);
        Lora::Chan::save();
        powerCycle();
        ck("all twenty-four come back", Lora::Chan::restore() == 24);
        ck("the last one is the twenty-fourth added",
           strcmp(MeshCore::channel(24).name, "#c23") == 0);
    }

    suite("The NRW group: eleven tags, and every hash pinned");
    {
        // A mistyped tag is not an error and never will be: it is a different
        // channel that decrypts nothing, for ever, quietly. The only thing that
        // catches it is a hash computed somewhere else -- these are
        // docs/LORA.md section 3.13's table, recomputed independently.
        struct { const char* tag; uint8_t hash; } want[] = {
            { "#wardriving", 0x81 }, { "#hamradio", 0xB3 }, { "#nrw", 0xD0 },
            { "#bochum", 0x6C }, { "#dortmund", 0x34 }, { "#essen", 0x46 },
            { "#gelsenkirchen", 0x59 }, { "#muenster", 0x07 }, { "#muensterland", 0xF6 },
            { "#darc-i21", 0x37 }, { "#rheine", 0x6C },
        };
        const int n = (int)(sizeof want / sizeof want[0]);
        const int g = Lora::Chan::findGroup("nrw");     // named case-insensitively
        ck("the group is there", g >= 0);
        ck("with eleven channels", g >= 0 && Lora::Chan::groupSize((uint8_t)g) == n);
        bool tags = g >= 0, hashes = true;
        for (int i = 0; i < n && g >= 0; i++) {
            if (strcmp(Lora::Chan::groupTag((uint8_t)g, (uint8_t)i), want[i].tag) != 0) tags = false;
            uint8_t k[16]; MeshCore::hashtagKey(want[i].tag, k);
            if (MeshCore::channelHash(k) != want[i].hash) hashes = false;
        }
        ck("the tags are the ones that were given, in order", tags);
        ck("every hash matches the documented table", hashes);

        MeshCore::clearUserChannels();
        Meshtastic::clearUserChannels();
        Lora::Chan::save();
        ck("one command adds all eleven", Lora::Chan::addGroup((uint8_t)g) == n);
        ck("and the store took the list", Lora::Chan::save());
        ck("adding it twice adds nothing twice", Lora::Chan::addGroup((uint8_t)g) == 0);
        ck("an unknown group is refused", Lora::Chan::findGroup("bavaria") < 0);
        powerCycle();
        ck("and they are there after the power goes", Lora::Chan::restore() == n);
        ck("#gelsenkirchen among them, with its hash",
           indexOfName("#gelsenkirchen") > 0 &&
           MeshCore::channel((uint8_t)indexOfName("#gelsenkirchen")).hash == 0x59);
    }

    suite("The radar groups: every tag from /api/init, every hash pinned");
    {
        // The nine groups added from meshcore.df0x.de/api/init's radarChannels,
        // read on 2026-09-26: forty names, "Public" and thirty-nine hashtags.
        //
        // WHY THIS TEST IS LONG AND BORING. A mistyped tag is not an error and
        // never will be: it is a different channel that decrypts nothing, for
        // ever, quietly. Nothing on the board would ever say so. So every tag is
        // written out twice -- once in src/lora_channels.cpp, once here -- and
        // every hash is derived by the firmware's own SHA-256 and compared
        // against a byte computed independently. The pairs below were produced
        // from the radarChannels array itself, not retyped from a page.
        struct Want { const char* group; const char* tag; uint8_t hash; };
        const Want WANT[] = {
            { "BENCH",  "#test",         0xD9 },   // the bench value docs/LORA.md 3.2 already pinned
            { "BENCH",  "#ping",         0x28 },
            { "BENCH",  "#testing",      0x59 },
            { "DE",     "#berlin",       0xB5 },
            { "DE",     "#hansemesh",    0xFC },
            { "DE",     "#dl-mitte",     0x90 },
            { "DE",     "#bsmesh",       0x93 },
            { "DE",     "#kiel",         0x55 },
            { "DE",     "#sh",           0x0C },
            { "AT-CH",  "#switzerland",  0xE7 },
            { "AT-CH",  "#austria",      0xFB },
            { "AT-CH",  "#vienna",       0xDD },
            { "EU",     "#london",       0xD5 },
            { "EU",     "#amsterdam",    0x32 },
            { "EU",     "#copenhagen",   0x83 },
            { "WORLD",  "#vancouver",    0xC8 },
            { "WORLD",  "#thailand",     0xB4 },
            { "WORLD",  "#queens",       0x3E },
            { "WORLD",  "#northeast",    0xC5 },
            { "NET",    "#meshcore",     0xEF },
            { "NET",    "#meshtastic",   0xFE },
            { "NET",    "#meshcorenetz", 0x7D },
            { "NET",    "#mesh",         0xB0 },
            { "NET",    "#bot",          0xCA },
            { "NET",    "#bots",         0x44 },
            { "NET",    "#admin",        0x9E },
            { "NET",    "#public",       0x66 },
            { "NET",    "#info",         0x3B },
            { "NET",    "#news",         0x03 },
            { "FIELD",  "#wardriving",   0x81 },
            { "FIELD",  "#wardrive",     0xE0 },
            { "FIELD",  "#camping",      0x8E },
            { "FIELD",  "#weather",      0x03 },
            { "FIELD",  "#emergency",    0x68 },
            { "FIELD",  "#prepper",      0x49 },
            { "SOCIAL", "#chat",         0xB8 },
            { "SOCIAL", "#coffee",       0xD4 },
            { "SOCIAL", "#beer",         0xB0 },
            { "HAM",    "#ham",          0xE3 },
            { "HAM",    "#hamradio",     0xB3 },
        };
        const int NW = (int)(sizeof WANT / sizeof WANT[0]);

        bool hashes = true, placed = true;
        for (int i = 0; i < NW; i++) {
            uint8_t k[16];
            MeshCore::hashtagKey(WANT[i].tag, k);
            if (MeshCore::channelHash(k) != WANT[i].hash) {
                hashes = false;
                printf("      %s: hash %02x, expected %02x\n", WANT[i].tag,
                       (unsigned)MeshCore::channelHash(k), (unsigned)WANT[i].hash);
            }
            // And it is in the group it is supposed to be in, at some position.
            const int g = Lora::Chan::findGroup(WANT[i].group);
            bool found = false;
            for (uint8_t j = 0; g >= 0 && j < Lora::Chan::groupSize((uint8_t)g); j++)
                if (strcmp(Lora::Chan::groupTag((uint8_t)g, j), WANT[i].tag) == 0) found = true;
            if (!found) { placed = false; printf("      %s is not in %s\n", WANT[i].tag, WANT[i].group); }
        }
        ck("every tag derives the hash it is supposed to", hashes);
        ck("and sits in the group it is supposed to", placed);

        // The other direction: no group holds a tag that is not in this table,
        // which is what catches a tag added to the source and nowhere else.
        int total = 0;
        bool unexpected = false;
        for (uint8_t g = 0; g < Lora::Chan::groupCount(); g++) {
            if (strcasecmp(Lora::Chan::groupName(g), "NRW") == 0) continue;   // pinned above
            for (uint8_t j = 0; j < Lora::Chan::groupSize(g); j++) {
                total++;
                bool known = false;
                for (int i = 0; i < NW; i++) if (strcmp(Lora::Chan::groupTag(g, j), WANT[i].tag) == 0) known = true;
                if (!known) { unexpected = true; printf("      unpinned tag: %s\n", Lora::Chan::groupTag(g, j)); }
            }
        }
        ck("no group holds an unpinned tag", !unexpected);
        ck("and the count is the whole radar list plus #hamradio", total == NW);

        // #public is NOT the built-in Public channel, and that is the trap in
        // radarChannels: the names differ by a hash character and a capital, the
        // keys share two leading hex digits, and they are different channels.
        uint8_t pub[16];
        MeshCore::hashtagKey("#public", pub);
        ck("#public derives a key of its own", MeshCore::channelHash(pub) == 0x66);
        ck("and it is not the built-in Public channel's",
           memcmp(pub, MeshCore::channel(0).key, 16) != 0 &&
           MeshCore::channel(0).hash == 0x11);

        // The 24-entry table against 49 tags: two groups fit, and the third is
        // where it stops. This is a designed limit, not a surprise.
        ck("ten groups", Lora::Chan::groupCount() == 10);
        bool allFit = true;
        for (uint8_t g = 0; g < Lora::Chan::groupCount(); g++)
            if (Lora::Chan::groupSize(g) > MeshCore::maxUserChannels()) allFit = false;
        ck("no single group is bigger than the table", allFit);
        MeshCore::clearUserChannels();
        const int nrw = Lora::Chan::findGroup("NRW");
        const int net = Lora::Chan::findGroup("NET");
        ck("the two biggest groups fit together",
           nrw >= 0 && net >= 0 &&
           Lora::Chan::addGroup((uint8_t)nrw) == Lora::Chan::groupSize((uint8_t)nrw) &&
           Lora::Chan::addGroup((uint8_t)net) == Lora::Chan::groupSize((uint8_t)net) &&
           MeshCore::userChannelCount() == 21);
        const int world = Lora::Chan::findGroup("WORLD");
        ck("and a third fills the table rather than overrunning it",
           world >= 0 && Lora::Chan::addGroup((uint8_t)world) == 3 &&
           MeshCore::userChannelCount() == MeshCore::maxUserChannels());
        // The one that did not fit is still refused cleanly, not half-written.
        ck("a group added to a full table adds nothing",
           Lora::Chan::addGroup((uint8_t)Lora::Chan::findGroup("SOCIAL")) == 0 &&
           MeshCore::userChannelCount() == MeshCore::maxUserChannels());

        // Hash collisions inside a real list. Two of these pairs are in the
        // groups above, and one of them was already inside NRW: the hash is one
        // byte and the two-byte HMAC in front of the ciphertext is what actually
        // decides (docs/LORA.md section 3.13). A test that assumed hashes were
        // unique would be pinning a falsehood.
        uint8_t a[16], b2[16];
        MeshCore::hashtagKey("#bochum", a);
        MeshCore::hashtagKey("#rheine", b2);
        ck("#bochum and #rheine really do collide at 0x6c",
           MeshCore::channelHash(a) == 0x6C && MeshCore::channelHash(b2) == 0x6C &&
           memcmp(a, b2, 16) != 0);
        MeshCore::hashtagKey("#news", a);
        MeshCore::hashtagKey("#weather", b2);
        ck("so do #news and #weather at 0x03",
           MeshCore::channelHash(a) == 0x03 && MeshCore::channelHash(b2) == 0x03 &&
           memcmp(a, b2, 16) != 0);
        MeshCore::hashtagKey("#mesh", a);
        MeshCore::hashtagKey("#beer", b2);
        ck("and #mesh and #beer at 0xb0",
           MeshCore::channelHash(a) == 0xB0 && MeshCore::channelHash(b2) == 0xB0 &&
           memcmp(a, b2, 16) != 0);

        MeshCore::clearUserChannels();
        Lora::Chan::save();
    }

    suite("A Meshtastic channel with no key at all");
    {
        // Ham mode: plaintext, so the hash is the name's alone and there are
        // no key bytes to store. The record carries a zero length and has to
        // come back as a channel rather than as a refusal.
        MeshCore::clearUserChannels();
        Meshtastic::clearUserChannels();
        ck("added", Meshtastic::addChannel("DL0ABC", ""));
        ck("no key on it", Meshtastic::channel((uint8_t)Meshtastic::findChannel("DL0ABC")).keyLen == 0);
        Lora::Chan::save();
        powerCycle();
        ck("it comes back", Lora::Chan::restore() == 1);
        const int ix = Meshtastic::findChannel("DL0ABC");
        ck("still plaintext, and its hash still the name's", ix >= 0 &&
           Meshtastic::channel((uint8_t)ix).keyLen == 0 &&
           Meshtastic::channel((uint8_t)ix).hash == Meshtastic::channelHash("DL0ABC", nullptr, 0));
    }

    suite("A blob this build does not understand");
    {
        uint8_t junk[8] = { 99, 1, 0, 3, 'a', 'b', 'c', 0 };
        Settings::setLoraChannels(junk, sizeof junk);
        powerCycle();
        MeshCore::addChannel("#live", nullptr);
        ck("it is refused, not guessed at", Lora::Chan::restore() == 0);
        ck("and the live list is left alone", indexOfName("#live") > 0);
    }

    return report();
}
