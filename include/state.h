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
    // chip, which is why the Espressif prefixes came out of FLOCK. HACKER
    // takes exact signatures only, which is what keeps it worth alerting on.
    HACKER  = 17,
    // A LoRaWAN operator's network within earshot: a utility's meters, a
    // city's sensors, The Things Network. One per OPERATOR, not per device --
    // the DevAddr prefix names who runs it (lora_opalert.h), and a street of
    // meters is one fact, not three hundred. Only a board with a LoRa radio
    // can raise it, and only while that radio is listening on a LoRaWAN
    // profile.
    LORAWAN = 18,
    // MY ALERTS: a WiFi name matching one of the owner's own patterns (see
    // my_alerts.h). Kept LAST and outside everything that walks the catalogue
    // of known kit -- the DEX, BINGO and the TYPE FILTER stop short of it
    // (they loop to MINE, not COUNT): it is not a kind of device anybody
    // could collect, and its on/off switch is the list itself.
    MINE    = 19,
    // A smart speaker with microphones: Sonos, Apple's HomePod, and an Echo or
    // a Google speaker while it is in setup. The room check -- a hotel, a
    // rental -- next to the hidden cameras. Ships OFF, like IBEACON: a kitchen
    // counter is not a threat. AFTER MINE, not before it: MINE's number is
    // saved in LOG rows and IGNORE lists since v1.49.0, so it keeps 19, and the
    // lists that walk the catalogue use catalogueAt() below to skip MINE.
    LISTENER = 20,
    COUNT   = 21
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
        case DetectionType::LORAWAN:     return "LORAWAN";
        case DetectionType::MINE:        return "MY ALERT";
        case DetectionType::LISTENER:    return "LISTENER";
        default:                         return "UNKNOWN";
    }
}

// The catalogue of kit: every real type but MY ALERTS, which is the owner's
// own list rather than a kind of device. The DEX, BINGO and the TYPE FILTER
// walk this. MINE sits between LORAWAN and LISTENER in the numbering, so the
// walk steps over it instead of stopping at it.
static const uint8_t CATALOGUE_N = (uint8_t)DetectionType::COUNT - 2;
inline DetectionType catalogueAt(uint8_t i) {
    uint8_t v = (uint8_t)(i + 1);
    if (v >= (uint8_t)DetectionType::MINE) v++;
    return (DetectionType)v;
}
inline uint8_t catalogueIndex(DetectionType t) {
    const uint8_t v = (uint8_t)t;
    if (v == 0 || v == (uint8_t)DetectionType::MINE || v >= (uint8_t)DetectionType::COUNT) return CATALOGUE_N;
    return v < (uint8_t)DetectionType::MINE ? (uint8_t)(v - 1) : (uint8_t)(v - 2);
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
    // Kept from before this boot (the black box): firstSeen then holds the
    // wall-clock second it was seen, not a millis() stamp, and lastSeen is 0.
    // Cleared the moment the device is seen again. Sits in what was padding,
    // so the log costs no more RAM than it did.
    uint8_t        restored;
    // The signal a couple of seconds ago, and when that was in units of two
    // seconds (wrapping every eight and a half minutes, which is far longer
    // than a row stays fresh). The LOG's closer/further arrow reads these:
    // comparing against the last ADVERT would only show the wobble a still
    // device makes, which is why the raw scan's arrow samples too.
    int8_t         prevRssi;
    uint8_t        prevAt;
    // Who made it, when the match knew: a pointer into the signature
    // tables or a literal, never a copy. Every vendor string in this
    // firmware is a compile-time constant, so a log of two hundred rows
    // was carrying two hundred copies of "Apple" -- eight bytes a row
    // against a pointer, and the log is the largest thing the board
    // allocates. nullptr when nothing named it: read it through
    // vendorText() rather than testing it at every use.
    const char*    vendor;
    char           name[20];
    uint32_t       firstSeen;
    uint32_t       lastSeen;
    uint16_t       hits;
    // AUTO SNOOZE (Settings). `alerts` counts the full-screen alerts this
    // device has actually raised; `quietBar` is the strongest RSSI it ever
    // alerted at. Past the threshold it has to beat that bar by a margin to
    // interrupt again -- so the board stops repeating itself about the
    // doorbell across the street without ever going deaf to it coming
    // closer, which a plain mute would not manage.
    //
    // NOT `hits`: that counts every sighting on the WiFi path and only the
    // returns on the Bluetooth one, so it means two different things
    // depending on which radio found the device.
    uint8_t        alerts;
    int8_t         quietBar;
    // When it last spent one. NOT lastSeen: by the time the gate runs, the
    // reactivation path has already moved lastSeen to now, so "how long has
    // it been gone" measured from it is always zero and the allowance would
    // never be handed back.
    uint32_t       lastAlertMs;
    // The grade of the signature that actually matched, not the grade of
    // the type. See lookupOui().
    Confidence     conf;
    bool           active;
    // When it last asked to interrupt, allowed or not, in minutes of uptime
    // (wrapping after 45 days). A device that keeps coming back keeps asking,
    // so the gap since the last ask is how long it was really gone. Sits in
    // what was padding.
    uint16_t       askedMin;

    // ---- the trail (see trail.h) -------------------------------------
    // The last minute of signal: one reading every four seconds or so,
    // oldest first from sparkHead. The alert card draws it as a ladder.
    static const uint8_t SPARK_N = 12;
    int8_t         spark[SPARK_N];
    uint8_t        sparkHead;       // next slot to write
    uint8_t        sparkCount;      // readings held, <= SPARK_N
    uint8_t        sparkAt;         // millis() >> 12 of the last reading
    // The places (see places.h) this device was heard at, oldest first,
    // with the minutes of uptime it was first and last heard there. Three
    // is what the card draws; an older stop drops off the front.
    static const uint8_t STOPS_N = 3;
    uint8_t        stopPlace[STOPS_N];
    uint16_t       stopFirstMin[STOPS_N];
    uint16_t       stopLastMin[STOPS_N];
    uint8_t        stops;
    // Whether its owner is about: 0 unknown, 1 with its owner, 2 away from
    // its owner, as the tracker's own advert says (trackers only). ownerMin
    // is the minute of uptime the state was first seen.
    uint8_t        owner;
    uint16_t       ownerMin;
};

