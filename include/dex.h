// SquachWatch-CYD — the SQUACHY-DEX: one entry per detection type.
//
// BINGO is a card you fill once a week. The DEX never finishes: eighteen
// numbered entries, a silhouette for every type never caught, and a card
// behind each with what it is, where it lives, what Squachy thinks of it,
// and this board's own record against it. The counts come from the
// engine's lifetime totals; what the engine never kept -- first catch,
// closest signal, catches at night -- is kept here, one small blob in NVS.
#pragma once
#include <stdint.h>
#include "state.h"

class DetectionEngine;

namespace Dex {

static const uint8_t ENTRIES = CATALOGUE_N;    // UNKNOWN and MINE have no card

// Entry i (0-based) is catalogueAt(i): the types in order, MINE stepped over.
DetectionType typeAt(uint8_t i);
uint8_t       indexOf(DetectionType t);

enum class Rarity : uint8_t { COMMON, UNCOMMON, RARE };
Rarity      rarity(DetectionType t);
const char* rarityName(Rarity r);        // "COMMON" ...
uint8_t     stars(Rarity r);             // 1..3

// The card's text. Written for the screen: lore wraps to a few lines at
// text size 1, the rest are one line each.
const char* shortName(DetectionType t);  // the tile: "GLASS", "STAG"
const char* kind(DetectionType t);       // "TRACKER", "CAMERA", "POLICE KIT"...
const char* lore(DetectionType t);
const char* habitat(DetectionType t);
const char* quip(DetectionType t);       // Squachy, on the record
const char* hint(DetectionType t);       // for an uncaught card
const char* radio(DetectionType t);      // "BLUETOOTH" / "WIFI"

// This board's record. Loaded once at boot; note() on every sighting (RAM
// only -- it is called from the Bluetooth host task); tick() writes it
// back from loop() a while after it changed.
void begin();
void note(DetectionType t, int8_t rssi);
void tick(uint32_t now);

struct Record {
    uint32_t firstEpoch;   // 0 when never caught, or caught before the DEX existed
    uint32_t lastEpoch;    // 0 likewise
    int8_t   bestRssi;     // the closest it has come; -128 when none
    uint16_t night;        // catches between eleven and five
};
const Record& record(DetectionType t);

// A new closest was just set for this type (a real improvement on a
// previous best, not the first reading). True once per improvement.
bool takeNewClosest(DetectionType t);

// His nemesis: the type caught most, once it has been caught ten times.
// UNKNOWN until then.
DetectionType nemesis(const DetectionEngine& eng);

// Everything back to zero. RESET STATS does this beside the lifetime counts.
void reset();

}  // namespace Dex
