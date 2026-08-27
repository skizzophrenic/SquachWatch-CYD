// SquachWatch-CYD — signature tables and lookup functions
// Sources per docs/DETECTIONS.md and docs/DESIGN.md §6.
#pragma once
#include "state.h"

struct OuiEntry   { uint8_t  b[3];     const char* name; DetectionType type; };
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
DetectionType lookupOui(const uint8_t* mac);
DetectionType lookupUuid(uint16_t uuid16);
DetectionType lookupBtName(const char* name);
DetectionType lookupSsid(const char* ssid);   // case-insensitive prefix

// Friendly vendor label for whichever kSsidPrefixes entry matched
// (e.g. "Axon-Body2"), or nullptr if none did. Same matching rule as
// lookupSsid — kept separate rather than changing that function's
// signature, since other callers just want the DetectionType.
const char* ssidVendorName(const char* ssid);
DetectionType lookupMfgId(uint16_t mfgId);

// Apple AirTag subtype check: 0x12 (near owner) or 0x1E (separated).
// Pass the full mfg-data payload (after the 2-byte mfgId).
bool isAirTagSubtype(const uint8_t* mfgPayload, uint8_t len);

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
enum class Confidence : uint8_t { LOW_CONF, MED_CONF, HIGH_CONF };
Confidence  confidenceFor(DetectionType t);
const char* confidenceLabel(Confidence c);     // "HIGH CONF" / "MED CONF" / "LOW CONF"
uint8_t     confidencePercent(Confidence c);   // ~90 / ~60 / ~30 — an honest approximation, not a measured stat
