// SquachWatch-CYD — shared state types
// Standalone header; only <stdint.h> dependency.
#pragma once
#include <stdint.h>

enum class DetectionType : uint8_t {
    UNKNOWN = 0,
    FLOCK   = 1,   // Flock Safety camera / sensor
    AXON    = 2,   // Axon body camera / LE equipment
    META    = 3,   // Ray-Ban Meta smart glasses
    SKIMMER = 4,   // HC-05/06/03 Bluetooth skimmer
    RAVEN   = 5,   // Raven gunshot detector
    AIRTAG  = 6,   // Apple AirTag / FindMy
    DRONE   = 7,   // OpenDroneID drone
    ALPR    = 8,   // Motorola / Vigilant ALPR
    CAMERA  = 9,   // Generic camera (existing OUI list)
    SAMSUNG_TAG = 10,  // Samsung Galaxy SmartTag / SmartTag+
    GOOGLE_TAG  = 11,  // Google Find My Device network tracker (Chipolo/Pebblebee/Moto Tag)
    TILE    = 12,  // Tile BLE tracker (was previously bucketed under AIRTAG)
    RING    = 13,  // Ring doorbell/camera (was previously bucketed under CAMERA)
    DEAUTH  = 14,  // WiFi deauth/disassoc flood -- rate-detected, not a signature match (see DetectionEngine)
    EVILTWIN = 15, // One SSID beaconing from a second BSSID whose OUI differs -- rogue/spoofed AP (see DetectionEngine)
    IBEACON = 16,  // Apple iBeacon proximity beacon -- retail/venue tracking, not police kit
    // Pentest and wireless-audit hardware: Flipper Zero, Pwnagotchi, WiFi
    // Pineapple, ESP deauthers. One bucket rather than four types because
    // what matters to somebody reading the screen is that a tool for
    // attacking radios is in the room, not which model it is -- the
    // specific device goes in the vendor label and, where it announces
    // one, its own name goes in the name field.
    //
    // Deliberately NOT in this bucket: bare Espressif and other generic
    // silicon. A nyanBOX, an ESP32 Marauder and a SquachWatch are the same
    // chip, and sixteen Espressif prefixes already sit under FLOCK. HACKER
    // takes exact signatures only, which is what keeps it worth alerting on.
    HACKER  = 17,
    COUNT   = 18
};

inline const char* detectionTypeName(DetectionType t) {
    switch (t) {
        case DetectionType::FLOCK:       return "FLOCK";
        case DetectionType::AXON:        return "AXON";
        case DetectionType::META:        return "META";
        case DetectionType::SKIMMER:     return "SKIMMER";
        case DetectionType::RAVEN:       return "RAVEN";
        case DetectionType::AIRTAG:      return "AIRTAG";
        case DetectionType::DRONE:       return "DRONE";
        case DetectionType::ALPR:        return "ALPR";
        case DetectionType::CAMERA:      return "CAMERA";
        case DetectionType::SAMSUNG_TAG: return "SAMSUNG_TAG";
        case DetectionType::GOOGLE_TAG:  return "GOOGLE_TAG";
        case DetectionType::TILE:        return "TILE";
        case DetectionType::RING:        return "RING";
        case DetectionType::DEAUTH:      return "DEAUTH";
        case DetectionType::EVILTWIN:    return "EVIL TWIN";
        case DetectionType::IBEACON:     return "IBEACON";
        case DetectionType::HACKER:      return "HACKER";
        default:                         return "UNKNOWN";
    }
}

// How sure we are that a match is what it claims to be.
//
// Moved here from signatures.h because it is now a property of the
// SIGHTING rather than of the type. A FLOCK hit off Flock Safety's own
// registered OUI and a FLOCK hit off a generic Espressif module block are
// the same DetectionType and are not remotely the same claim.
//
// Note: plain LOW/MEDIUM/HIGH collide with the Arduino core's pin-state
// macros through the preprocessor, which enum class scoping does not
// protect against -- hence the _CONF suffix.
enum class Confidence : uint8_t { LOW_CONF, MED_CONF, HIGH_CONF };

struct Detection {
    uint8_t        mac[6];
    int8_t         rssi;
    uint8_t        channel;        // 0 if N/A
    DetectionType  type;
    char           vendor[12];
    char           name[20];
    uint32_t       firstSeen;
    uint32_t       lastSeen;
    uint16_t       hits;
    // The grade of the signature that actually matched, not the grade of
    // the type. See lookupOui().
    Confidence     conf;
    bool           active;
};

enum class AppState : uint8_t {
    BOOT     = 0,
    CLEAR    = 1,
    ALERT    = 2,
    LOG      = 3,
    SETTINGS = 4,
    DIARY    = 5,
    OUTFIT   = 6,
    RAWSCAN     = 7,  // manual BLE/WiFi scanner, reached via CLEAR's SCAN picker
    WATCH_ALERT = 8,  // a watched target (see DetectionEngine::watchBle/watchWifi) came back in range
    DIAGNOSTICS = 9,  // on-device diagnostics screen, reached via Settings
    HUNT        = 10, // live signal-strength gauge for the watched target,
                       // reached via raw-scan's long-press confirm panel
                       // (the "HUNT" choice alongside WATCH/CANCEL)
    COLOR_CHECK = 11, // first-boot RED/GREEN/BLUE display sanity check,
                       // also reachable later via Settings' "CHECK COLORS" row
    DETECTION_FILTER = 12, // per-type detection on/off, reached via
                            // Settings' "DETECTION FILTER" row
    OUTFIT_UNLOCK    = 13, // "OUTFIT UNLOCKED" celebration, pushed
                            // automatically whenever Squachy earns a new
                            // costume; returns to CLEAR when dismissed
    IGNORE_LIST      = 14, // muted devices, reached via Settings'
                            // "IGNORED DEVICES" row
    POWER_SAVER      = 15, // battery settings, reached via Settings'
                            // "POWER SAVER" row
    PHONE            = 16, // the payphone: type a name for Squachy. Reached
                            // from the SquachMesh menu. Only ever entered on
                            // a SquachMesh build -- the row is not offered
                            // otherwise -- but the state costs nothing.
    MESH_MENU        = 17, // SquachMesh's own screen: detect, transmit, name
    MESH_WARN        = 18  // the consent gate in front of it. Stands before
                            // the MENU rather than before the TRANSMIT row:
                            // a warning read next to a switch reads as an
                            // obstacle, one read before there is anything to
                            // click reads as information.
};

enum class ButtonId : uint8_t {
    NONE  = 255,
    SCAN  = 0,
    LOG   = 1,
    CLR   = 2
};
