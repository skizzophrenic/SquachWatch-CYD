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
    {{0x00, 0x25, 0xDF}, "Axon",         DetectionType::AXON,       Confidence::HIGH_CONF},
    {{0xE4, 0x05, 0x40}, "Axon-Body",    DetectionType::AXON,       Confidence::LOW_CONF},
    {{0x28, 0x24, 0xFF}, "Axon-Signal",  DetectionType::AXON,       Confidence::LOW_CONF},

    // ---- Flock Safety (ESP32 modules + LTE backhaul) ----
    // Source: colonelpanichacks/flock-you, @NitekryDPaul, DeFlockJoplin
    {{0x24, 0x0A, 0xC4}, "Flock-ESP32",  DetectionType::FLOCK,      Confidence::LOW_CONF},
    {{0x30, 0xAE, 0xA4}, "Flock-ESP32",  DetectionType::FLOCK,      Confidence::LOW_CONF},
    {{0x24, 0x6F, 0x28}, "Flock-ESP32",  DetectionType::FLOCK,      Confidence::LOW_CONF},
    {{0xCC, 0x50, 0xE3}, "Flock-ESP32",  DetectionType::FLOCK,      Confidence::LOW_CONF},
    {{0xDC, 0x54, 0x75}, "Flock-ESP32",  DetectionType::FLOCK,      Confidence::LOW_CONF},
    {{0xE8, 0x9F, 0x6D}, "Flock-ESP32",  DetectionType::FLOCK,      Confidence::LOW_CONF},
    {{0x8C, 0xAA, 0xB5}, "Flok-ESP-S3", DetectionType::FLOCK,      Confidence::LOW_CONF},
    {{0x34, 0x85, 0x18}, "Flok-ESP-S3", DetectionType::FLOCK,      Confidence::LOW_CONF},
    {{0xB4, 0x1E, 0x52}, "Flock-MA-L",   DetectionType::FLOCK,      Confidence::HIGH_CONF},
    {{0xD4, 0xAD, 0xFC}, "Flock-ESP32",  DetectionType::FLOCK,      Confidence::LOW_CONF},
    {{0xAC, 0x67, 0xB2}, "Flock-ESP32",  DetectionType::FLOCK,      Confidence::LOW_CONF},
    {{0x84, 0xF3, 0xEB}, "Flok-ESP-S3", DetectionType::FLOCK,      Confidence::LOW_CONF},
    {{0xB4, 0xE6, 0x2D}, "Flock-ESP32",  DetectionType::FLOCK,      Confidence::LOW_CONF},
    {{0xCC, 0xDB, 0xA7}, "Flock-ESP32",  DetectionType::FLOCK,      Confidence::LOW_CONF},
    {{0x94, 0xB9, 0x7E}, "Flock-ESP32",  DetectionType::FLOCK,      Confidence::LOW_CONF},
    {{0xA4, 0xCF, 0x12}, "Flok-ESP-S2", DetectionType::FLOCK,      Confidence::LOW_CONF},
    {{0xC0, 0x49, 0xEF}, "Flok-ESP-C6", DetectionType::FLOCK,      Confidence::LOW_CONF},
    {{0x24, 0xB2, 0xB9}, "Flock-Liteo", DetectionType::FLOCK,      Confidence::LOW_CONF},
    {{0xD0, 0x39, 0x57}, "Flock",        DetectionType::FLOCK,      Confidence::LOW_CONF},
    {{0x00, 0xF4, 0x8D}, "Flock",        DetectionType::FLOCK,      Confidence::LOW_CONF},
    {{0x14, 0x5A, 0xFC}, "Flock",        DetectionType::FLOCK,      Confidence::LOW_CONF},
    {{0x80, 0x30, 0x49}, "Flock",        DetectionType::FLOCK,      Confidence::LOW_CONF},
    {{0xE0, 0x0A, 0xF6}, "Flock",        DetectionType::FLOCK,      Confidence::LOW_CONF},
    {{0x70, 0xC9, 0x4E}, "Flock",        DetectionType::FLOCK,      Confidence::LOW_CONF},
    {{0x3C, 0x91, 0x80}, "Flock",        DetectionType::FLOCK,      Confidence::LOW_CONF},
    {{0xD8, 0xF3, 0xBC}, "Flock",        DetectionType::FLOCK,      Confidence::LOW_CONF},
    {{0xB8, 0x35, 0x32}, "Flock",        DetectionType::FLOCK,      Confidence::LOW_CONF},
    {{0x82, 0x6B, 0xF2}, "Flock-DeFlk",  DetectionType::FLOCK,      Confidence::LOW_CONF},
    // Labelled as Sierra Wireless, the LTE modem in a Flock camera. The
    // registry says SPECTRA - TEK, which is neither Sierra nor Flock. The
    // prefix may still turn up on Flock hardware, so it stays -- as the
    // Low-confidence guess it always was.
    {{0x00, 0xA0, 0xD8}, "Flock-OEM",    DetectionType::FLOCK,      Confidence::LOW_CONF},

    // ---- ALPR and fixed surveillance camera vendors ----
    //
    // 00:0E:58 used to be the only entry here, labelled Vigilant. It is not
    // Vigilant. The IEEE registry gives that block to SONOS, INC. of Goleta
    // CA, assigned in February 2004, and it is still theirs -- so every
    // Sonos speaker in earshot was being logged as an ALPR camera. Removed
    // rather than corrected, because there is nothing to correct it to:
    // Motorola's own blocks are below.
    //
    // All seven of these were read out of the IEEE registry rather than
    // copied from another detector, after the one above turned out to be
    // wrong. Motorola Solutions absorbed Vigilant, so its blocks are the
    // nearest honest thing to the entry they replace.
    {{0x00, 0x04, 0x7D}, "ALPR-Mtrla",   DetectionType::ALPR,       Confidence::HIGH_CONF},
    {{0x00, 0x18, 0x85}, "ALPR-Mtrla",   DetectionType::ALPR,       Confidence::HIGH_CONF},
    {{0x00, 0x1F, 0x92}, "ALPR-Mtrla",   DetectionType::ALPR,       Confidence::HIGH_CONF},
    {{0x4C, 0xCC, 0x34}, "ALPR-Mtrla",   DetectionType::ALPR,       Confidence::HIGH_CONF},
    // Genetec's AutoVu is an LPR platform, so these sit with the ALPR set.
    {{0x00, 0xBF, 0x15}, "ALPR-Gentec",  DetectionType::ALPR,       Confidence::HIGH_CONF},
    {{0x0C, 0xBF, 0x15}, "ALPR-Gentec",  DetectionType::ALPR,       Confidence::HIGH_CONF},
    // Verkada sells LPR too but is mostly general-purpose surveillance, so
    // it is filed as a camera rather than overstated as a plate reader.

    // ---- Skimmer OUIs (BT Classic module prefixes) ----
    {{0x20, 0x13, 0x00}, "Skim-Linvor",  DetectionType::SKIMMER,    Confidence::LOW_CONF},
    {{0x98, 0xD3, 0x00}, "Skim-SPP",     DetectionType::SKIMMER,    Confidence::LOW_CONF},
    {{0x00, 0x1A, 0x7D}, "Skim-CSR",     DetectionType::SKIMMER,    Confidence::LOW_CONF},

    // ---- Specific camera vendors ----
    {{0x2C, 0xAA, 0x8E}, "Wyze",         DetectionType::CAMERA,     Confidence::HIGH_CONF},
    {{0xD0, 0x3F, 0x27}, "Wyze",         DetectionType::CAMERA,     Confidence::HIGH_CONF},
    {{0x7C, 0x78, 0xB2}, "Wyze",         DetectionType::CAMERA,     Confidence::HIGH_CONF},
    {{0xB8, 0xD7, 0xAF}, "Wyze-Mod",     DetectionType::CAMERA,     Confidence::LOW_CONF},
    {{0x34, 0xD2, 0x70}, "Amazon",       DetectionType::CAMERA,     Confidence::MED_CONF},
    // Labelled Hikvision for a long time. The registry says AMAZON
    // TECHNOLOGIES, which is a different company entirely -- and a much
    // broader one, hence Medium rather than High.
    {{0xF0, 0x27, 0x2D}, "Amazon",       DetectionType::CAMERA,     Confidence::MED_CONF},
    {{0xC0, 0x56, 0xE3}, "Hikvision",    DetectionType::CAMERA,     Confidence::HIGH_CONF},
    {{0x44, 0x19, 0xB6}, "Hikvision",    DetectionType::CAMERA,     Confidence::HIGH_CONF},
    // Labelled Reolink; registered to Hikvision. Both make cameras, so the
    // detection stood up while the attribution did not.
    {{0x28, 0x57, 0xBE}, "Hikvision",    DetectionType::CAMERA,     Confidence::HIGH_CONF},
    {{0x00, 0xE0, 0x4C}, "Realtek",      DetectionType::CAMERA,     Confidence::LOW_CONF},
    {{0xBC, 0xDD, 0xC2}, "Arlo",         DetectionType::CAMERA,     Confidence::LOW_CONF},
    {{0x4C, 0x69, 0x05}, "Blink",        DetectionType::CAMERA,     Confidence::LOW_CONF},
    {{0xA4, 0xC1, 0x38}, "Tuya",         DetectionType::CAMERA,     Confidence::LOW_CONF},

    // ---- Ring (own type, not generic CAMERA) ----
    // FC:65:DE and 68:37:E9 were already here under CAMERA; the rest
    // are Ring LLC's full registered MA-L block. Source: IEEE MA-L
    // registry, cross-checked via netify.ai and maclookup.app (both
    // list the same 13 prefixes for "Ring LLC", registered 2019-03-01).
    {{0xFC, 0x65, 0xDE}, "Ring",         DetectionType::RING,       Confidence::MED_CONF},
    {{0x68, 0x37, 0xE9}, "Ring",         DetectionType::RING,       Confidence::MED_CONF},
    {{0xAC, 0x9F, 0xC3}, "Ring",         DetectionType::RING,       Confidence::HIGH_CONF},
    {{0x18, 0x7F, 0x88}, "Ring",         DetectionType::RING,       Confidence::HIGH_CONF},
    {{0x34, 0x3E, 0xA4}, "Ring",         DetectionType::RING,       Confidence::HIGH_CONF},
    {{0x54, 0xE0, 0x19}, "Ring",         DetectionType::RING,       Confidence::HIGH_CONF},
    {{0x5C, 0x47, 0x5E}, "Ring",         DetectionType::RING,       Confidence::HIGH_CONF},
    {{0x64, 0x9A, 0x63}, "Ring",         DetectionType::RING,       Confidence::HIGH_CONF},
    {{0x90, 0x48, 0x6C}, "Ring",         DetectionType::RING,       Confidence::HIGH_CONF},
    {{0x9C, 0x76, 0x13}, "Ring",         DetectionType::RING,       Confidence::HIGH_CONF},
    {{0xCC, 0x3B, 0xFB}, "Ring",         DetectionType::RING,       Confidence::HIGH_CONF},
    {{0xC4, 0xDB, 0xAD}, "Ring",         DetectionType::RING,       Confidence::HIGH_CONF},
    {{0x24, 0x2B, 0xD6}, "Ring",         DetectionType::RING,       Confidence::HIGH_CONF},
    {{0x00, 0xB4, 0x63}, "Ring",         DetectionType::RING,       Confidence::HIGH_CONF},
    {{0x50, 0xE4, 0x67}, "Ring",         DetectionType::RING,       Confidence::HIGH_CONF},

    // ---- Commercial / institutional camera vendors ----
    // Source: public IEEE MA-L registry (maclookup.app), cross-checked
    // per-vendor registration records.
    {{0xE0, 0xA7, 0x00}, "Verkada",      DetectionType::CAMERA,     Confidence::HIGH_CONF},  // registered 2016-09-22
    {{0x70, 0x1A, 0xD5}, "Avigilon",     DetectionType::CAMERA,     Confidence::HIGH_CONF},  // Avigilon Alta, registered 2021-04-27
    {{0x00, 0x40, 0x8C}, "Axis",         DetectionType::CAMERA,     Confidence::HIGH_CONF},  // Axis Communications, registered 1998
    {{0xB8, 0xA4, 0x4F}, "Axis",         DetectionType::CAMERA,     Confidence::HIGH_CONF},

    // ---- Pentest hardware ------------------------------------------
    // Flipper Devices' own MA-L block, verified in the IEEE registry
    // (FLIPPER DEVICES INC, Claymont DE). This is the only Flipper prefix
    // that is actually registered to them.
    //
    // Two others circulate widely -- 80:E1:26 and 80:E1:27, copied from
    // detector to detector -- and neither appears in the IEEE registry at
    // all. That is how 00:0E:58 spent eleven releases here as "Vigilant"
    // while belonging to Sonos, so they are left out rather than shipped
    // at LOW. Flipper has three other signatures below; it does not need
    // a prefix nobody can source.
    {{0x0C,0xFA,0x22}, "Flipper",    DetectionType::HACKER, Confidence::HIGH_CONF},

    // Hak5's WiFi Pineapple. Hak5 hold NO IEEE registration -- the whole
    // 40,000-row registry has no entry for them -- because their gear is
    // OpenWRT on ODM boards. What they do use is these two locally
    // administered addresses, which is a real habit and a real hint, but
    // bit 1 of the first octet is set on both: by construction they
    // identify no vendor and anybody can set them. LOW, and the test
    // suite enforces that no locally-administered row is ever graded
    // higher.
    {{0x02,0xC0,0xCA}, "Hak5-LA",    DetectionType::HACKER, Confidence::LOW_CONF},
    {{0x02,0x13,0x37}, "Hak5-LA",    DetectionType::HACKER, Confidence::LOW_CONF},
};
const uint16_t kOuiCount = sizeof(kOuiTable) / sizeof(kOuiTable[0]);

