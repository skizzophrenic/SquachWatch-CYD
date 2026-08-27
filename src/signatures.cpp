// SquachWatch-CYD — signature data tables and lookup functions
// Sources per docs/DETECTIONS.md.
#include "signatures.h"
#include <string.h>
#include <strings.h>   // strncasecmp

// OUI table. Order MATTERS: lookupOui walks top-to-bottom and returns
// the first match. Per DESIGN.md §6.2: Axon > Flock > ALPR > cameras >
// ESP32 family > skimmer. Some Flock hardware uses Espressif modules,
// so Flock entries for the same prefix must appear before generic ESP32.
const OuiEntry kOuiTable[] = {
    // ---- Axon / Taser (priority: highest) ----
    {{0x00, 0x25, 0xDF}, "Axon",       DetectionType::AXON},
    {{0xE4, 0x05, 0x40}, "Axon-Body",  DetectionType::AXON},
    {{0x28, 0x24, 0xFF}, "Axon-Signal", DetectionType::AXON},

    // ---- Flock Safety (ESP32 modules + LTE backhaul) ----
    // Source: colonelpanichacks/flock-you, @NitekryDPaul, DeFlockJoplin
    {{0x24, 0x0A, 0xC4}, "Flock-ESP32",  DetectionType::FLOCK},
    {{0x30, 0xAE, 0xA4}, "Flock-ESP32",  DetectionType::FLOCK},
    {{0x24, 0x6F, 0x28}, "Flock-ESP32",  DetectionType::FLOCK},
    {{0xCC, 0x50, 0xE3}, "Flock-ESP32",  DetectionType::FLOCK},
    {{0xDC, 0x54, 0x75}, "Flock-ESP32",  DetectionType::FLOCK},
    {{0xE8, 0x9F, 0x6D}, "Flock-ESP32",  DetectionType::FLOCK},
    {{0x8C, 0xAA, 0xB5}, "Flock-ESP-S3", DetectionType::FLOCK},
    {{0x34, 0x85, 0x18}, "Flock-ESP-S3", DetectionType::FLOCK},
    {{0xB4, 0x1E, 0x52}, "Flock-MA-L",   DetectionType::FLOCK},
    {{0xD4, 0xAD, 0xFC}, "Flock-ESP32",  DetectionType::FLOCK},
    {{0xAC, 0x67, 0xB2}, "Flock-ESP32",  DetectionType::FLOCK},
    {{0x84, 0xF3, 0xEB}, "Flock-ESP-S3", DetectionType::FLOCK},
    {{0xB4, 0xE6, 0x2D}, "Flock-ESP32",  DetectionType::FLOCK},
    {{0xCC, 0xDB, 0xA7}, "Flock-ESP32",  DetectionType::FLOCK},
    {{0x94, 0xB9, 0x7E}, "Flock-ESP32",  DetectionType::FLOCK},
    {{0xA4, 0xCF, 0x12}, "Flock-ESP-S2", DetectionType::FLOCK},
    {{0xC0, 0x49, 0xEF}, "Flock-ESP-C6", DetectionType::FLOCK},
    {{0x24, 0xB2, 0xB9}, "Flock-Liteon", DetectionType::FLOCK},
    {{0xD0, 0x39, 0x57}, "Flock",        DetectionType::FLOCK},
    {{0x00, 0xF4, 0x8D}, "Flock",        DetectionType::FLOCK},
    {{0x14, 0x5A, 0xFC}, "Flock",        DetectionType::FLOCK},
    {{0x80, 0x30, 0x49}, "Flock",        DetectionType::FLOCK},
    {{0xE0, 0x0A, 0xF6}, "Flock",        DetectionType::FLOCK},
    {{0x70, 0xC9, 0x4E}, "Flock",        DetectionType::FLOCK},
    {{0x3C, 0x91, 0x80}, "Flock",        DetectionType::FLOCK},
    {{0xD8, 0xF3, 0xBC}, "Flock",        DetectionType::FLOCK},
    {{0xB8, 0x35, 0x32}, "Flock",        DetectionType::FLOCK},
    {{0x82, 0x6B, 0xF2}, "Flock-DeFlk",  DetectionType::FLOCK},
    {{0x00, 0xA0, 0xD8}, "Flock-Sierra", DetectionType::FLOCK},

    // ---- Motorola / Vigilant ALPR ----
    {{0x00, 0x0E, 0x58}, "ALPR-Viglnt",  DetectionType::ALPR},

    // ---- Skimmer OUIs (BT Classic module prefixes) ----
    {{0x20, 0x13, 0x00}, "Skim-Linvor",  DetectionType::SKIMMER},
    {{0x98, 0xD3, 0x00}, "Skim-SPP",     DetectionType::SKIMMER},
    {{0x00, 0x1A, 0x7D}, "Skim-CSR",     DetectionType::SKIMMER},

    // ---- Specific camera vendors ----
    {{0x2C, 0xAA, 0x8E}, "Wyze",         DetectionType::CAMERA},
    {{0xD0, 0x3F, 0x27}, "Wyze",         DetectionType::CAMERA},
    {{0x7C, 0x78, 0xB2}, "Wyze",         DetectionType::CAMERA},
    {{0xB8, 0xD7, 0xAF}, "Wyze-Mod",     DetectionType::CAMERA},
    {{0xFC, 0x65, 0xDE}, "Ring",         DetectionType::CAMERA},
    {{0x68, 0x37, 0xE9}, "Ring",         DetectionType::CAMERA},
    {{0x34, 0xD2, 0x70}, "Amazon",       DetectionType::CAMERA},
    {{0xF0, 0x27, 0x2D}, "Hikvision",    DetectionType::CAMERA},
    {{0xC0, 0x56, 0xE3}, "Hikvision",    DetectionType::CAMERA},
    {{0x44, 0x19, 0xB6}, "Hikvision",    DetectionType::CAMERA},
    {{0x28, 0x57, 0xBE}, "Reolink",      DetectionType::CAMERA},
    {{0x00, 0xE0, 0x4C}, "Realtek",      DetectionType::CAMERA},
    {{0xBC, 0xDD, 0xC2}, "Arlo",         DetectionType::CAMERA},
    {{0x4C, 0x69, 0x05}, "Blink",        DetectionType::CAMERA},
    {{0xA4, 0xC1, 0x38}, "Tuya",         DetectionType::CAMERA},
};
const uint16_t kOuiCount = sizeof(kOuiTable) / sizeof(kOuiTable[0]);

