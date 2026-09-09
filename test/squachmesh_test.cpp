// The SquachMesh payload codec.
//
// This runs inside the BLE advertisement callback, on every advert in the
// air, against bytes a stranger composed. So -- exactly like the pwnagotchi
// parser it sits beside -- the cases worth writing down are not "does the
// round trip work". They are the refusals: a payload that is nearly right,
// a name that never terminates, a length that disagrees with the flag that
// implies it, bytes hiding in the padding.
//
// The round trip is here too, but it is the cheap half.
#include "squachmesh.h"
#include "test_util.h"
#include <cstring>
#include <cstdio>

using namespace SquachMesh;

static uint8_t buf[64];

static Peer mk(uint8_t nick, uint8_t outfit, uint8_t shade, const char* name) {
    Peer p{};
    p.nick = nick; p.outfit = outfit; p.shade = shade;
    p.custom = (name != nullptr);
    p.name[0] = '\0';
    if (name) {
        size_t i = 0;
        for (; i < NAME_LEN && name[i]; i++) p.name[i] = name[i];
        p.name[i] = '\0';
    }
    return p;
}

// A known-good indexed payload, rebuilt before each mutation so one test
// cannot leak damage into the next.
static size_t good(bool custom) {
    memset(buf, 0xAA, sizeof(buf));
    return encode(mk(3, 7, 2, custom ? "STOMPY" : nullptr), buf);
}

