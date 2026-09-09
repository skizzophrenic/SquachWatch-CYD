// SquachMesh -- the wire format two SquachWatches use to recognise each
// other and describe their own Squachy well enough for the other device to
// draw it.
//
// This header is the whole protocol. It deliberately contains no radio: the
// codec is arithmetic over a byte buffer, which is what lets the host tests
// in test/squachmesh_test.cpp cover every refusal path without a board.
//
// WHY IT IS THIS SMALL. Everything about Squachy's appearance is already a
// curated index rather than free text -- ten nicknames, fourteen outfits,
// four shade colours -- and both devices ship the identical tables. So the
// only thing that has to travel is WHICH ENTRY, and the whole appearance
// packs into a single 16-bit word. The name bytes exist only for the
// minority of devices whose owner typed their own.
//
// The payload rides in BLE manufacturer-specific data under company ID
// 0xFFFF. That ID is reserved by the SIG for exactly this kind of
// non-production use, and is the honest choice for a project with no
// registration of its own -- this codebase already documents what happens
// when somebody squats a registered ID they do not own: Marauder labels
// Flipper's company as 0x0FBA, which belongs to a headset manufacturer, and
// every project that copied the constant inherited the mistake.
//
// 0xFFFF is shared with every other hobby project that made the same call,
// which is precisely why MAGIC is not optional.
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace SquachMesh {

// Company ID for the manufacturer-specific AD structure.
static const uint16_t COMPANY_ID = 0xFFFF;

// Four bytes of "this is one of ours", checked before anything else is
// believed. 'S' 'Q' 'M' '1'.
static const uint8_t MAGIC[4] = { 0x53, 0x51, 0x4D, 0x31 };

// Bumped when the layout below changes incompatibly. A device that does not
// recognise the version refuses the whole payload rather than guessing at
// fields that may have moved.
static const uint8_t VERSION = 1;

// Name field. Twelve bytes, ASCII, NUL-padded when shorter. The cap is a
// rendering budget rather than a storage one: the name has to fit a
// nameplate above a Squachy drawn at SMALL, which is a tighter measure than
// the settings row it lives in on its own device.
static const size_t NAME_LEN = 12;

// The two lengths a valid payload can have. Nothing in between is legal --
// a custom-name payload is all twelve name bytes or it is malformed.
static const size_t LEN_INDEXED = 8;
static const size_t LEN_NAMED   = LEN_INDEXED + NAME_LEN;   // 20
static const size_t LEN_MAX     = LEN_NAMED;

// ---------------------------------------------------------------------
// The appearance word. Field order matches the byte map in the plan doc.
//
//   bits 12..15  nickname index   (4)  -- 10 in use
//   bits  8..11  outfit index     (4)  -- 14 in use
//   bits  6..7   shades index     (2)  --  4 in use
//   bit      5   custom-name flag
//   bits  0..4   spare            (5)
//
// SIZE is deliberately NOT here. Both Squachys are drawn at SMALL while
// they are meeting -- that is what makes two of them fit -- so the peer's
// own size preference is never read, and a field nobody reads is worse than
// no field at all. Those bits went back to spare.
// ---------------------------------------------------------------------
static const uint8_t  NICK_SHIFT   = 12, NICK_BITS   = 4;
static const uint8_t  OUTFIT_SHIFT =  8, OUTFIT_BITS = 4;
static const uint8_t  SHADE_SHIFT  =  6, SHADE_BITS  = 2;
static const uint16_t CUSTOM_BIT   = 1u << 5;

struct Peer {
    uint8_t nick;              // index into the shared NICKNAMES table
    uint8_t outfit;            // index into the shared OUTFITS table
    uint8_t shade;             // index into the shared SHADE_NAMES table
    bool    custom;            // true when `name` was typed, not indexed
    char    name[NAME_LEN + 1];// NUL-terminated; empty unless custom
};

// Builds the advertised payload. Returns the number of bytes written, which
// is LEN_INDEXED or LEN_NAMED. `out` must have room for LEN_MAX.
//
// A custom name that is empty once trimmed encodes as indexed instead --
// there is no such thing as a valid nameless custom name, and letting one
// onto the wire would mean every receiver had to handle it.
size_t encode(const Peer& p, uint8_t* out);

// Parses a payload. Returns false and leaves `out` untouched unless the
// whole thing is well formed: magic, version, an exact length, a flags byte
// that is zero, and -- when the custom bit is set -- a name that is
// printable ASCII with nothing hiding after its terminator.
//
// Indices are clamped on the way out rather than rejected. An index the
// sender had and this build does not is a version skew, not an attack, and
// drawing the wrong hat is a better failure than refusing the peer.
bool decode(const uint8_t* in, size_t len, Peer& out);

} // namespace SquachMesh
