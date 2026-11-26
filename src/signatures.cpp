// SquachWatch-CYD — signature data tables and lookup functions
// Sources per docs/DETECTIONS.md.
#include "signatures.h"
#include <string.h>
#include <strings.h>   // strncasecmp

// OUI table. Order MATTERS: lookupOui walks top-to-bottom and returns
// the first match. Per DESIGN.md §6.2: Axon > Flock > ALPR > cameras >
// skimmer. A prefix shared by two makers goes under the more specific
// row first.
const OuiEntry kOuiTable[] = {
    // ---- Axon / Taser (priority: highest) ----
    {{0x00, 0x25, 0xDF}, "Axon",         DetectionType::AXON,       Confidence::HIGH_CONF},
    {{0xE4, 0x05, 0x40}, "Axon-Body",    DetectionType::AXON,       Confidence::LOW_CONF},
    {{0x28, 0x24, 0xFF}, "Axon-Signal",  DetectionType::AXON,       Confidence::LOW_CONF},

    // ---- Other body-camera makers (2026-10-14) ----
    // Registry-checked. Filed under AXON, which the screens call a body
    // camera; the label says whose. WatchGuard VIDEO (Motorola) -- not
    // WatchGuard Technologies, the firewall maker, whose 00:01:21 and
    // 00:90:7F would name every office firewall a police camera.
    {{0x00, 0x1D, 0x96}, "WatchGuard",   DetectionType::AXON,       Confidence::MED_CONF},
    {{0x00, 0x23, 0xBD}, "DigitalAlly",  DetectionType::AXON,       Confidence::MED_CONF},

    // ---- In-vehicle routers (2026-11-20) ----
    // The two makers whose rugged routers sit in most North American patrol
    // cars (Cradlepoint IBR/R-series, Sierra Wireless AirLink MG/MP/XR), on
    // their own IEEE blocks. Filed under AXON, the LE-equipment type, at
    // LOW with the maker as the label: buses, ambulances and utility trucks
    // carry the same boxes, so a hit means "a fleet vehicle's router", not
    // "police". Neither maker documents a default hotspot name for the
    // current models, so there is no SSID row.
    {{0x00, 0x30, 0x44}, "Cradlepoint",  DetectionType::AXON,       Confidence::LOW_CONF},
    {{0x00, 0xE0, 0x1C}, "Cradlepoint",  DetectionType::AXON,       Confidence::LOW_CONF},
    {{0x00, 0xA0, 0xD5}, "AirLink",      DetectionType::AXON,       Confidence::LOW_CONF},
    {{0x28, 0xA3, 0x31}, "AirLink",      DetectionType::AXON,       Confidence::LOW_CONF},
    {{0x50, 0x13, 0x9D}, "AirLink",      DetectionType::AXON,       Confidence::LOW_CONF},
    {{0x64, 0xCE, 0x6E}, "AirLink",      DetectionType::AXON,       Confidence::LOW_CONF},
    {{0x84, 0xDB, 0x2F}, "AirLink",      DetectionType::AXON,       Confidence::LOW_CONF},
    {{0xCC, 0x93, 0x4A}, "AirLink",      DetectionType::AXON,       Confidence::LOW_CONF},

    // ---- ShotSpotter / SoundThinking gunshot sensors ----
    // ShotSpotter, Inc.'s own block (IEEE MA-L). Filed with Raven, the other
    // gunshot sensor, because that is what it is.
    {{0xD4, 0x11, 0xD6}, "ShotSpotter",  DetectionType::RAVEN,      Confidence::HIGH_CONF},

    // ---- Flock Safety (ESP32 modules + LTE backhaul) ----
    // Source: colonelpanichacks/flock-you, @NitekryDPaul, DeFlockJoplin
    //
    // Seventeen Espressif blocks used to sit here as "Flock-ESP32". Espressif
    // makes the chip in every ESP32 -- this board, every other SquachWatch,
    // half the smart plugs in a house -- so they named all of those a Flock
    // camera, if only at LOW. Taken out 2026-10-14: the projects that test
    // at real Flock sites (all-cameras-are-beacons, Flock-You's own rewrite)
    // no longer trust an Espressif block, and Flock's newer cameras run a
    // Qualcomm radio (the QCA row below).
    {{0xB4, 0x1E, 0x52}, "Flock-MA-L",   DetectionType::FLOCK,      Confidence::HIGH_CONF},
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
    // From Flock-You (github.com/colonelpanichacks/flock-you, MIT), issue
    // #15: the community field list (@NitekryDPaul / DeFlockJoplin, 2026-07-16)
    // and a Flock camera firmware dump (2026-09-16). Every one is a module
    // vendor's block -- Liteon, USI, Silicon Labs, Atheros -- so LOW, like
    // the rows above. Left out: 48:27:EA, which the registry gives
    // to Samsung Electronics and would call every Samsung TV a Flock.
    {{0x74, 0x4C, 0xA1}, "Flock-Liteo", DetectionType::FLOCK,      Confidence::LOW_CONF},
    {{0x9C, 0x2F, 0x9D}, "Flock-Liteo", DetectionType::FLOCK,      Confidence::LOW_CONF},
    {{0xC0, 0x35, 0x32}, "Flock-Liteo", DetectionType::FLOCK,      Confidence::LOW_CONF},
    {{0x94, 0x08, 0x53}, "Flock-Liteo", DetectionType::FLOCK,      Confidence::LOW_CONF},
    {{0xE4, 0xAA, 0xEA}, "Flock-Liteo", DetectionType::FLOCK,      Confidence::LOW_CONF},
    {{0xF4, 0x6A, 0xDD}, "Flock-Liteo", DetectionType::FLOCK,      Confidence::LOW_CONF},
    {{0xE8, 0xD0, 0xFC}, "Flock-Liteo", DetectionType::FLOCK,      Confidence::LOW_CONF},
    {{0xB8, 0x1E, 0xA4}, "Flock-Liteo", DetectionType::FLOCK,      Confidence::LOW_CONF},
    {{0x70, 0x08, 0x94}, "Flock-Liteo", DetectionType::FLOCK,      Confidence::LOW_CONF},
    {{0x58, 0x00, 0xE3}, "Flock-Liteo", DetectionType::FLOCK,      Confidence::LOW_CONF},
    {{0x5C, 0x93, 0xA2}, "Flock-Liteo", DetectionType::FLOCK,      Confidence::LOW_CONF},
    {{0x64, 0x6E, 0x69}, "Flock-Liteo", DetectionType::FLOCK,      Confidence::LOW_CONF},
    {{0x14, 0xB5, 0xCD}, "Flock-Liteo", DetectionType::FLOCK,      Confidence::LOW_CONF},
    {{0x08, 0x3A, 0x88}, "Flock-UGSI",  DetectionType::FLOCK,      Confidence::LOW_CONF},
    {{0xE0, 0x4F, 0x43}, "Flock-UGSI",  DetectionType::FLOCK,      Confidence::LOW_CONF},
    {{0x58, 0x8E, 0x81}, "Flock-SiLab", DetectionType::FLOCK,      Confidence::LOW_CONF},
    {{0xEC, 0x1B, 0xBD}, "Flock-SiLab", DetectionType::FLOCK,      Confidence::LOW_CONF},
    {{0x90, 0x35, 0xEA}, "Flock-SiLab", DetectionType::FLOCK,      Confidence::LOW_CONF},
    // Atheros's own default: the camera's QCA9377 ships 00:03:7F:50:00:01
    // in its calibration blobs, and a unit nobody reprogrammed keeps it.
    {{0x00, 0x03, 0x7F}, "Flock-QCA",     DetectionType::FLOCK,      Confidence::LOW_CONF},
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
    // Registered to Motorola Solutions Malaysia Sdn. Bhd. rather than to the
    // US parent, which is why it was missing from the sweep that found the
    // four above -- same company, different registry line.
    {{0xB8, 0xE2, 0x8C}, "ALPR-Mtrla",   DetectionType::ALPR,       Confidence::HIGH_CONF},
    // Genetec's AutoVu is an LPR platform, so these sit with the ALPR set.
    {{0x00, 0xBF, 0x15}, "ALPR-Gentec",  DetectionType::ALPR,       Confidence::HIGH_CONF},
    {{0x0C, 0xBF, 0x15}, "ALPR-Gentec",  DetectionType::ALPR,       Confidence::HIGH_CONF},
    // Verkada sells LPR too but is mostly general-purpose surveillance, so
    // it is filed as a camera rather than overstated as a plate reader.

    // ---- Skimmer OUIs (BT Classic module prefixes) ----
    {{0x20, 0x13, 0x00}, "Skim-Linvor",  DetectionType::SKIMMER,    Confidence::LOW_CONF},
    // Was 98:D3:00, which nobody holds. The HC-05/HC-06 modules' maker,
    // Shenzhen Bolutek, holds 98:D3:31 (IEEE MA-L).
    {{0x98, 0xD3, 0x31}, "Skim-HC05",    DetectionType::SKIMMER,    Confidence::LOW_CONF},
    {{0x00, 0x1A, 0x7D}, "Skim-CSR",     DetectionType::SKIMMER,    Confidence::LOW_CONF},

    // ---- Specific camera vendors ----
    {{0x2C, 0xAA, 0x8E}, "Wyze",         DetectionType::CAMERA,     Confidence::HIGH_CONF},
    {{0xD0, 0x3F, 0x27}, "Wyze",         DetectionType::CAMERA,     Confidence::HIGH_CONF},
    {{0x7C, 0x78, 0xB2}, "Wyze",         DetectionType::CAMERA,     Confidence::HIGH_CONF},
    // Wyze's two newer blocks (IEEE MA-L, read 2026-11-20): without them a
    // recent Wyze camera matched nothing at all.
    {{0xF0, 0xC8, 0x8B}, "Wyze",         DetectionType::CAMERA,     Confidence::HIGH_CONF},
    {{0x80, 0x48, 0x2C}, "Wyze",         DetectionType::CAMERA,     Confidence::HIGH_CONF},
    // SimpliSafe's one block. MED, not HIGH: it is their cameras and their
    // doorbell, and their base station and keypads as well.
    {{0xF8, 0x51, 0x28}, "SimpliSafe",   DetectionType::CAMERA,     Confidence::MED_CONF},
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
    // Arlo's own MA-L blocks, all three registered to "Arlo Technology",
    // so High by the rule the other vendor-registered camera rows use.
    {{0xFC, 0x9C, 0x98}, "Arlo",         DetectionType::CAMERA,     Confidence::HIGH_CONF},
    {{0x48, 0x62, 0x64}, "Arlo",         DetectionType::CAMERA,     Confidence::HIGH_CONF},
    {{0xA4, 0x11, 0x62}, "Arlo",         DetectionType::CAMERA,     Confidence::HIGH_CONF},
    // BC:DD:C2 sat here as "Arlo" and is registered to ESPRESSIF. It was
    // wrong in both directions: every ESP32 in that block read as a covert
    // camera, and no real Arlo was ever matched, because none of the three
    // blocks above were in the table. It also contradicted this project's
    // own documented decision -- DETECTIONS.md, under the camera section,
    // says a "flag any Espressif OUI as a possible camera" fallback "would
    // need a real audit of Espressif's OUI ranges against known false
    // positives before shipping -- it is NOT built". One was in the table
    // anyway, mislabelled, so the audit graded it Low as "Arlo" instead of
    // removing it as Espressif. Same hazard the FLOCK block was cleared of
    // on 2026-10-14 ("so a SquachWatch is never somebody else's Flock
    // camera"); a SquachWatch is an ESP32, and this is a SquachWatch
    // reading another SquachWatch as surveillance.
    {{0x4C, 0x69, 0x05}, "Blink",        DetectionType::CAMERA,     Confidence::LOW_CONF},
    {{0xA4, 0xC1, 0x38}, "Tuya",         DetectionType::CAMERA,     Confidence::LOW_CONF},
    // Blink by Amazon's own blocks (IEEE MA-L, "Blink by Amazon").
    {{0x3C, 0xA0, 0x70}, "Blink",        DetectionType::CAMERA,     Confidence::MED_CONF},
    {{0x70, 0xAD, 0x43}, "Blink",        DetectionType::CAMERA,     Confidence::MED_CONF},
    {{0x74, 0x13, 0x48}, "Blink",        DetectionType::CAMERA,     Confidence::MED_CONF},
    {{0x74, 0xAB, 0x93}, "Blink",        DetectionType::CAMERA,     Confidence::MED_CONF},
    {{0xC8, 0x19, 0xD8}, "Blink",        DetectionType::CAMERA,     Confidence::MED_CONF},
    {{0xF0, 0x74, 0xC1}, "Blink",        DetectionType::CAMERA,     Confidence::MED_CONF},
    // i-PRO (Panasonic's security cameras, body cams among them).
    {{0xD4, 0x2D, 0xC5}, "i-PRO",        DetectionType::CAMERA,     Confidence::HIGH_CONF},

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

    // ---- Dashcams and fleet driver cams (2026-11-20) ----
    // Registry-checked (IEEE MA-L). Only the brands with their own block:
    // most consumer dashcams ride a Realtek or Ampak module and are caught
    // by their hotspot name instead (kSsidPrefixes). The fleet systems
    // (Lytx DriveCam, Samsara) are the cameras in delivery vans and trucks
    // that watch the road and the driver; Samsara's block also covers its
    // plain vehicle gateways, hence LOW. Netradyne and Motive hold only
    // 28-bit blocks, which this table cannot express yet.
    {{0x00, 0x25, 0x42}, "BlackVue",     DetectionType::CAMERA,     Confidence::HIGH_CONF},
    {{0x44, 0x27, 0xF3}, "70mai",        DetectionType::CAMERA,     Confidence::HIGH_CONF},
    {{0xB8, 0x59, 0xC8}, "70mai",        DetectionType::CAMERA,     Confidence::HIGH_CONF},
    {{0x00, 0x1F, 0x4F}, "Thinkware",    DetectionType::CAMERA,     Confidence::HIGH_CONF},
    {{0x00, 0x16, 0xB2}, "Lytx",         DetectionType::CAMERA,     Confidence::MED_CONF},   // registered as DriveCam Inc, Lytx's old name
    {{0x2C, 0x42, 0x05}, "Lytx",         DetectionType::CAMERA,     Confidence::MED_CONF},
    {{0x50, 0xDF, 0x95}, "Lytx",         DetectionType::CAMERA,     Confidence::MED_CONF},
    {{0x70, 0xE4, 0x6E}, "Lytx",         DetectionType::CAMERA,     Confidence::MED_CONF},
    {{0x58, 0xA7, 0x48}, "Lytx",         DetectionType::CAMERA,     Confidence::MED_CONF},
    {{0xFC, 0xDB, 0x21}, "Samsara",      DetectionType::CAMERA,     Confidence::LOW_CONF},
    {{0x28, 0xEA, 0x5B}, "Samsara",      DetectionType::CAMERA,     Confidence::LOW_CONF},

    // ---- Camera glasses ----
    // Luxottica Group's own block (IEEE MA-L): Ray-Ban and Oakley Meta's
    // maker. Four other "Meta glasses" OUIs circulate (7C:2A:9E, CC:66:0A,
    // F4:03:43, 5C:E9:1E); the registry gives two to Apple, one to HPE and
    // one to nobody, so they are not here.
    {{0x98, 0x59, 0x49}, "Luxottica",    DetectionType::META,       Confidence::MED_CONF},
    // Two more camera-glasses makers with their own blocks (2026-11-20).
    // Rokid's block is registered to Lingban Technology (Hangzhou), the
    // company's legal name -- a press-and-wiki link, not Rokid's own
    // site, so LOW. Vuzix makes the Blade and M400 (cameras) and the Z100
    // (none); one block covers all of them, so LOW too.
    {{0x28, 0xE8, 0x88}, "Rokid",        DetectionType::META,       Confidence::LOW_CONF},
    {{0x98, 0xDA, 0x92}, "Vuzix",        DetectionType::META,       Confidence::LOW_CONF},

    // ---- Drones, by maker (2026-10-14) ----
    // For the drones that do not broadcast Remote ID, or not yet. All IEEE
    // MA-L. DJI's main blocks are its drones and controllers; the Baiwang
    // blocks are a DJI subsidiary with a wider range, so LOW. Parrot's two
    // oldest blocks are mostly its car kits, so LOW too. Left out: DJI's
    // Ronin and Osmo blocks (camera gimbals), and Autel's (an MA-M, finer
    // than this table matches).
    {{0x60, 0x60, 0x1F}, "DJI",          DetectionType::DRONE,      Confidence::MED_CONF},
    {{0x34, 0xD2, 0x62}, "DJI",          DetectionType::DRONE,      Confidence::MED_CONF},
    {{0x48, 0x1C, 0xB9}, "DJI",          DetectionType::DRONE,      Confidence::MED_CONF},
    {{0xE4, 0x7A, 0x2C}, "DJI",          DetectionType::DRONE,      Confidence::MED_CONF},
    {{0x58, 0xB8, 0x58}, "DJI",          DetectionType::DRONE,      Confidence::MED_CONF},
    {{0x04, 0xA8, 0x5A}, "DJI",          DetectionType::DRONE,      Confidence::MED_CONF},
    {{0x8C, 0x58, 0x23}, "DJI",          DetectionType::DRONE,      Confidence::MED_CONF},
    {{0x0C, 0x9A, 0xE6}, "DJI",          DetectionType::DRONE,      Confidence::MED_CONF},
    {{0x88, 0x29, 0x85}, "DJI",          DetectionType::DRONE,      Confidence::MED_CONF},   // registered 2025-10
    {{0x4C, 0x43, 0xF6}, "DJI",          DetectionType::DRONE,      Confidence::MED_CONF},   // registered 2025-12
    {{0x9C, 0x5A, 0x8A}, "DJI-BW",       DetectionType::DRONE,      Confidence::LOW_CONF},
    {{0xEC, 0x72, 0xF7}, "DJI-BW",       DetectionType::DRONE,      Confidence::LOW_CONF},
    {{0x34, 0x91, 0xF0}, "DJI-BW",       DetectionType::DRONE,      Confidence::LOW_CONF},
    {{0xC8, 0xA1, 0x62}, "DJI-BW",       DetectionType::DRONE,      Confidence::LOW_CONF},
    {{0x38, 0x1D, 0x14}, "Skydio",       DetectionType::DRONE,      Confidence::HIGH_CONF},
    {{0x90, 0x03, 0xB7}, "Parrot",       DetectionType::DRONE,      Confidence::MED_CONF},
    {{0x90, 0x3A, 0xE6}, "Parrot",       DetectionType::DRONE,      Confidence::MED_CONF},
    {{0xA0, 0x14, 0x3D}, "Parrot",       DetectionType::DRONE,      Confidence::MED_CONF},
    {{0x00, 0x12, 0x1C}, "Parrot",       DetectionType::DRONE,      Confidence::LOW_CONF},
    {{0x00, 0x26, 0x7E}, "Parrot",       DetectionType::DRONE,      Confidence::LOW_CONF},

    // ---- Pentest hardware ------------------------------------------
    // No Flipper prefix here. Flipper Devices' own IEEE block, 0C:FA:22,
    // sat here as HACKER until 2026-10-14, and this table is only ever
    // read for WiFi frames -- a radio a Flipper Zero does not have. What
    // it caught was the BUSY Bar, Flipper's desk sign, whose WiFi uses
    // that block (0c:fa:22:00:41:54 on the bench, beside three real
    // Zeros). The Zeros themselves advertise from 80:E1:26, which is in
    // nobody's IEEE registration, so it stays out as well; their three
    // service UUIDs below catch every one of them.

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

    // ---- LISTENER: smart speakers -------------------------------------------
    // Sonos, Inc.'s fifteen MA-L blocks (IEEE registry, 2026-11-21). Nothing
    // but Sonos and IKEA's Symfonisk line ships on them. MED, not HIGH: the
    // maker is certain, the microphone is not -- One SL, Five, Sub, Amp,
    // Port, Play:1/3, Playbar and Symfonisk have none. 00:04:3C is "SONOS Co.
    // Ltd", a different (Korean) company, and stays out. Amazon's 205 blocks
    // and Google's 114 are every Kindle, Fire TV, Pixel and Chromecast too,
    // so an Echo or a Nest speaker has no prefix of its own to match.
    {{0x00,0x0E,0x58}, "Sonos",      DetectionType::LISTENER, Confidence::MED_CONF},
    {{0x34,0x7E,0x5C}, "Sonos",      DetectionType::LISTENER, Confidence::MED_CONF},
    {{0x38,0x42,0x0B}, "Sonos",      DetectionType::LISTENER, Confidence::MED_CONF},
    {{0x48,0xA6,0xB8}, "Sonos",      DetectionType::LISTENER, Confidence::MED_CONF},
    {{0x54,0x2A,0x1B}, "Sonos",      DetectionType::LISTENER, Confidence::MED_CONF},
    {{0x5C,0xAA,0xFD}, "Sonos",      DetectionType::LISTENER, Confidence::MED_CONF},
    {{0x60,0xF6,0x20}, "Sonos",      DetectionType::LISTENER, Confidence::MED_CONF},
    {{0x74,0xCA,0x60}, "Sonos",      DetectionType::LISTENER, Confidence::MED_CONF},
    {{0x78,0x28,0xCA}, "Sonos",      DetectionType::LISTENER, Confidence::MED_CONF},
    {{0x80,0x4A,0xF2}, "Sonos",      DetectionType::LISTENER, Confidence::MED_CONF},
    {{0x94,0x9F,0x3E}, "Sonos",      DetectionType::LISTENER, Confidence::MED_CONF},
    {{0xB8,0xE9,0x37}, "Sonos",      DetectionType::LISTENER, Confidence::MED_CONF},
    {{0xC4,0x38,0x75}, "Sonos",      DetectionType::LISTENER, Confidence::MED_CONF},
    {{0xF0,0xF6,0xC1}, "Sonos",      DetectionType::LISTENER, Confidence::MED_CONF},
    {{0xF8,0x5C,0x24}, "Sonos",      DetectionType::LISTENER, Confidence::MED_CONF},
};
const uint16_t kOuiCount = sizeof(kOuiTable) / sizeof(kOuiTable[0]);

