// SquachWatch-Sim — synthetic detections for the interactive emulator.
//
// The emulator has no radios, so the only way to see the UI react to a
// detection is to hand it one. This builds a Detection that looks like
// something the real matcher would have produced: the vendor strings
// and OUIs below are lifted from src/signatures.cpp, so a triggered
// AirTag shows up on LOG and ALERT reading exactly the way a real one
// does.
//
// What this does NOT do is exercise the code that decides what counts
// as a detection. detection_sim.cpp's postBle() is `pushLog(d)` -- no
// signature match, no dedup, no reactivation of a stale entry, and no
// Settings::typeEnabled() guard. So the TYPE FILTER screen has no
// effect on anything triggered here, deliberately: filtering is a
// property of the real engine, and mirroring it into the stub would be
// testing the mirror rather than the firmware.
#pragma once
#include <cstdio>
#include <cstring>
#include <cstdint>
#include "state.h"

struct SimDetectionProfile {
    DetectionType type;
    uint8_t       oui[3];
    const char*   vendor;   // fits Detection::vendor[12]
    const char*   name;     // fits Detection::name[20]
    int8_t        rssi;     // a plausible default; the caller can override
};

// Indexed by DetectionType, skipping UNKNOWN. OUIs and vendor strings
// come from src/signatures.cpp; the SIG-UUID-matched types (Tile, Meta,
// Raven, drones, the tag networks) have no OUI of their own in the
// firmware, so those get a locally-administered prefix rather than one
// that would claim to be a real registration.
inline const SimDetectionProfile kSimProfiles[] = {
    { DetectionType::FLOCK,       {0x24, 0x0A, 0xC4}, "Flock-ESP32", "Flock Safety",    -68 },
    { DetectionType::AXON,        {0x00, 0x25, 0xDF}, "Axon-Body",   "Axon Body 3",     -74 },
    { DetectionType::META,        {0x02, 0xFD, 0x5F}, "Meta",        "Ray-Ban Meta",    -66 },
    { DetectionType::SKIMMER,     {0x98, 0xD3, 0x00}, "Skim-SPP",    "HC-05",           -49 },
    { DetectionType::RAVEN,       {0x02, 0x31, 0x00}, "Raven",       "Gunshot Sensor",  -79 },
    { DetectionType::AIRTAG,      {0x02, 0x00, 0x4C}, "Apple",       "AirTag",          -42 },
    { DetectionType::DRONE,       {0x02, 0xFF, 0xFA}, "DroneID",     "OpenDroneID",     -71 },
    { DetectionType::ALPR,        {0x00, 0x0E, 0x58}, "ALPR-Viglnt", "Vigilant ALPR",   -76 },
    { DetectionType::CAMERA,      {0xF0, 0x27, 0x2D}, "Hikvision",   "IP Camera",       -63 },
    { DetectionType::SAMSUNG_TAG, {0x02, 0xFD, 0x5A}, "SmartTag",    "Galaxy SmartTag", -58 },
    { DetectionType::GOOGLE_TAG,  {0x02, 0xFE, 0xAA}, "FindMyDev",   "Find My Device",  -61 },
    { DetectionType::TILE,        {0x02, 0xFE, 0xED}, "Tile",        "Tile Mate",       -81 },
    { DetectionType::RING,        {0xFC, 0x65, 0xDE}, "Ring",        "Ring Doorbell",   -55 },
    { DetectionType::DEAUTH,      {0x02, 0xDE, 0xAD}, "Deauth",      "Deauth Flood",    -47 },
    // The name field is the impersonated SSID for this type, matching
    // what processWiFiQ() puts there on a real hit -- which network is
    // being spoofed is the useful part, not the rogue's own vendor.
    { DetectionType::EVILTWIN,    {0x02, 0xE7, 0x11}, "EvilTwin",    "HomeNet-5G",      -52 },
};
inline const size_t kSimProfileCount = sizeof(kSimProfiles) / sizeof(kSimProfiles[0]);

inline const SimDetectionProfile* simProfileFor(DetectionType t) {
    for (size_t i = 0; i < kSimProfileCount; i++)
        if (kSimProfiles[i].type == t) return &kSimProfiles[i];
    return nullptr;
}

// Builds one synthetic sighting. `serial` varies the low three MAC
// bytes so repeated triggers of the same type read as different units
// of the same make rather than one device seen twice -- which is both
// the more interesting case for testing a scrolling log, and the
// scenario that started all this (a commute past dozens of separate
// AirTags).
//
// firstSeen is `now`, which matters: the CLEAR screen only raises the
// full-screen ALERT for a detection first seen within the last 200ms.
inline bool simMakeDetection(Detection& d, DetectionType type, uint32_t now,
                             int rssi = 0, uint16_t serial = 0) {
    const SimDetectionProfile* p = simProfileFor(type);
    if (!p) return false;

    memset(&d, 0, sizeof(d));
    memcpy(d.mac, p->oui, 3);
    d.mac[3] = (uint8_t)(0xA0 + (serial >> 8));
    d.mac[4] = (uint8_t)(serial & 0xFF);
    d.mac[5] = (uint8_t)(0x5C ^ serial);
    d.rssi      = (int8_t)(rssi != 0 ? rssi : p->rssi);
    d.channel   = (type == DetectionType::DEAUTH) ? (uint8_t)(1 + (serial % 11)) : 0;
    d.type      = type;
    snprintf(d.vendor, sizeof(d.vendor), "%s", p->vendor);
    snprintf(d.name,   sizeof(d.name),   "%s", p->name);
    d.firstSeen = now;
    d.lastSeen  = now;
    d.hits      = 1;
    d.active    = true;
    return true;
}
