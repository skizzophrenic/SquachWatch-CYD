// SquachWatch-CYD — the channel list, kept across a reboot. See
// include/lora_channels.h for what is stored and why it differs by kind.
#include "lora_channels.h"
#include "lora_meshcore.h"
#include "lora_meshtastic.h"
#include "settings.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>   // strcasecmp: a group named at a console

#if defined(ARDUINO_ARCH_ESP32)
#include <esp_heap_caps.h>
#endif

namespace Lora {
namespace Chan {

namespace {

// Bumped only if a record's shape changes. replay() refuses anything else
// rather than reading the next byte as a kind and inventing channels from
// whatever follows.
const uint8_t FORMAT = 1;

enum Kind : uint8_t {
    KIND_MC_TAG = 1,   // MeshCore hashtag: the tag is stored, the key derived at boot
    KIND_MC_KEY = 2,   // MeshCore with a key handed over: 16 bytes follow the name
    KIND_MT     = 3,   // Meshtastic: a key-length byte then 0, 16 or 32 bytes
};

const uint8_t F_MUTED = 0x01;   // flags bit 0: in the list, out of the decoder

// 1.4 kB is more than the loop task's stack wants to lend at the depth
// setup() reaches, and it is wanted for microseconds -- so it is borrowed and
// given back rather than held in .bss for the life of the board. PSRAM first:
// this panel has 7.6 MB of it against about 57 kB of contiguous internal RAM
// (measured 2026-09-26), and nothing that can live out there should not.
// heap_caps_malloc memory is released with plain free().
uint8_t* scratch() {
#if defined(ARDUINO_ARCH_ESP32)
    uint8_t* p = (uint8_t*)heap_caps_malloc(BLOB_MAX, MALLOC_CAP_SPIRAM);
    if (p) return p;
#endif
    return (uint8_t*)malloc(BLOB_MAX);
}

uint8_t nameLenOf(const char* name, size_t cap) {
    size_t n = 0;
    while (n < cap && name[n]) n++;
    return (uint8_t)n;
}

// The decoders take a key as TEXT, because that is how a person hands one
// over. Stored bytes go back in through that same checked door rather than
// through a second entry point that could drift away from it.
void toHex(const uint8_t* key, uint8_t len, char* out) {
    static const char* D = "0123456789abcdef";
    for (uint8_t i = 0; i < len; i++) { out[2 * i] = D[key[i] >> 4]; out[2 * i + 1] = D[key[i] & 15]; }
    out[2 * len] = '\0';
}

}  // namespace

size_t encode(uint8_t* out, size_t cap) {
    if (!out || cap < 1) return 0;
    size_t o = 0;
    out[o++] = FORMAT;

    // The built-in channels are the first entries of each list and are not
    // stored: they come back from the constants in the decoders, and writing
    // them down would mean a firmware that changed one still read the old.
    const uint8_t mcFirst = (uint8_t)(MeshCore::channelCount() - MeshCore::userChannelCount());
    for (uint8_t i = mcFirst; i < MeshCore::channelCount(); i++) {
        const MeshCore::Channel c = MeshCore::channel(i);
        const uint8_t nl = nameLenOf(c.name, sizeof c.name);
        if (!nl) continue;
        const size_t need = 3u + nl + (c.derived ? 0u : 16u);
        if (o + need > cap) break;
        out[o++] = c.derived ? KIND_MC_TAG : KIND_MC_KEY;
        out[o++] = (uint8_t)(c.enabled ? 0 : F_MUTED);
        out[o++] = nl;
        memcpy(out + o, c.name, nl); o += nl;
        if (!c.derived) { memcpy(out + o, c.key, 16); o += 16; }
    }

    const uint8_t mtFirst = (uint8_t)(Meshtastic::channelCount() - Meshtastic::userChannelCount());
    for (uint8_t i = mtFirst; i < Meshtastic::channelCount(); i++) {
        const Meshtastic::Channel c = Meshtastic::channel(i);
        const uint8_t nl = nameLenOf(c.name, sizeof c.name);
        if (!nl) continue;
        const uint8_t kl = c.keyLen > 32 ? 32 : c.keyLen;
        const size_t need = 4u + nl + kl;
        if (o + need > cap) break;
        out[o++] = KIND_MT;
        out[o++] = (uint8_t)(c.enabled ? 0 : F_MUTED);
        out[o++] = nl;
        memcpy(out + o, c.name, nl); o += nl;
        out[o++] = kl;
        if (kl) { memcpy(out + o, c.key, kl); o += kl; }
    }
    return o;
}

uint8_t replay(const uint8_t* rec, size_t n) {
    if (!rec || n < 1 || rec[0] != FORMAT) return 0;
    MeshCore::clearUserChannels();
    Meshtastic::clearUserChannels();
    uint8_t got = 0;
    size_t o = 1;
    while (o + 3 <= n) {
        const uint8_t kind = rec[o], flags = rec[o + 1], nl = rec[o + 2];
        o += 3;
        if (!nl || o + nl > n) break;
        char name[24] = {0};
        memcpy(name, rec + o, nl < sizeof name - 1 ? nl : sizeof name - 1);
        o += nl;
        bool ok = false;
        bool mc = true;
        if (kind == KIND_MC_TAG) {
            // No key in the record: the tag is the key. This is the line that
            // makes a stored tag survive a change in the derivation -- it is
            // re-derived here, at this boot, by today's code.
            ok = MeshCore::addChannel(name, nullptr);
        } else if (kind == KIND_MC_KEY) {
            if (o + 16 > n) break;
            char hex[33];
            toHex(rec + o, 16, hex);
            o += 16;
            ok = MeshCore::addChannel(name, hex);
        } else if (kind == KIND_MT) {
            if (o >= n) break;
            const uint8_t kl = rec[o++];
            if (kl > 32 || o + kl > n) break;
            char hex[65];
            toHex(rec + o, kl, hex);
            o += kl;
            ok = Meshtastic::addChannel(name, kl ? hex : "0");   // "0" is the firmware's own "no key"
            mc = false;
        } else {
            break;   // a kind this build does not know: stop, do not resync on noise
        }
        if (!ok) continue;                 // table full, or a duplicate: skip it, keep the rest
        got++;
        if (flags & F_MUTED) {
            const uint8_t last = (uint8_t)((mc ? MeshCore::channelCount() : Meshtastic::channelCount()) - 1);
            if (mc) MeshCore::setChannelEnabled(last, false);
            else    Meshtastic::setChannelEnabled(last, false);
        }
    }
    return got;
}

bool save() {
    uint8_t* buf = scratch();
    if (!buf) return false;   // this boot keeps the list; the next one would not
    const size_t n = encode(buf, BLOB_MAX);
    // One byte is the format marker with no records behind it, which is the
    // empty list -- and the empty list has to REMOVE the entry, not write
    // nothing over it. See Settings::setLoraChannels.
    const bool ok = Settings::setLoraChannels(buf, n > 1 ? n : 0);
    free(buf);
    return ok;
}

// ---- named groups ----------------------------------------------------------
// The open MeshCore channels one Ruhr and Westphalia operator listens on, as
// given on 2026-09-26: two topical, nine regional. Every one is a hashtag, so
// this table is a list of names and not a list of keys -- see the header.
//
// #test and #ping were that bench session's own and are deliberately not here:
// a group is a thing to carry into the field. They are in BENCH below.
static const char* const GROUP_NRW[] = {
    "#wardriving", "#hamradio", "#nrw", "#bochum", "#dortmund", "#essen",
    "#gelsenkirchen", "#muenster", "#muensterland", "#darc-i21", "#rheine",
};

// The rest come from ONE measured source: meshcore.df0x.de/api/init, read on
// 2026-09-26, whose `radarChannels` array is forty names -- "Public" and
// thirty-nine hashtags. That is the list the club's own visualiser watches, so
// it is the list with traffic on it, and every entry below is copied from that
// response rather than typed from a page. test/lora_channels_test.cpp holds the
// same forty names independently and asserts that each one lands in a group and
// that its derived hash is the expected byte -- because a mistyped tag is not an
// error, it is a different channel that quietly hears nothing for ever.
//
// WHY THEY ARE SPLIT UP AT ALL. MeshCore::maxUserChannels() is 24 and these are
// 39 tags, 49 with NRW's. They were never going to fit in one group, so the cut
// is made where it is useful rather than where it is arithmetically neat: a
// board goes out with a place and a purpose, and "Braunschweig plus Kiel plus
// Berlin" is a posture. Two groups fit comfortably (NRW's eleven and NET's ten
// is 21 of 24); three usually do not, and LORA CHAN GROUP says so with the
// number of free slots when it runs out.
//
// The hashes are NOT in this table. A group is a list of tags and the tag is the
// key (SHA256("#tag")[:16], hash SHA256(key)[0]); writing a hash down here would
// be writing down a derivation this file deliberately does not perform. The test
// pins them.

// "Public" from radarChannels is NOT here, and this is the trap in that list:
// the built-in Public channel has the fixed key 8b3387e9c5cdea6ac9e5edbaa115cd72
// (docs/LORA.md section 3.2), while the hashtag "#public" in the same array
// derives 8b4b705b080c0d943b1c80f6b3ef6b6d -- a completely different channel that
// happens to share two leading hex digits. Public is built in and always on;
// #public is an ordinary hashtag and lives in NET.

// The owner's own bench channels, back where they can be typed in one word.
static const char* const GROUP_BENCH[] = { "#test", "#ping", "#testing" };

// German regional groups from the radar list. #sh is Schleswig-Holstein,
// #bsmesh Braunschweig, #hansemesh the Hanseatic north, #dl-mitte the middle.
static const char* const GROUP_DE[] = {
    "#berlin", "#hansemesh", "#dl-mitte", "#bsmesh", "#kiel", "#sh",
};

// The neighbours that speak German. #vienna is a city and #austria a country and
// both are in the list, so both are here.
static const char* const GROUP_AT_CH[] = { "#switzerland", "#austria", "#vienna" };

// European cities outside DACH.
static const char* const GROUP_EU[] = { "#london", "#amsterdam", "#copenhagen" };

// The rest of the world in the radar list. #queens and #northeast are both New
// York and New England; they are grouped by distance from the bench, not by
// continent, because that is what decides whether a key is worth a slot.
static const char* const GROUP_WORLD[] = { "#vancouver", "#thailand", "#queens", "#northeast" };

// The plumbing: channels about the mesh rather than about a place. #public is
// here, and is not the built-in Public -- see above.
static const char* const GROUP_NET[] = {
    "#meshcore", "#meshtastic", "#meshcorenetz", "#mesh", "#bot", "#bots",
    "#admin", "#public", "#info", "#news",
};

// Out of the house with a radio. #wardriving is also in NRW and #wardrive is a
// second, separate channel with a different key -- both are in the list, both
// carry traffic, and neither is a typo for the other.
static const char* const GROUP_FIELD[] = {
    "#wardriving", "#wardrive", "#camping", "#weather", "#emergency", "#prepper",
};

static const char* const GROUP_SOCIAL[] = { "#chat", "#coffee", "#beer" };

// #ham from the radar list, with #hamradio which NRW already holds: whichever
// group a ham types first, they get both spellings.
static const char* const GROUP_HAM[] = { "#ham", "#hamradio" };

namespace {
struct Group { const char* name; const char* const* tags; uint8_t n; };
#define GROUP_ROW(nm, arr) { nm, arr, (uint8_t)(sizeof arr / sizeof arr[0]) }
const Group GROUPS[] = {
    GROUP_ROW("NRW",    GROUP_NRW),
    GROUP_ROW("BENCH",  GROUP_BENCH),
    GROUP_ROW("DE",     GROUP_DE),
    GROUP_ROW("AT-CH",  GROUP_AT_CH),
    GROUP_ROW("EU",     GROUP_EU),
    GROUP_ROW("WORLD",  GROUP_WORLD),
    GROUP_ROW("NET",    GROUP_NET),
    GROUP_ROW("FIELD",  GROUP_FIELD),
    GROUP_ROW("SOCIAL", GROUP_SOCIAL),
    GROUP_ROW("HAM",    GROUP_HAM),
};
#undef GROUP_ROW
const uint8_t N_GROUPS = (uint8_t)(sizeof GROUPS / sizeof GROUPS[0]);
}  // namespace

uint8_t     groupCount() { return N_GROUPS; }
const char* groupName(uint8_t g) { return g < N_GROUPS ? GROUPS[g].name : ""; }
uint8_t     groupSize(uint8_t g) { return g < N_GROUPS ? GROUPS[g].n : 0; }
const char* groupTag(uint8_t g, uint8_t i) {
    if (g >= N_GROUPS || i >= GROUPS[g].n) return "";
    return GROUPS[g].tags[i];
}

int findGroup(const char* name) {
    if (!name || !*name) return -1;
    for (uint8_t g = 0; g < N_GROUPS; g++) if (strcasecmp(name, GROUPS[g].name) == 0) return (int)g;
    return -1;
}

uint8_t addGroup(uint8_t g) {
    if (g >= N_GROUPS) return 0;
    uint8_t added = 0;
    // No key argument: every tag in a group derives its own, which is the only
    // reason a group can be a table of strings at all.
    for (uint8_t i = 0; i < GROUPS[g].n; i++)
        if (MeshCore::addChannel(GROUPS[g].tags[i], nullptr)) added++;
    return added;
}

uint8_t restore() {
    uint8_t* buf = scratch();
    if (!buf) return 0;
    const size_t n = Settings::loraChannels(buf, BLOB_MAX);
    // Nothing stored means nothing to do -- not "clear the tables", which is
    // what replay() would do with an empty blob.
    const uint8_t got = n ? replay(buf, n) : 0;
    free(buf);
    return got;
}

}
}