// 16-bit BLE service UUIDs. Note: SPP 0x1101 is the *16-bit* form of
// 00001101-0000-1000-8000-00805F9B34FB. Caller extracts the 16-bit
// UUID from the advertised service data.
const UuidEntry kUuidTable[] = {
    {0x1101, "Skim-SPP",   DetectionType::SKIMMER},   // Classic SPP
    {0xFEED, "Tile",       DetectionType::TILE},      // Tile, Inc. — Bluetooth SIG assigned
    {0xFEEC, "Tile",       DetectionType::TILE},      // Tile, Inc. — second SIG-assigned UUID
    {0xFD84, "Tile",       DetectionType::TILE},      // Tile, Inc. — third SIG-assigned UUID
    // Axon / TASER International's SIG-assigned UUIDs (OUI-SPY AXEOFF,
    // 2026-10-09; FE6B also seen on Axon gear in all-cameras-are-beacons'
    // captures).
    {0xFC81, "Axon",       DetectionType::AXON},
    {0xFE6B, "Axon",       DetectionType::AXON},
    {0xFE6C, "Axon",       DetectionType::AXON},
    // More glasses: Meta Platforms' other two UUIDs, Snap's.
    {0xFEB7, "Meta",       DetectionType::META},
    {0xFEB8, "Meta",       DetectionType::META},
    {0xFE45, "Snap",       DetectionType::META},
    // DULT, the cross-maker "unwanted tracker" standard: a tag away from its
    // owner announces this whoever made it. Filed under GOOGLE_TAG, which
    // the screens call TRACKER, as is Chipolo.
    {0xFCB2, "Tracker",    DetectionType::GOOGLE_TAG},
    {0xFE33, "Chipolo",    DetectionType::GOOGLE_TAG},
    {0xFE65, "Chipolo",    DetectionType::GOOGLE_TAG},
    // Its own label, not "Meta": this UUID is the one signature specific to
    // Ray-Ban Meta, and the company-ID rows below say Meta too -- which is
    // any Meta radio, Quest headsets included. Two different pages.
    {0xFD5F, "RayBanMeta", DetectionType::META},      // Ray-Ban Meta glasses
    {0x3100, "Raven",      DetectionType::RAVEN},     // Raven gunshot detector
    {0x3200, "Raven",      DetectionType::RAVEN},
    {0x3300, "Raven",      DetectionType::RAVEN},
    {0x3400, "Raven",      DetectionType::RAVEN},
    {0x3500, "Raven",      DetectionType::RAVEN},
    {0xFFFA, "DroneID",    DetectionType::DRONE},     // OpenDroneID
    {0xFD5A, "SmartTag",   DetectionType::SAMSUNG_TAG}, // Samsung's own SIG-assigned UUID for SmartTag discovery
    // Google's Find Hub network (FMDN). The UUID is Eddystone's too -- every
    // shop beacon -- so it only counts as service data whose frame type is
    // FMDN's 0x40 or 0x41 (see fmdnFrame()); the UUID list never reports it.
    {0xFEAA, "FindHub",    DetectionType::GOOGLE_TAG},

    // Flipper Zero. It advertises one of three 16-bit service UUIDs, one
    // per case variant -- which is also how the desktop detectors tell a
    // white one from a black one. For our purposes the colour does not
    // matter; that there are exactly three fixed values does, because it
    // makes this an exact match rather than a guess.
    {0x3081, "Flipper",    DetectionType::HACKER},
    {0x3082, "Flipper",    DetectionType::HACKER},
    {0x3083, "Flipper",    DetectionType::HACKER},

    // Sonos's SIG-assigned service UUID (ESPresense keys Sonos on it too).
    {0xFE07, "Sonos",      DetectionType::LISTENER},
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
    // A confirmed Flock hotspot's network, "Flock Camera net." (Flock-You
    // issue #43). The bare "Flock" and "flock-" above both miss it.
    {"Flock Camera", "Flock-Cam", DetectionType::FLOCK},

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

    // ---- Dashcams (2026-11-20) ----
    // The hotspot a dashcam puts up for its phone app, named as the maker's
    // manual says. Most ride a generic WiFi module, so the name is the only
    // handle. Vantrue puts its name in the MIDDLE ("E1 Lite_VANTRUE_1234"):
    // that one is a substring rule in lookupSsid(), not a row here. Garmin
    // and Rexing document no name and have none. Nextbase's is the older
    // series, with a space ("NEXTBASE 312GW 123"); the newer ones publish
    // no pattern. The Lytx aux camera's "AuxCamera_" is from one FCC label.
    {"VIOFO_",     "Viofo",     DetectionType::CAMERA},
    {"VIOFO-",     "Viofo",     DetectionType::CAMERA},
    {"BlackVue",   "BlackVue",  DetectionType::CAMERA},
    {"70mai_",     "70mai",     DetectionType::CAMERA},
    {"thinkware",  "Thinkware", DetectionType::CAMERA},
    {"NEXTBASE ",  "Nextbase",  DetectionType::CAMERA},
    {"ROVE_R",     "Rove",      DetectionType::CAMERA},
    {"REDTIGER_",  "Redtiger",  DetectionType::CAMERA},
    {"AuxCamera_", "Lytx",      DetectionType::CAMERA},

    // ---- Hidden and mini cameras in setup mode (2026-11-20) ----
    // The cheap WiFi cameras that turn up in rentals sit in access-point
    // mode until paired, under a fixed name from their app's firmware.
    // The biggest family (LookCam, HDWifiCamPro, JXLCAM, TinyCam Pro)
    // names the hotspot after the camera's own UID, "ABCD-123456-ABCDE":
    // that shape is a rule in lookupSsid() (isP2pCamSsid), and the rows
    // here only put the right family on the label. V380's "MV" + digits
    // is a rule too. Rejected as too generic: "CAM-", "A9", "HD-",
    // "Camera-", "IPCAM-", Tuya's "SmartLife-" (every unpaired plug).
    {"DGK-",       "HDWifiCam", DetectionType::CAMERA},
    {"ACCQ-",      "LookCam",   DetectionType::CAMERA},
    {"BCCQ-",      "LookCam",   DetectionType::CAMERA},
    {"XCCQ-",      "LookCam",   DetectionType::CAMERA},
    {"HCAM-",      "TinyCam",   DetectionType::CAMERA},
    {"CLOUDCAM_",  "YCC365",    DetectionType::CAMERA},
    {"MEYE_",      "Sricam",    DetectionType::CAMERA},
    {"GW_IPC",     "Yoosee",    DetectionType::CAMERA},
    {"IPC_AP_",    "Zosi",      DetectionType::CAMERA},

    // ---- LISTENER: a speaker in setup mode ------------------------------
    // A Google Home or Nest speaker being set up names its hotspot
    // "GoogleHome" and four digits (Google's own setup help). Once it is
    // on the room's WiFi this goes away: it is a client then, not a hotspot.
    // Amazon's "Amazon-XXXX" is left out: other Amazon gear uses it too.
    {"GoogleHome", "Google speaker", DetectionType::LISTENER},
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
    // 0x058E (Meta Platforms Technologies) used to be here. It is what the
    // Quest headsets send, and no glasses capture has shown it; the real
    // Ray-Ban Meta sends 0x01AB with 0xFD5F (two independent captures,
    // 2026-07 and 2026-10). Taken out 2026-10-14.
    {0x0D53, "Luxottica",  DetectionType::META},      // Ray-Ban's manufacturer
    {0x03C2, "Snap",       DetectionType::META},      // Snap Spectacles
    {0x060C, "Vuzix",      DetectionType::META},      // Vuzix smart glasses

    // ---- Body cameras ----------------------------------------------------
    // TASER International, now Axon (SIG). OUI-SPY's AXEOFF keys on it.
    {0x034D, "Axon",       DetectionType::AXON},

    // ---- Trackers ----------------------------------------------------------
    {0x08C3, "Chipolo",    DetectionType::GOOGLE_TAG},

    // ---- Smart speakers ------------------------------------------------------
    // Sonos, Inc. (SIG). Every Sonos product, the portable Roam and Move with
    // their microphones among them.
    {0x05A7, "Sonos",      DetectionType::LISTENER},

    // ---- Pentest hardware -----------------------------------------------
    // None. Flipper Devices' SIG company ID is 0x0E29 (not 0x0FBA, which
    // ESP32 Marauder comments it as: that is Cosonic, who make headsets),
    // and it sat here as HACKER until 2026-10-14. A Flipper Zero never sends
    // it -- its firmware's serial profile advertises only a service UUID and
    // a name, and three Zeros on the bench agreed -- while the BUSY Bar, a
    // desk sign, puts it in every advert until it is paired (its firmware's
    // ble_advertise.c). So the only thing the row ever caught was the sign.
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

bool isBareSerialName(const char* name) {
    if (!name) return false;
    for (int i = 0; i < 10; i++) if (name[i] < '0' || name[i] > '9') return false;
    return name[10] == '\0';
}

// "FS-" and a hex serial, e.g. FS-BEC46A: a Flock accessory, seen at real
// Flock sites in 2026-06 (all-cameras-are-beacons). At least four hex digits
// and nothing else, so "FS-Living Room" is not one.
bool isFlockFsName(const char* name) {
    if (!name || strncmp(name, "FS-", 3) != 0) return false;
    int n = 0;
    for (const char* p = name + 3; *p; p++, n++) {
        const char c = *p;
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'))) return false;
    }
    return n >= 4 && n <= 12;
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
    // A Penguin battery pack on its other firmware names itself by its bare
    // ten-digit serial (Flock-You). Graded LOW where it is used.
    if (isBareSerialName(name))       return DetectionType::FLOCK;
    if (isFlockFsName(name))          return DetectionType::FLOCK;
    if (strcasestr(name, "Axon"))     return DetectionType::AXON;
    // Utility Inc.'s BodyWorn body cam: its activation remote advertises this
    // (nite-oui-collection field capture, via all-cameras-are-beacons).
    if (strcasestr(name, "BodyWorn")) return DetectionType::AXON;
    // A Flipper advertises "Flipper " followed by the unit's name. The
    // owner can change it, which is exactly why a name match is graded
    // down where it is used -- it is a string, not a signature. Left in
    // because the default is what most of them are still called, and it
    // costs nothing next to three exact signatures that cannot be typed.
    if (strcasestr(name, "Flipper"))  return DetectionType::HACKER;
    return DetectionType::UNKNOWN;
}

bool isP2pCamSsid(const char* ssid) {
    if (!ssid) return false;
    const char* p = ssid;
    int n = 0;
    while (*p >= 'A' && *p <= 'Z') { p++; n++; }
    if (n < 3 || n > 4 || *p != '-') return false;
    p++;
    for (int i = 0; i < 6; i++, p++) if (*p < '0' || *p > '9') return false;
    if (*p != '-') return false;
    p++;
    for (int i = 0; i < 5; i++, p++) if (*p < 'A' || *p > 'Z') return false;
    return *p == '\0';
}

// V380's setup hotspot: "MV" and the camera's UID, six or more digits.
static bool isV380Ssid(const char* ssid) {
    if (!ssid || ssid[0] != 'M' || ssid[1] != 'V') return false;
    const char* p = ssid + 2;
    int n = 0;
    while (*p >= '0' && *p <= '9') { p++; n++; }
    return n >= 6 && *p == '\0';
}

// The names that are a shape rather than a prefix. Shared by lookupSsid()
// and ssidVendorName() so the two never disagree.
static const char* ssidPatternName(const char* ssid) {
    if (!ssid) return nullptr;
    if (isP2pCamSsid(ssid))             return "P2P-Cam";
    if (isV380Ssid(ssid))               return "V380";
    if (strcasestr(ssid, "_VANTRUE_"))  return "Vantrue";
    return nullptr;
}

DetectionType lookupSsid(const char* ssid) {
    if (!ssid) return DetectionType::UNKNOWN;
    // A provisioned camera's bare "Flock" (Flock-You's firmware dump). Exact,
    // not a prefix: "Flock" as a prefix would take "Flockhart Family WiFi".
    if (strcasecmp(ssid, "Flock") == 0) return DetectionType::FLOCK;
    for (uint16_t i = 0; i < kSsidCount; i++) {
        size_t n = strlen(kSsidPrefixes[i].prefix);
        if (strncasecmp(ssid, kSsidPrefixes[i].prefix, n) == 0) {
            return kSsidPrefixes[i].type;
        }
    }
    // The camera hotspots that are a shape, not a prefix (all CAMERA).
    if (ssidPatternName(ssid)) return DetectionType::CAMERA;
    return DetectionType::UNKNOWN;
}

const char* uuidName(uint16_t uuid16) {
    for (uint16_t i = 0; i < kUuidCount; i++)
        if (kUuidTable[i].uuid == uuid16) return kUuidTable[i].name;
    return nullptr;
}

const char* mfgIdName(uint16_t mfgId) {
    for (uint16_t i = 0; i < kMfgIdCount; i++)
        if (kMfgIdTable[i].mfgId == mfgId) return kMfgIdTable[i].name;
    return nullptr;
}

const char* ssidVendorName(const char* ssid) {
    if (!ssid) return nullptr;
    for (uint16_t i = 0; i < kSsidCount; i++) {
        size_t n = strlen(kSsidPrefixes[i].prefix);
        if (strncasecmp(ssid, kSsidPrefixes[i].prefix, n) == 0) {
            return kSsidPrefixes[i].name;
        }
    }
    return ssidPatternName(ssid);
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
// PATTERN 3: 4C 00 12 02
//   The same Find My type with a two-byte payload: the "nearby" frame a
//   tag sends while its owner's phone is within reach. Found on the bench
//   2026-11-20: an AirTag next to its phone sent nothing the first two
//   patterns match, and was invisible until this one went in.
//
// PATTERN 4: 4C 00 07 19 xx 00 55
//   Apple's Proximity Pairing block (type 0x07), which an AirTag sends
//   while it is NOT set up with a phone: out of the box, or after being
//   reset. The same block is what AirPods and the other headphones send,
//   which is why 0x07 alone was a false positive for a long time; the
//   two bytes after the prefix are the device model, and 00 55 is the
//   AirTag's. Captured off a real tag on the bench, 2026-11-20:
//   4c 00 07 19 05 00 55 10 00 00 01 17 17 ...
//   AirPods send 01 and their own model there (0E 20, 0F 20, 02 20 ...).
static bool isAirTagPairing(const uint8_t* p, uint8_t left) {
    // p points at 4C; needs 4C 00 07 19 xx 00 55.
    return left >= 7 && p[0] == 0x4C && p[1] == 0x00 && p[2] == 0x07 && p[3] == 0x19 &&
           p[5] == 0x00 && p[6] == 0x55;
}

bool isAirTagPayload(const uint8_t* payload, uint8_t len) {
    if (!payload || len < 4) return false;
    for (uint8_t i = 0; i + 3 < len; i++) {
        if (payload[i] == 0x1E && payload[i + 1] == 0xFF &&
            payload[i + 2] == 0x4C && payload[i + 3] == 0x00) {
            // A 30-byte Apple block whose type byte is 0x07 is Proximity
            // Pairing: an AirTag not yet set up (model 00 55), or AirPods and
            // the other headphones, which used to come up as AIRTAG here.
            // 0x12 is the offline-finding block a set-up tag sends. Anything
            // else keeps the old loose match.
            if (i + 4 < len && payload[i + 4] == 0x07)
                return isAirTagPairing(payload + i + 2, (uint8_t)(len - i - 2));
            return true;
        }
        if (payload[i] == 0x4C && payload[i + 1] == 0x00 && payload[i + 2] == 0x12 &&
            (payload[i + 3] == 0x19 || payload[i + 3] == 0x02)) return true;
        if (isAirTagPairing(payload + i, (uint8_t)(len - i))) return true;
    }
    return false;
}

// Apple's Continuity "Hey Siri" message (furiousMAC/continuity, hey_siri.md):
// type 0x08, length 7 -- a perceptual hash (2), SNR (1), confidence (1), the
// device class (2), a random byte (1). Class 0x0007 is a HomePod, and nothing
// else sends it. It goes out only when somebody near it says "Hey Siri", so
// a HomePod shows up rarely, but when it does there is no doubt about it.
// The class is read both ways round: the write-up gives it as a UINT16
// without saying which end comes first.
bool isHomePodSiri(const uint8_t* mfg, uint8_t len) {
    if (!mfg || len < 4 || mfg[0] != 0x4C || mfg[1] != 0x00) return false;
    for (uint8_t i = 2; i + 1 < len; ) {
        const uint8_t type = mfg[i], l = mfg[i + 1];
        if ((uint16_t)i + 2 + l > len) return false;
        if (type == 0x08 && l == 7) {
            const uint8_t* v = mfg + i + 2;
            return (v[4] == 0x00 && v[5] == 0x07) || (v[4] == 0x07 && v[5] == 0x00);
        }
        i = (uint8_t)(i + 2 + l);
    }
    return false;
}

uint8_t airTagFrame(const uint8_t* payload, uint8_t len, uint8_t* status) {
    if (!payload || len < 4) return 0;
    for (uint8_t i = 0; i + 3 < len; i++) {
        if (payload[i] == 0x4C && payload[i + 1] == 0x00 && payload[i + 2] == 0x12 &&
            (payload[i + 3] == 0x19 || payload[i + 3] == 0x02)) {
            if (status) *status = (i + 4 < len) ? payload[i + 4] : 0;
            return payload[i + 3];
        }
        // Not set up with a phone yet: 0x07, with the pairing prefix byte
        // where the status would be.
        if (isAirTagPairing(payload + i, (uint8_t)(len - i))) {
            if (status) *status = payload[i + 4];
            return 0x07;
        }
    }
    return 0;
}

// The offline-finding block's status byte, when the advert has one:
// 4C 00 12 19 <status> <key...>. 0 when it is not there. Bits 7-6 are the
// battery and bit 5 is set while the tag has been near its owner's phone
// lately (the public Find My write-ups call it "maintained"); clear means
// it has been apart from its owner, which is the state it beeps in and the
// state worth knowing about. Read off real tags on the bench before the
// card leans on it harder than a chip.
uint8_t airTagStatusByte(const uint8_t* payload, uint8_t len) {
    if (!payload || len < 5) return 0;
    for (uint8_t i = 0; i + 4 < len; i++)
        if (payload[i] == 0x4C && payload[i + 1] == 0x00 &&
            payload[i + 2] == 0x12 && payload[i + 3] == 0x19) return payload[i + 4];
    return 0;
}

// Walks the AD structures of a raw advertisement: length, type, data.
// Calls f(type, data, dataLen) for each; stops early if f returns true.
template <typename F>
static bool forEachAd(const uint8_t* p, uint8_t len, F f) {
    if (!p) return false;
    for (uint8_t i = 0; i + 1 < len; ) {
        const uint8_t l = p[i];
        if (l == 0 || i + 1 + l > len) break;
        if (f(p[i + 1], p + i + 2, (uint8_t)(l - 1))) return true;
        i = (uint8_t)(i + 1 + l);
    }
    return false;
}

bool isAxonBwcPayload(const uint8_t* payload, uint8_t len) {
    // "BWCDEVICE" forwards, and backwards: in the field it rides inside a
    // 128-bit UUID (AD 0x21), which is little-endian, so the bytes read
    // "...ECIVEDCWB" (all-cameras-are-beacons, owner-confirmed 2026-09-19).
    static const char F[] = "BWCDEVICE";
    const uint8_t n = 9;
    if (!payload || len < n) return false;
    for (uint8_t i = 0; i + n <= len; i++) {
        bool fw = true, bw = true;
        for (uint8_t k = 0; k < n && (fw || bw); k++) {
            if (payload[i + k] != (uint8_t)F[k]) fw = false;
            if (payload[i + k] != (uint8_t)F[n - 1 - k]) bw = false;
        }
        if (fw || bw) return true;
    }
    return false;
}

uint8_t fmdnFrameType(const uint8_t* payload, uint8_t len) {
    uint8_t ft = 0;
    forEachAd(payload, len, [&](uint8_t type, const uint8_t* d, uint8_t n) {
        if (type == 0x16 && n >= 3 && d[0] == 0xAA && d[1] == 0xFE && (d[2] == 0x40 || d[2] == 0x41)) {
            ft = d[2];
            return true;
        }
        return false;
    });
    return ft;
}

bool isFmdnPayload(const uint8_t* payload, uint8_t len) {
    return fmdnFrameType(payload, len) != 0;
}

uint16_t serviceDataUuid(const uint8_t* payload, uint8_t len) {
    uint16_t found = 0;
    forEachAd(payload, len, [&](uint8_t type, const uint8_t* d, uint8_t n) {
        if (type != 0x16 || n < 2) return false;
        const uint16_t u = (uint16_t)(d[0] | (d[1] << 8));
        if (u == 0xFEAA || u == 0xFFFA) return false;   // their own checks, which read the frame
        for (uint16_t i = 0; i < kUuidCount; i++)
            if (kUuidTable[i].uuid == u) { found = u; return true; }
        return false;
    });
    return found;
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
        // MY ALERTS: the owner named the network. Whether it is what they
        // think it is, is theirs to judge; that it matched is certain.
        case DetectionType::MINE:
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
        // signatures -- the three Flipper Zero service UUIDs and a
        // Pwnagotchi announcing its own handshake count --
        // set HIGH explicitly at their match sites, and the OUI rows carry
        // their own. This is the value a caller gets when nothing more
        // specific was established.
        case DetectionType::HACKER:
        // The base grade, for a DevAddr of type 3 and up: eleven to seventeen
        // bits of a 21-bit NetID reach the air, so the registry row it lands
        // on is the likeliest owner and not the only possible one. Types 0-2
        // carry the whole NetID and are graded HIGH where the sighting is made.
        case DetectionType::LORAWAN:
        // Sonos by its own company ID, UUID or prefix: the maker is certain,
        // the microphone is likely (some Sonos have none). A HomePod's Siri
        // block is set HIGH where it is matched.
        case DetectionType::LISTENER:
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