// 16-bit BLE service UUIDs. Note: SPP 0x1101 is the *16-bit* form of
// 00001101-0000-1000-8000-00805F9B34FB. Caller extracts the 16-bit
// UUID from the advertised service data.
const UuidEntry kUuidTable[] = {
    {0x1101, "Skim-SPP",   DetectionType::SKIMMER},   // Classic SPP
    {0xFEED, "Tile",       DetectionType::TILE},      // Tile, Inc. — Bluetooth SIG assigned
    {0xFEEC, "Tile",       DetectionType::TILE},      // Tile, Inc. — second SIG-assigned UUID
    {0xFD5F, "Meta",       DetectionType::META},      // Ray-Ban Meta glasses
    {0x3100, "Raven",      DetectionType::RAVEN},     // Raven gunshot detector
    {0x3200, "Raven",      DetectionType::RAVEN},
    {0x3300, "Raven",      DetectionType::RAVEN},
    {0x3400, "Raven",      DetectionType::RAVEN},
    {0x3500, "Raven",      DetectionType::RAVEN},
    {0xFFFA, "DroneID",    DetectionType::DRONE},     // OpenDroneID
    {0xFD5A, "SmartTag",   DetectionType::SAMSUNG_TAG}, // Samsung's own SIG-assigned UUID for SmartTag discovery
    {0xFEAA, "FindMyDev",  DetectionType::GOOGLE_TAG},  // Google "Eddystone" service UUID, also used by the Find My Device network

    // Flipper Zero. It advertises one of three 16-bit service UUIDs, one
    // per case variant -- which is also how the desktop detectors tell a
    // white one from a black one. For our purposes the colour does not
    // matter; that there are exactly three fixed values does, because it
    // makes this an exact match rather than a guess.
    {0x3081, "Flipper",    DetectionType::HACKER},
    {0x3082, "Flipper",    DetectionType::HACKER},
    {0x3083, "Flipper",    DetectionType::HACKER},
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

    // Hak5 WiFi Pineapple management AP. The documented default is
    // Pineapple_XXXX, the X being the last four of its MAC. It is the
    // setup network, so it is up on a fresh unit and on one somebody has
    // not renamed -- which is most of them, and none of the careful ones.
    {"Pineapple_", "Pineapple",  DetectionType::HACKER},

    // The ESP8266/ESP32 deauther family (DSTIKE and the many clones) puts
    // up its own control AP, default SSID "pwned", password "deauther".
    // A prefix rather than an exact match because the forks vary the
    // suffix; MED at best, since somebody can also just name their home
    // network this as a joke.
    {"pwned",   "Deauther",     DetectionType::HACKER},
};
const uint16_t kSsidCount = sizeof(kSsidPrefixes) / sizeof(kSsidPrefixes[0]);

