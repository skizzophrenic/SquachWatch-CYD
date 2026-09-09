// SquachMesh payload codec. See include/squachmesh.h for the layout and
// for why it is as small as it is.
//
// Everything here runs against bytes a stranger composed, inside the BLE
// advertisement callback, on every advert in the air. So the shape of this
// file is the same as the pwnagotchi parser next door in signatures.cpp:
// the interesting code is the refusals, and the test suite is mostly cases
// that must NOT parse.
#include "squachmesh.h"

// Compiled ONLY into the mesh build. Without this the codec links into
// every board -- measured at +8,680 bytes of flash on cyd-fast -- and the
// promise that a shipping build is untouched by this feature stops being
// checkable. The host tests define SQUACH_MESH themselves; see test/Makefile.
#if SQUACH_MESH

#include <string.h>

namespace SquachMesh {

// Counts in this build. Only used to clamp on decode -- see the note in the
// header about version skew being a wrong hat rather than an attack.
static const uint8_t NICK_N   = 10;
static const uint8_t OUTFIT_N = 14;
static const uint8_t SHADE_N  = 4;

static inline uint8_t maskOf(uint8_t bits) { return (uint8_t)((1u << bits) - 1u); }

// Printable ASCII only, the same rule and the same reason as the pwnagotchi
// name parser: this string lands in a field the screen renders, and a
// control character in it would be somebody else deciding what our display
// does.
static inline bool printable(char c) {
    return (uint8_t)c >= 0x20 && (uint8_t)c <= 0x7E;
}

size_t encode(const Peer& p, uint8_t* out) {
    // Decide first whether this is really a custom name. An empty one is
    // not, and encoding it as custom would put a payload on the air that
    // every receiver then has to have a rule for.
    bool custom = p.custom && p.name[0] != '\0';

    uint16_t word = 0;
    word |= (uint16_t)((p.nick   & maskOf(NICK_BITS))   << NICK_SHIFT);
    word |= (uint16_t)((p.outfit & maskOf(OUTFIT_BITS)) << OUTFIT_SHIFT);
    word |= (uint16_t)((p.shade  & maskOf(SHADE_BITS))  << SHADE_SHIFT);
    if (custom) word |= CUSTOM_BIT;

    memcpy(out, MAGIC, sizeof(MAGIC));
    out[4] = VERSION;
    out[5] = (uint8_t)(word & 0xFF);          // little-endian, as the rest
    out[6] = (uint8_t)((word >> 8) & 0xFF);   // of this project stores 16s
    out[7] = 0;                                // flags: reserved, must be 0

    if (!custom) return LEN_INDEXED;

    // Fixed-width and NUL-padded rather than length-prefixed. A length byte
    // is one more field that can lie, and at twelve bytes the padding costs
    // less than the validation would.
    memset(out + LEN_INDEXED, 0, NAME_LEN);
    for (size_t i = 0; i < NAME_LEN && p.name[i]; i++) {
        out[LEN_INDEXED + i] = (uint8_t)p.name[i];
    }
    return LEN_NAMED;
}

bool decode(const uint8_t* in, size_t len, Peer& out) {
    if (!in) return false;
    // Length is checked before anything is read, so a short buffer can
    // never reach the field accesses below.
    if (len != LEN_INDEXED && len != LEN_NAMED) return false;
    if (memcmp(in, MAGIC, sizeof(MAGIC)) != 0) return false;
    if (in[4] != VERSION) return false;
    // Reserved means reserved. Refusing a non-zero flags byte now is what
    // makes it safe to give those bits a meaning later: a v1 device will
    // decline a payload using them rather than misread it.
    if (in[7] != 0) return false;

    const uint16_t word = (uint16_t)(in[5] | ((uint16_t)in[6] << 8));
    const bool custom = (word & CUSTOM_BIT) != 0;

    // The two must agree. A custom bit with no name bytes, or name bytes
    // with no custom bit, is a malformed payload either way -- and catching
    // it here is what lets the caller trust `custom` without re-checking
    // the length it was derived from.
    if (custom && len != LEN_NAMED)   return false;
    if (!custom && len != LEN_INDEXED) return false;

    Peer p;
    p.nick   = (uint8_t)((word >> NICK_SHIFT)   & maskOf(NICK_BITS));
    p.outfit = (uint8_t)((word >> OUTFIT_SHIFT) & maskOf(OUTFIT_BITS));
    p.shade  = (uint8_t)((word >> SHADE_SHIFT)  & maskOf(SHADE_BITS));
    p.custom = custom;
    p.name[0] = '\0';

    if (custom) {
        const uint8_t* n = in + LEN_INDEXED;
        size_t used = 0;
        while (used < NAME_LEN && n[used] != 0) {
            if (!printable((char)n[used])) return false;
            used++;
        }
        // A custom payload whose name is empty is the case encode() refuses
        // to produce, so seeing one means it did not come from us.
        if (used == 0) return false;
        // Everything past the terminator must be padding. Without this a
        // sender could park a second string in the tail -- invisible to a
        // renderer that stops at the NUL, and waiting for any future code
        // that reads the field as a fixed twelve bytes.
        for (size_t i = used; i < NAME_LEN; i++) {
            if (n[i] != 0) return false;
        }
        memcpy(p.name, n, used);
        p.name[used] = '\0';
    }

    // Clamp last, once the payload is known good. Modulo rather than
    // rejection: an index this build does not have is a peer running a
    // newer firmware, and a wrong hat beats refusing to draw them.
    if (p.nick   >= NICK_N)   p.nick   %= NICK_N;
    if (p.outfit >= OUTFIT_N) p.outfit %= OUTFIT_N;
    if (p.shade  >= SHADE_N)  p.shade  %= SHADE_N;

    out = p;
    return true;
}

} // namespace SquachMesh

#endif // SQUACH_MESH
