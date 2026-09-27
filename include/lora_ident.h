// SquachWatch-CYD — what a node row says about itself, with the radio off.
//
// Everything here is a pure function of fields the node table already holds:
// a grid square from a decoded position, a callsign out of a free-text name,
// a DXCC entity from a callsign's prefix. No WiFi, no request, no cache, no
// rate limit, nothing that can fail -- which is why it ships ahead of the
// online lookups in lora_enrich.h and why most of the value is here.
//
// WHERE IT RUNS. At DISPLAY time, on a copy of the row, never inside
// Nodes::note(). note() runs on the radio task while the sniffer holds
// s_lock and loop() blocks on the same mutex (src/lora_sniffer.cpp), so a
// string scan in there is a string scan the screen waits for.
//
// CLAIMED, NOT VERIFIED. For LoRa APRS and MeshCom the callsign is a field:
// a licensed station's own identification, transmitted because the law says
// it must be (docs/LORA.md section 8). For Meshtastic and MeshCore it is a
// substring of a name a stranger typed, and Ident::claimed says so. Anything
// drawn from a claimed callsign is drawn from a guess about somebody else,
// which is why it is marked on every screen that shows it and why
// lora_enrich.cpp will not send one anywhere.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "lora_nodes.h"

namespace Lora {
namespace Ident {

// A Maidenhead locator for a decoded position: two field letters, two square
// digits, two subsquare letters. Six characters and a NUL, so out[7].
//
// The unit hams and mesh operators actually speak, and free: no table, just
// arithmetic on the integers the row already keeps. Positions outside the
// grid's range (the poles, the antimeridian) are clamped to the last square
// rather than refused, because the alternative is a blank beside a position
// the row is certain about. False only when out is null.
bool grid(int32_t latE7, int32_t lonE7, char* out, size_t cap);

// ---- callsign prefix to DXCC entity -----------------------------------------
// From cty.dat, MIT licensed, generated into include/lora_dxcc_table.h by
// tools/gen_dxcc.py -- 324 entities and 1,937 prefixes, about 21 kB of flash.
// Longest prefix wins, so BV9P is Pratas Island where BV is Taiwan.
//
// NO CQ OR ITU ZONE. The generator drops cty.dat's per-call-area rows, so the
// only zone left to report would be the entity's default, and that is wrong
// for every country spanning more than one zone -- the United States spans CQ
// 3 to 5. See the generator for the measurement behind that cut.
struct Dxcc {
    const char* entity;        // "Fed. Rep. of Germany"
    const char* continent;     // "EU"
};
// False when no allocation covers the prefix, which is a real answer: 1C8WR
// has no 1C allocation and is one of the false positives extraction produces.
bool dxcc(const char* call, Dxcc& out);

// ---- a callsign out of a name ------------------------------------------------
// Measured over 104,776 real node names (63,724 MeshCore adverts, 9,915
// meshmap.net and 31,137 liamcottle Meshtastic long names): about one name in
// eight carries a callsign-shaped token, and for tokens of five characters or
// more the extraction was right 29 times in 30 against a live callsign
// database. The rules, and what each one is worth, are at the top of
// src/lora_ident.cpp.
struct Call {
    char        text[12];      // "DL1TMA", or "DH5DAX-7" when an SSID follows
    uint8_t     len;           // characters of the callsign itself, SSID excluded
    const char* entity;        // nullptr when the prefix has no allocation
    const char* continent;
};

// The four hex digits a firmware appends to its own default name, as a 16-bit
// number, or -1 for a network that appends none. Both mesh firmwares do it --
// "Meshtastic d9ec", "DE-NW-GM-0C0A" -- and a four-character hex string is
// callsign-shaped whenever its second character is a digit, which makes this
// the dominant false positive by a wide margin (up to 22.5 % of all tokens in
// one corpus). Meshtastic uses the low 16 bits of the node number; MeshCore
// the first two bytes of the advert public key. Both are already in the row.
int32_t selfHex(const Nodes::Node& n);

// Tokens found in `name`, best first. `selfHex` is the value above, or -1.
// Returns how many were written, which may be more than the caller wants to
// see: a name with two tokens is an owner/operator pair ("N7JMV/KC7OOU-R"),
// and picking one of them invents a fact, so a caller shows all or none.
static const uint8_t CALL_MAX = 4;
uint8_t calls(const char* name, int32_t selfHex, Call* out, uint8_t cap);

// ---- the whole offline answer for one row -----------------------------------
struct Ident {
    char    grid[7];           // "" when the row has no position
    Call    call[CALL_MAX];
    uint8_t callCount;         // 0, 1, or 2+; show them all or show none
    bool    claimed;           // the callsign came out of free text, not a field
};
// Reads only the row. For LoRa APRS and MeshCom the tag IS the callsign and
// claimed is false; for Meshtastic and MeshCore the name is scanned and
// claimed is true; for LoRaWAN and FANET there is no callsign to find and
// only the grid is filled.
void describe(const Nodes::Node& n, Ident& out);

}
}
