// SquachWatch-CYD -- goblins from Network Goblin Scout, as visitors.
//
// Network Goblin Scout is a "digital pet" for the NM-CYD-C5 by MostlyWithinSpec
// (issue #37). Its goblin announces itself in a Bluetooth advert, and its
// maker published the format so a Squachy could have a goblin drop by. This
// is the decoder for that advert and nothing else: no radio, no drawing, so
// test/goblin_test.cpp covers every refusal without a board.
//
// THE ADVERT. Manufacturer data under company ID 0xFFFF, the same test ID
// SquachMesh uses. The two never collide: SquachMesh wants 'S' 'Q' 'M' '1'
// and refuses this, and this wants 'N' 'G' and refuses SquachMesh.
//
//   payload byte   field (after the two company-ID bytes)
//   0-1            magic 'N' 'G'
//   2              version, >= 1 (fields are only ever appended)
//   3-6            goblin ID, uint32 little-endian, never 0
//   7-8            level, uint16 little-endian
//   9              hue, 0-255 around the colour wheel
//   10             flags: bits 0-4 the hat (0 = none), bits 5-7 the hoard tier 0-7
//   11+            name, up to 12 printable ASCII characters, no terminator
//
// A goblin is a visitor, never a detection. It is not logged, not counted,
// not alerted on, and it never reaches the signature tables. Its ID stays the
// same across reboots (that is how goblins remember each other), so nothing
// here keeps it past the visit it came for.
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace Goblin {

static const uint16_t COMPANY_ID = 0xFFFF;
static const uint8_t  MAGIC0 = 0x4E, MAGIC1 = 0x47;   // 'N' 'G'
static const size_t   LEN_MIN  = 11;                  // up to the flags byte
static const size_t   NAME_LEN = 12;

struct Visitor {
    uint32_t id;                  // never 0
    uint16_t level;
    uint8_t  hue;                 // 0-255 around the wheel: his skin
    uint8_t  hat;                 // 0 = none, 1-31 = which one (theirs; we draw a cap)
    uint8_t  hoard;               // 0-7: how big a sack of loot he carries
    char     name[NAME_LEN + 1];  // NUL-terminated, may be empty
};

// Parses the payload AFTER the company ID. False, and `out` untouched, unless
// the magic, a version of 1 or more, the length and a non-zero ID are all
// right. The name is the printable run from byte 11, up to twelve characters;
// anything past a byte that is not printable ASCII is not believed.
bool decode(const uint8_t* in, size_t len, Visitor& out);

// Builds one, for the emulator and the tests. Returns the bytes written
// (LEN_MIN plus the name). `out` must have room for LEN_MIN + NAME_LEN.
size_t encode(const Visitor& v, uint8_t* out);

// His colour as RGB565: the hue at a goblin's middling saturation.
uint16_t skin565(uint8_t hue, uint8_t value = 200);

} // namespace Goblin
