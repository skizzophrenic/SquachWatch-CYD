// SquachWatch-CYD — signature tables and lookup functions
// Sources per docs/DETECTIONS.md and docs/DESIGN.md §6.
#pragma once
#include "state.h"

// Every OUI carries its own confidence, because the registrant matters as
// much as the prefix. Roughly:
//
//   HIGH  the block is registered to the company that makes the product.
//   MED   registered to a parent whose range is much wider than the
//         product -- Amazon owns Ring, and also Echo, Fire TV and Kindle.
//   LOW   a module or ODM vendor whose parts are in everything (Espressif,
//         Liteon, Murata, Realtek, Telink), or a block that is not in the
//         IEEE registry at all.
//
// The LOW rows are not mistakes and are not being deleted: Flock really
// does build on ESP32, so the prefix really is evidence. It is just
// evidence shared with every dev board on earth, and saying so is the
// difference between a detector and a rumour.
struct OuiEntry   { uint8_t  b[3];     const char* name; DetectionType type; Confidence conf; };
struct UuidEntry  { uint16_t uuid;     const char* name; DetectionType type; };
struct NameEntry  { const char* name;  DetectionType type; };
struct SsidEntry  { const char* prefix; const char* name; DetectionType type; };
struct MfgIdEntry { uint16_t mfgId;    const char* name; DetectionType type; };

extern const OuiEntry    kOuiTable[];
extern const uint16_t    kOuiCount;
extern const UuidEntry   kUuidTable[];
extern const uint16_t    kUuidCount;
extern const NameEntry   kBtClassicNames[];
extern const uint16_t    kBtClassicCount;
extern const SsidEntry   kSsidPrefixes[];
extern const uint16_t    kSsidCount;
extern const MfgIdEntry  kMfgIdTable[];
extern const uint16_t    kMfgIdCount;

// First-match-wins lookups. Precedence per DESIGN.md §6.2.
// Yields the matched entry's own confidence through `conf` when given one.
// Callers that do not care keep the old one-argument form.
DetectionType lookupOui(const uint8_t* mac, Confidence* conf = nullptr);
DetectionType lookupUuid(uint16_t uuid16);
DetectionType lookupBtName(const char* name);
DetectionType lookupSsid(const char* ssid);   // case-insensitive prefix

// Friendly vendor label for whichever kSsidPrefixes entry matched
// (e.g. "Axon-Body2"), or nullptr if none did. Same matching rule as
// lookupSsid — kept separate rather than changing that function's
// signature, since other callers just want the DetectionType.
const char* ssidVendorName(const char* ssid);
DetectionType lookupMfgId(uint16_t mfgId);

// AirTag check, run against the RAW advertisement bytes rather than
// NimBLE's parsed manufacturer-data field -- pass adv->getPayload() and
// adv->getPayloadLength(). Scanning the raw advert also catches tags
// whose Find My structure sits behind other AD structures, or arrives
// in a scan response, where the parsed field alone would miss it.
//
// See the implementation for exactly which byte patterns match and what
// that costs; the short version is that this deliberately trades some
// precision for actually catching a tag that has just been powered on.
bool isAirTagPayload(const uint8_t* payload, uint8_t len);

// iBeacon check, run against the PARSED manufacturer-data field (company
// ID included, i.e. what NimBLE's getManufacturerData() returns).
//
// Unlike the AirTag test above this one is exact rather than heuristic:
// Apple's format fixes every byte of the header and the total length, so
// there is nothing to trade away. See the implementation.
bool isIBeacon(const uint8_t* mfg, uint8_t len);

// How sure we are that a match is really what it claims to be — mirrors
// the per-signature grading in docs/DETECTIONS.md, collapsed to one
// value per DetectionType. Where a type bundles signatures of differing
// documented confidence (e.g. AIRTAG covers both Apple's High-confidence
// AirTag match and Tile's Medium-confidence one, and the engine doesn't
// currently distinguish which matched), this reports the conservative
// (lower) grade rather than overstating certainty.
// Note: plain LOW/MEDIUM/HIGH collide with Arduino core's LOW/HIGH
// pin-state macros via textual substitution (enum class scoping
// doesn't protect against the preprocessor), hence the _CONF suffix.
// Confidence itself now lives in state.h -- see the note there.
Confidence  confidenceFor(DetectionType t);
const char* confidenceLabel(Confidence c);     // "HIGH CONF" / "MED CONF" / "LOW CONF"
uint8_t     confidencePercent(Confidence c);   // ~90 / ~60 / ~30 — an honest approximation, not a measured stat