int main() {
    Peer p{};

    suite("A payload round-trips");
    {
        size_t n = encode(mk(3, 7, 2, nullptr), buf);
        ck("an indexed payload is 8 bytes", n == LEN_INDEXED);
        ck("it decodes", decode(buf, n, p));
        ck("nickname survives",  p.nick == 3);
        ck("outfit survives",    p.outfit == 7);
        ck("shades survive",     p.shade == 2);
        ck("it is not custom",   !p.custom);
        ck("the name is empty",  p.name[0] == '\0');

        n = encode(mk(9, 13, 3, "STOMPY"), buf);
        ck("a named payload is 20 bytes", n == LEN_NAMED);
        ck("it decodes", decode(buf, n, p));
        ck("nickname survives",  p.nick == 9);
        ck("outfit survives",    p.outfit == 13);
        ck("shades survive",     p.shade == 3);
        ck("it is custom",       p.custom);
        ck("the name survives",  strcmp(p.name, "STOMPY") == 0);
    }

    suite("A name exactly fills the field");
    {
        // Twelve characters and no room for a terminator inside the field.
        // The decoder has to stop at the field width rather than at a NUL
        // it will never find.
        size_t n = encode(mk(0, 0, 0, "ABCDEFGHIJKL"), buf);
        ck("it is a named payload", n == LEN_NAMED);
        ck("it decodes", decode(buf, n, p));
        ck("all twelve characters survive", strcmp(p.name, "ABCDEFGHIJKL") == 0);
    }

    suite("Encoding refuses to produce nonsense");
    {
        // A custom name that is empty is not a custom name. Letting one on
        // the air would mean every receiver needed a rule for it.
        Peer e = mk(1, 1, 1, "");
        e.custom = true;
        size_t n = encode(e, buf);
        ck("an empty custom name encodes as indexed", n == LEN_INDEXED);
        ck("and decodes as not-custom", decode(buf, n, p) && !p.custom);

        // Longer than the field: truncated, never overrun.
        Peer l{};
        l.custom = true;
        memcpy(l.name, "ABCDEFGHIJKL", 13);   // exactly NAME_LEN + NUL
        n = encode(l, buf);
        ck("a full-width name still fits", n == LEN_NAMED);
        ck("and comes back whole", decode(buf, n, p) && strlen(p.name) == NAME_LEN);
    }

    suite("The header must be right");
    {
        for (int i = 0; i < 4; i++) {
            size_t n = good(false);
            buf[i] ^= 0xFF;
            char what[64];
            snprintf(what, sizeof(what), "magic byte %d wrong is refused", i);
            ck(what, !decode(buf, n, p));
        }
        size_t n = good(false);
        buf[4] = VERSION + 1;
        ck("an unknown version is refused", !decode(buf, n, p));

        n = good(false);
        buf[7] = 0x01;
        ck("a non-zero reserved flags byte is refused", !decode(buf, n, p));
    }

    suite("Only two lengths are legal");
    {
        size_t n = good(true);
        ck("the real length decodes", decode(buf, n, p));
        for (size_t l = 0; l < LEN_NAMED; l++) {
            if (l == LEN_INDEXED) continue;   // legal, but for the other flag
            if (!decode(buf, l, p)) continue;
            char what[64];
            snprintf(what, sizeof(what), "length %zu should have been refused", l);
            ck(what, false);
        }
        ck("every truncation short of 20 is refused", true);
        ck("one byte too many is refused", !decode(buf, LEN_NAMED + 1, p));
        ck("a null buffer is refused", !decode(nullptr, LEN_INDEXED, p));
    }

    suite("The custom bit and the length must agree");
    {
        // Custom bit set, but only the indexed bytes present. Without this
        // check the name read would run off the end of the advert.
        size_t n = good(false);
        buf[5] |= (uint8_t)(CUSTOM_BIT & 0xFF);
        ck("custom bit on a short payload is refused", !decode(buf, n, p));

        // The reverse: name bytes present, bit clear.
        n = good(true);
        buf[5] &= (uint8_t)~(CUSTOM_BIT & 0xFF);
        ck("name bytes without the custom bit are refused", !decode(buf, n, p));
    }

    suite("Names are printable ASCII or they are refused");
    {
        const struct { uint8_t byte; const char* what; } bad[] = {
            { 0x01, "a control character is refused" },
            { 0x0A, "a newline is refused" },
            { 0x1B, "an escape is refused" },
            { 0x7F, "DEL is refused" },
            { 0x80, "a high-bit byte is refused" },
            { 0xFF, "0xFF is refused" },
        };
        for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
            size_t n = good(true);
            buf[LEN_INDEXED + 2] = bad[i].byte;
            ck(bad[i].what, !decode(buf, n, p));
        }

        // An all-padding name with the custom bit set: encode() will not
        // make one, so it did not come from us.
        size_t n = good(true);
        memset(buf + LEN_INDEXED, 0, NAME_LEN);
        ck("an empty custom name is refused", !decode(buf, n, p));
    }

    suite("Nothing hides in the padding");
    {
        // "SQ\0" then a second string in the tail. A renderer that stops at
        // the terminator would never show it, which is exactly why it must
        // not be allowed to travel: any later code reading the field as a
        // fixed twelve bytes would.
        size_t n = good(true);
        memset(buf + LEN_INDEXED, 0, NAME_LEN);
        memcpy(buf + LEN_INDEXED, "SQ", 2);
        memcpy(buf + LEN_INDEXED + 5, "HIDDEN", 6);
        ck("data after the terminator is refused", !decode(buf, n, p));

        // The same payload without the stowaway is fine, so the refusal
        // above is about the tail and not about the short name.
        n = good(true);
        memset(buf + LEN_INDEXED, 0, NAME_LEN);
        memcpy(buf + LEN_INDEXED, "SQ", 2);
        ck("...but a short name with clean padding is fine",
           decode(buf, n, p) && strcmp(p.name, "SQ") == 0);
    }

    suite("Unknown indices clamp rather than refuse");
    {
        // Four bits hold 0..15; this build has 10 nicknames and 14 outfits.
        // A peer on newer firmware is a wrong hat, not an attack.
        size_t n = encode(mk(15, 15, 3, nullptr), buf);
        ck("a payload with out-of-range indices still decodes", decode(buf, n, p));
        ck("nickname lands in range", p.nick < 10);
        ck("outfit lands in range",   p.outfit < 14);
        ck("shades land in range",    p.shade < 4);
    }

    suite("A rejected payload leaves the output alone");
    {
        Peer keep = mk(5, 5, 1, nullptr);
        Peer probe = keep;
        size_t n = good(false);
        buf[4] = 0xEE;                       // bad version
        ck("decode fails", !decode(buf, n, probe));
        ck("the caller's peer is untouched",
           probe.nick == keep.nick && probe.outfit == keep.outfit &&
           probe.shade == keep.shade && probe.custom == keep.custom);
    }

    suite("SquachEmit's hand-built payload decodes");
    {
        // Byte-for-byte what SquachEmit lays out in fireSquachMesh(), typed
        // out again rather than shared. That duplication IS the test: two
        // independent implementations agreeing about a format neither owns
        // proves something, whereas one implementation called from both
        // sides proves only that it equals itself.
        //
        // The company ID prefix is stripped by the receive hook before
        // decode() sees it, so this starts at the magic.
        const uint8_t nick = 7, outfit = 12, shade = 2;
        uint8_t e[20];
        e[0] = 'S'; e[1] = 'Q'; e[2] = 'M'; e[3] = '1';
        e[4] = 1;
        uint16_t w = (uint16_t)((nick & 0x0F) << 12) |
                     (uint16_t)((outfit & 0x0F) << 8) |
                     (uint16_t)((shade & 0x03) << 6);
        w |= (uint16_t)(1u << 5);                 // custom-name bit
        e[5] = (uint8_t)(w & 0xFF);
        e[6] = (uint8_t)(w >> 8);
        e[7] = 0;
        const char* nm = "ZERO COOL";
        for (size_t i = 0; i < 12; i++) e[8 + i] = (i < strlen(nm)) ? (uint8_t)nm[i] : 0;

        ck("it decodes", decode(e, 20, p));
        ck("nickname agrees", p.nick == nick);
        ck("outfit agrees",   p.outfit == outfit);
        ck("shades agree",    p.shade == shade);
        ck("it is custom",    p.custom);
        ck("the name agrees", strcmp(p.name, "ZERO COOL") == 0);

        // And the indexed form, which is what a device with no typed name
        // sends -- ten bytes on the wire once the company prefix is off.
        uint8_t f[8];
        memcpy(f, e, 8);
        f[5] = (uint8_t)(f[5] & ~(1u << 5));      // clear the custom bit
        ck("the indexed form decodes", decode(f, 8, p));
        ck("and reports no name", !p.custom && p.name[0] == 0);
    }

    return report();
}