// 16-bit BLE service UUIDs. Note: SPP 0x1101 is the *16-bit* form of
// 00001101-0000-1000-8000-00805F9B34FB. Caller extracts the 16-bit
// UUID from the advertised service data.
const UuidEntry kUuidTable[] = {
    {0x1101, "Skim-SPP",   DetectionType::SKIMMER},   // Classic SPP
    {0xFEED, "Tile",       DetectionType::AIRTAG},    // Tile (tracker class)
    {0xFD5F, "Meta",       DetectionType::META},      // Ray-Ban Meta glasses
    {0x3100, "Raven",      DetectionType::RAVEN},     // Raven gunshot detector
    {0x3200, "Raven",      DetectionType::RAVEN},
    {0x3300, "Raven",      DetectionType::RAVEN},
    {0x3400, "Raven",      DetectionType::RAVEN},
    {0x3500, "Raven",      DetectionType::RAVEN},
    {0xFFFA, "DroneID",    DetectionType::DRONE},     // OpenDroneID
};
const uint16_t kUuidCount = sizeof(kUuidTable) / sizeof(kUuidTable[0]);

// BT Classic device names (skimmers typically advertise as HC-05 etc).
const NameEntry kBtClassicNames[] = {
    {"HC-03",       DetectionType::SKIMMER},
    {"HC-05",       DetectionType::SKIMMER},
    {"HC-06",       DetectionType::SKIMMER},
    {"RN42",        DetectionType::SKIMMER},
    {"BT04-A",      DetectionType::SKIMMER},
    {"Flock_Setup", DetectionType::FLOCK},
    {"FS Ext Battery", DetectionType::FLOCK},
};
const uint16_t kBtClassicCount = sizeof(kBtClassicNames) / sizeof(kBtClassicNames[0]);

// WiFi SSID prefix matches (case-insensitive).
const SsidEntry kSsidPrefixes[] = {
    {"AB2-",    "Axon-Body2",   DetectionType::AXON},
    {"AB3-",    "Axon-Body3",   DetectionType::AXON},
    {"AB4-",    "Axon-Body4",   DetectionType::AXON},
    {"AXON-",   "Axon-Field",   DetectionType::AXON},
    {"flock-",  "Flock-Setup",  DetectionType::FLOCK},
    {"FLOCK-",  "Flock-Setup",  DetectionType::FLOCK},
};
const uint16_t kSsidCount = sizeof(kSsidPrefixes) / sizeof(kSsidPrefixes[0]);