// 16-bit BLE manufacturer IDs.
const MfgIdEntry kMfgIdTable[] = {
    {0x004C, "Apple",      DetectionType::AIRTAG},    // AirTag / FindMy
    {0x09C8, "XUNTONG",    DetectionType::FLOCK},     // Flock BLE radio supplier

    // ---- Camera glasses -------------------------------------------------
    // Service UUID 0xFD5F caught Ray-Ban Meta and nothing else. These are
    // the Bluetooth SIG company IDs the dedicated glasses-spotting apps
    // actually key on, which is what widens this from one product to the
    // category.
    //
    // The catch, and the reason META is no longer graded High: Meta uses
    // these same company IDs across their other Bluetooth products, Quest
    // headsets included. A hit here means a Meta radio nearby, not
    // necessarily a camera pointed at you. The 0xFD5F match remains the
    // specific one.
    {0x01AB, "Meta",       DetectionType::META},      // Meta Platforms
    {0x058E, "Meta-Tech",  DetectionType::META},      // Meta Platforms Technologies
    {0x0D53, "Luxottica",  DetectionType::META},      // Ray-Ban's manufacturer
    {0x03C2, "Snap",       DetectionType::META},      // Snap Spectacles

    // ---- Pentest hardware -----------------------------------------------
    // Flipper Devices Inc., from the Bluetooth SIG company identifier list.
    //
    // Worth stating because the wrong value is widespread: ESP32 Marauder
    // comments its Flipper company ID as 0x0FBA, and every project that
    // copied that constant inherited it. 0x0FBA is Cosonic Intelligent
    // Technologies, who make headsets. Flipper is 0x0E29. Checked against
    // the SIG registry, the same way the OUI rows are checked against IEEE.
    {0x0E29, "Flipper",    DetectionType::HACKER},
};
const uint16_t kMfgIdCount = sizeof(kMfgIdTable) / sizeof(kMfgIdTable[0]);

