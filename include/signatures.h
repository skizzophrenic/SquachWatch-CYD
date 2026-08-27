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
DetectionType lookupMfgId(uint16_t mfgId);

// Apple AirTag subtype check: 0x12 (near owner) or 0x1E (separated).
// Pass the full mfg-data payload (after the 2-byte mfgId).
bool isAirTagSubtype(const uint8_t* mfgPayload, uint8_t len);