enum : uint8_t { OWNER_UNKNOWN = 0, OWNER_WITH = 1, OWNER_AWAY = 2 };

// A detection's vendor, safe to print. A record is zeroed before it is
// filled, so "no vendor" arrives here as a null pointer.
inline const char* vendorText(const Detection& d) { return d.vendor ? d.vendor : ""; }

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
    MESH_WARN        = 18, // the consent gate in front of it. Stands before
                            // the MENU rather than before the TRANSMIT row:
                            // a warning read next to a switch reads as an
                            // obstacle, one read before there is anything to
                            // click reads as information.
    MESH_PHRASE      = 19, // roll, show or enter the five-word phrase
    MESH_COMPOSE     = 20, // send a message; opened from CLEAR's little bubble
    BEACON_WARN      = 21, // what switching iBeacons on means, asked from
                            // DETECTION FILTER before it happens
    SECURITY         = 22, // the SECURITY submenu (PIN lock + the rest)
    LOCKED           = 23, // the lock screen: the payphone, digits only
    PIN_ENTRY        = 24, // setting, changing or checking a PIN, from SECURITY
    SQUAD            = 25, // every SquachWatch in range, and the inbox
    UPDATE           = 26, // UPDATE FIRMWARE, from Settings' SYSTEM page:
                            // WiFi or Bluetooth update, or switch slots
    WIFI_PASS        = 27, // typing a WiFi password, from UPDATE's network list
    STATUS_LIGHT     = 28, // the RGB LED's settings, from the APPEARANCE page
    NUDGE            = 29, // another board asked the squad to update: the countdown
    SQUAD_UPDATE     = 30, // UPDATE SQUAD, from the UPDATE FIRMWARE screen
    INVITE           = 31, // ADD TO SQUAD, either side of it
    DESK             = 32, // desk mode: the clock, the date, the focus timer
    WIFI_NETS        = 33, // WIFI NETWORKS, from the SYSTEM page: the saved list
    WIFI_ADD         = 34, // ...and the scan to add one from
    SYS_PROPS        = 35, // SYSTEM PROPERTIES: the window that says an update
                            // is out, in front of the main screen on the first
                            // frame after the intro. See ui_sysprops.h.
    // 36 was CROWD, a page holding one setting; the SquachMesh menu's CROWD
    // row steps it in place now.
    BINGO            = 36, // the detection bingo card, from Settings' BINGO
                            // row. See ui_bingo.h.
    DEX              = 37, // the SQUACHY-DEX, from Settings' row. See ui_dex.h.
    LORA_CHAT        = 38, // LORA CHATS, from WATCH SETTINGS (SQUACH_LORA builds). See ui_lorachat.h.
    LORA_HOME        = 39, // LORA, the T-Deck's radio landing page. See ui_lorahome.h.
    LORA_SETTINGS    = 40, // LORA SETTINGS, only from the LORA page. See ui_lorasettings.h.
    SENSING          = 41, // HUMAN SENSING (SENSE_CYD_APP builds): WiFi motion sensing on the CYD's own
                           // radio, from Settings > HUMAN SENSING. See ui_sensing_cyd.h and sense_cyd.h.
    MY_ALERTS        = 42, // MY ALERTS, from Settings: the owner's own WiFi names. See ui_myalerts.h.
    CRYPTID_GUIDE    = 43  // CRYPTID FIELD GUIDE, from Settings: Mothman's page. See ui_cryptid_guide.h.
};

enum class ButtonId : uint8_t {
    NONE  = 255,
    SCAN  = 0,
    LOG   = 1,
    CLR   = 2
};