// Apple's iBeacon, which is a fixed 25-byte manufacturer-data block:
//
//   4C 00   Apple's company ID, little endian
//   02      beacon type
//   15      remaining length, 21 bytes
//   ...     16-byte proximity UUID  (which deployment)
//   ...     2-byte major            (which site)
//   ...     2-byte minor            (which unit)
//   ...     1-byte measured power   (RSSI at one metre)
//
// Every one of those is fixed, so this is an exact match and not the kind
// of judgement call isAirTagPayload has to make. The length check is the
// whole test: 0x02 0x15 at that offset with 25 bytes behind it is an
// iBeacon, and an Apple device that is not one cannot accidentally look
// like one.
bool isIBeacon(const uint8_t* mfg, uint8_t len) {
    if (!mfg || len < 25) return false;
    if (mfg[0] != 0x4C || mfg[1] != 0x00) return false;   // Apple, little endian
    return mfg[2] == 0x02 && mfg[3] == 0x15;
}

// --- lookups ---

DetectionType lookupOui(const uint8_t* mac, Confidence* conf) {
    if (!mac) return DetectionType::UNKNOWN;
    for (uint16_t i = 0; i < kOuiCount; i++) {
        if (mac[0] == kOuiTable[i].b[0] &&
            mac[1] == kOuiTable[i].b[1] &&
            mac[2] == kOuiTable[i].b[2]) {
            if (conf) *conf = kOuiTable[i].conf;
            return kOuiTable[i].type;
        }
    }
    // Left alone rather than zeroed on a miss: the caller seeds it with the
    // type-level default before asking, so a non-OUI match keeps that.
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
    // A Flipper advertises "Flipper " followed by the unit's name. The
    // owner can change it, which is exactly why a name match is graded
    // down where it is used -- it is a string, not a signature. Left in
    // because the default is what most of them are still called, and it
    // costs nothing next to three exact signatures that cannot be typed.
    if (strcasestr(name, "Flipper"))  return DetectionType::HACKER;
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

const char* ssidVendorName(const char* ssid) {
    if (!ssid) return nullptr;
    for (uint16_t i = 0; i < kSsidCount; i++) {
        size_t n = strlen(kSsidPrefixes[i].prefix);
        if (strncasecmp(ssid, kSsidPrefixes[i].prefix, n) == 0) {
            return kSsidPrefixes[i].name;
        }
    }
    return nullptr;
}

DetectionType lookupMfgId(uint16_t mfgId) {
    for (uint16_t i = 0; i < kMfgIdCount; i++) {
        if (kMfgIdTable[i].mfgId == mfgId) return kMfgIdTable[i].type;
    }
    return DetectionType::UNKNOWN;
}

// Ported from nyanBOX's airtag_detector.cpp (jbohack, MIT) -- known
// good against a real tag, which the previous subtype-only check was
// not. Scans the whole raw advertisement for either of two sequences.
//
// PATTERN 1: 1E FF 4C 00
//   0x1E is an AD structure LENGTH byte (30), 0xFF is the AD type
//   "manufacturer specific", 4C 00 is Apple. So this matches any Apple
//   manufacturer advert that is exactly 30 bytes -- NOT a subtype test,
//   which is what the old code here mistook it for. That is why this
//   file used to accept a subtype of 0x1E: there is no such Apple
//   subtype, and that branch never once fired.
//
// PATTERN 2: 4C 00 12 19
//   Apple, type 0x12 (Find My), payload length 0x19. The beacon a tag
//   sends once separated from its owner.
//
// KNOWN COST, accepted deliberately. Pattern 1 also matches Apple's
// Proximity Pairing advert (4C 00 07 19 ...), which is likewise 30
// bytes -- so AirPods and similar accessories will report as AIRTAG
// again, which is the false positive v1.5.0 removed by dropping 0x07.
// It is back on purpose: a freshly powered AirTag advertises 0x07, not
// 0x12, so rejecting it meant the detector could not see a tag at the
// exact moment someone is most likely to be testing it. Catching the
// tag matters more than the accessory noise.
//
// The way to get both is to look further into the Proximity Pairing
// payload, which carries a device model ID that separates a tag from
// headphones. That needs a capture of real bytes from both to get
// right, and guessing model IDs from memory is how this gets subtly
// wrong again.
bool isAirTagPayload(const uint8_t* payload, uint8_t len) {
    if (!payload || len < 4) return false;
    for (uint8_t i = 0; i + 3 < len; i++) {
        if (payload[i] == 0x1E && payload[i + 1] == 0xFF &&
            payload[i + 2] == 0x4C && payload[i + 3] == 0x00) return true;
        if (payload[i] == 0x4C && payload[i + 1] == 0x00 &&
            payload[i + 2] == 0x12 && payload[i + 3] == 0x19) return true;
    }
    return false;
}

Confidence confidenceFor(DetectionType t) {
    // Per docs/DETECTIONS.md. FLOCK/AXON/META/SKIMMER/CAMERA are graded
    // High there for the signature path actually active in v1.0 (the
    // wildcard-probe and ESP32-generic-fallback ideas mentioned in that
    // doc as lower-confidence alternates aren't implemented — see the
    // note at the top of kOuiTable). RAVEN/AIRTAG/DRONE/ALPR are graded
    // Medium — unverified against real hardware, address rotation, or
    // thin OUI coverage, respectively. SAMSUNG_TAG is High: 0xFD5A is
    // Samsung's own dedicated SIG-assigned UUID, not shared with
    // anything else. GOOGLE_TAG is Medium: 0xFEAA is the general
    // "Eddystone" service UUID, also used by unrelated retail/asset
    // beacons, not exclusively Find My Device Network trackers. TILE
    // is High: 0xFEED/0xFEEC are both Bluetooth SIG-assigned exclusively
    // to Tile, Inc. RING is High: real MA-L registry OUI matches, same
    // evidentiary basis as CAMERA.
    switch (t) {
        case DetectionType::FLOCK:
        case DetectionType::AXON:
        case DetectionType::SKIMMER:
        case DetectionType::CAMERA:
        case DetectionType::SAMSUNG_TAG:
        case DetectionType::TILE:
        case DetectionType::RING:
        // Pattern-based rather than a signature, but a specific and
        // hard-to-fake one: two BSSIDs claiming one SSID from different
        // vendors while disagreeing about encryption. A mesh network --
        // the obvious false positive, and the thing that sank the
        // earlier vendor-only test -- never disagrees with itself about
        // security. High.
        // Exact: the whole iBeacon header is fixed by Apple's format and the
        // block is a fixed length, so a match cannot be a coincidence. What
        // it is NOT is a claim about intent -- see the docs.
        case DetectionType::IBEACON:
        case DetectionType::EVILTWIN:
            return Confidence::HIGH_CONF;
        // Was High when it was only Ray-Ban Meta's 0xFD5F service UUID, which
        // is specific to the glasses. Broadening it to the Meta, Luxottica and
        // Snap company IDs catches the rest of the category and costs that
        // precision: Meta puts the same IDs on Quest headsets. Medium is the
        // conservative grade the rule at the top of this function asks for.
        case DetectionType::META:
        case DetectionType::RAVEN:
        case DetectionType::AIRTAG:
        case DetectionType::DRONE:
        case DetectionType::ALPR:
        case DetectionType::GOOGLE_TAG:
        // Rate-thresholded (see DetectionEngine's deauth-flood
        // tracking), not a single-frame guess -- a real burst pattern,
        // but the threshold/window are still heuristic, so Medium
        // rather than High.
        case DetectionType::DEAUTH:
        // The BASE grade only, and it is the one the weakest members of
        // this bucket deserve: an SSID beginning "pwned" or a BLE name
        // beginning "Flipper" is a string anybody can type. The exact
        // signatures -- the three Flipper service UUIDs, Flipper's SIG
        // company ID, a Pwnagotchi announcing its own handshake count --
        // set HIGH explicitly at their match sites, and the OUI rows carry
        // their own. This is the value a caller gets when nothing more
        // specific was established.
        case DetectionType::HACKER:
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

// A pwnagotchi finds other pwnagotchis by stuffing a JSON blob into a
// vendor information element in its own beacon frames -- a private
// protocol riding inside a standard frame. It is not obfuscated in any
// way: the blob is plain ASCII and carries the unit's name, version,
// uptime, handshake count and whether deauth is switched on.
//
// So this does not parse JSON. Pulling in a parser to run inside the
// promiscuous callback would cost heap and time on every beacon in the
// air, and there are two things worth having: whether the blob is there,
// and the name. Both are byte scans.
//
// "pwnd_tot" is the key that makes this a pwnagotchi rather than any
// other JSON-in-a-beacon: it is the handshake counter, and nothing else
// advertises one. Requiring it means a beacon that merely contains a
// brace cannot match.
//
// Returns false and leaves `out` untouched unless both are found.
bool pwnagotchiName(const uint8_t* frame, uint32_t len,
                           char* out, size_t outSz) {
    if (!frame || !out || outSz < 2) return false;
    // 24-byte header plus the 12 fixed beacon parameters; the information
    // elements, and so anything a pwnagotchi added, start after that.
    const uint32_t start = 36;
    if (len <= start) return false;
    // Cap the scan. A beacon has no business being longer than this, and a
    // malformed sig_len must not walk this loop off the end of the buffer.
    if (len > 512) len = 512;
    const char* body = (const char*)frame;

    bool haveTot = false;
    static const char KEY_TOT[] = "pwnd_tot";
    const uint32_t totLen = sizeof(KEY_TOT) - 1;
    for (uint32_t i = start; i + totLen <= len; i++) {
        if (memcmp(body + i, KEY_TOT, totLen) == 0) { haveTot = true; break; }
    }
    if (!haveTot) return false;

    // Then the name, which is a quoted string value: "name":"foo". The
    // spacing varies between pwnagotchi versions, so this walks to the
    // colon and then to the opening quote rather than assuming a layout.
    static const char KEY_NAME[] = "\"name\"";
    const uint32_t nameLen = sizeof(KEY_NAME) - 1;
    for (uint32_t i = start; i + nameLen <= len; i++) {
        if (memcmp(body + i, KEY_NAME, nameLen) != 0) continue;
        uint32_t j = i + nameLen;
        while (j < len && (body[j] == ' ' || body[j] == ':')) j++;
        if (j >= len || body[j] != '"') return false;
        j++;
        size_t o = 0;
        while (j < len && body[j] != '"' && o + 1 < outSz) {
            // Printable ASCII only. This string is attacker-controlled and
            // lands in a field the log renders; a control character in it
            // would be somebody else deciding what our screen does.
            if (body[j] < 0x20 || body[j] > 0x7E) return false;
            out[o++] = body[j++];
        }
        out[o] = '\0';
        return o > 0;
    }
    return false;
}