// 16-bit BLE manufacturer IDs.
const MfgIdEntry kMfgIdTable[] = {
    {0x004C, "Apple",      DetectionType::AIRTAG},    // AirTag / FindMy
    {0x09C8, "XUNTONG",    DetectionType::FLOCK},     // Flock BLE radio supplier
};
const uint16_t kMfgIdCount = sizeof(kMfgIdTable) / sizeof(kMfgIdTable[0]);

// --- lookups ---

DetectionType lookupOui(const uint8_t* mac) {
    if (!mac) return DetectionType::UNKNOWN;
    for (uint16_t i = 0; i < kOuiCount; i++) {
        if (mac[0] == kOuiTable[i].b[0] &&
            mac[1] == kOuiTable[i].b[1] &&
            mac[2] == kOuiTable[i].b[2]) {
            return kOuiTable[i].type;
        }
    }
    return DetectionType::UNKNOWN;
}

DetectionType lookupUuid(uint16_t uuid16) {
    for (uint16_t i = 0; i < kUuidCount; i++) {
        if (kUuidTable[i].uuid == uuid16) return kUuidTable[i].type;
    }
    return DetectionType::UNKNOWN;
}

DetectionType lookupBtName(const char* name) {
    if (!name) return DetectionType::UNKNOWN;
    for (uint16_t i = 0; i < kBtClassicCount; i++) {
        if (strcasecmp(name, kBtClassicNames[i].name) == 0) {
            return kBtClassicNames[i].type;
        }
    }
    // Substring matches for BLE advertised names
    if (strcasestr(name, "Flock"))    return DetectionType::FLOCK;
    if (strcasestr(name, "Penguin"))  return DetectionType::FLOCK;
    if (strcasestr(name, "Pigvision"))return DetectionType::FLOCK;
    if (strcasestr(name, "Axon"))     return DetectionType::AXON;
    return DetectionType::UNKNOWN;
}

DetectionType lookupSsid(const char* ssid) {
    if (!ssid) return DetectionType::UNKNOWN;
    for (uint16_t i = 0; i < kSsidCount; i++) {
        size_t n = strlen(kSsidPrefixes[i].prefix);
        if (strncasecmp(ssid, kSsidPrefixes[i].prefix, n) == 0) {
            return kSsidPrefixes[i].type;
        }
    }
    return DetectionType::UNKNOWN;
}

DetectionType lookupMfgId(uint16_t mfgId) {
    for (uint16_t i = 0; i < kMfgIdCount; i++) {
        if (kMfgIdTable[i].mfgId == mfgId) return kMfgIdTable[i].type;
    }
    return DetectionType::UNKNOWN;
}

bool isAirTagSubtype(const uint8_t* mfgPayload, uint8_t len) {
    if (!mfgPayload || len < 3) return false;
    uint8_t subtype = mfgPayload[2];
    return (subtype == 0x12 || subtype == 0x1E);
}

Confidence confidenceFor(DetectionType t) {
    // Per docs/DETECTIONS.md. FLOCK/AXON/META/SKIMMER/CAMERA are graded
    // High there for the signature path actually active in v1.0 (the
    // wildcard-probe and ESP32-generic-fallback ideas mentioned in that
    // doc as lower-confidence alternates aren't implemented — see the
    // note at the top of kOuiTable). RAVEN/AIRTAG/DRONE/ALPR are graded
    // Medium — unverified against real hardware, address rotation, or
    // thin OUI coverage, respectively.
    switch (t) {
        case DetectionType::FLOCK:
        case DetectionType::AXON:
        case DetectionType::META:
        case DetectionType::SKIMMER:
        case DetectionType::CAMERA:
            return Confidence::HIGH_CONF;
        case DetectionType::RAVEN:
        case DetectionType::AIRTAG:
        case DetectionType::DRONE:
        case DetectionType::ALPR:
            return Confidence::MED_CONF;
        default:
            return Confidence::LOW_CONF;
    }
}

const char* confidenceLabel(Confidence c) {
    switch (c) {
        case Confidence::HIGH_CONF:   return "HIGH CONF";
        case Confidence::MED_CONF: return "MED CONF";
        default:                 return "LOW CONF";
    }
}

uint8_t confidencePercent(Confidence c) {
    switch (c) {
        case Confidence::HIGH_CONF:   return 90;
        case Confidence::MED_CONF: return 60;
        default:                 return 30;
    }
}
