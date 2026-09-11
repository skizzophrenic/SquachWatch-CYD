// SquachWatch-Sim — synthetic detections for the interactive emulator.
//
// The emulator has no radios, so the only way to see the UI react to a
// detection is to hand it one. This builds a Detection that looks like
// something the real matcher would have produced: the vendor strings
// and OUIs below are lifted from src/signatures.cpp, so a triggered
// AirTag shows up on LOG and ALERT reading exactly the way a real one
// does.
//
// ONE PROFILE PER DEVICE, not per type. A type is a bucket -- HACKER is a
// Flipper Zero, a Pwnagotchi, a Pineapple and a deauther -- and each device
// has its own MORE INFO page (device_info.cpp), so each has to be summonable
// here or its page is one nobody can look at. test/device_info_test.cpp fails
// if a page has no profile. The first profile of each type is what `T AIRTAG`
// and the other by-type callers get.
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
#include "signatures.h"

struct SimDetectionProfile {
    DetectionType type;
    uint8_t       oui[3];
    const char*   vendor;   // as the firmware writes it; fits Detection::vendor[12]
    const char*   name;     // fits Detection::name[20]; "" where the real match has none
    int8_t        rssi;     // a plausible default; the caller can override
    bool          wifi;     // found over WiFi: gets a channel, which is how LOG tells
    const char*   label;    // what the emulator's picker calls it
};

// SIG-UUID and company-ID matches have no OUI of their own in the firmware,
// so those get a locally-administered prefix rather than one that would claim
// to be a real registration.
inline const SimDetectionProfile kSimProfiles[] = {
    // FLOCK -- the first is what `T FLOCK` gets
    { DetectionType::FLOCK,       {0x24, 0x0A, 0xC4}, "Flock-ESP32", "",               -68, true,  "ESP32 module (maybe Flock)" },
    { DetectionType::FLOCK,       {0xB4, 0x1E, 0x52}, "Flock-MA-L",  "",               -70, true,  "Flock camera (registered)" },
    { DetectionType::FLOCK,       {0x24, 0xB2, 0xB9}, "Flock-Liteo", "",               -72, true,  "Liteon chip (maybe Flock)" },
    { DetectionType::FLOCK,       {0xD0, 0x39, 0x57}, "Flock",       "",               -74, true,  "Unverified Flock address" },
    { DetectionType::FLOCK,       {0x02, 0xF1, 0x0C}, "Flock-Setup", "",               -60, true,  "Flock setup network" },
    { DetectionType::FLOCK,       {0x02, 0xF1, 0x0B}, "Flock-BLE",   "FS Ext Battery", -66, false, "Flock external battery" },
    { DetectionType::FLOCK,       {0x02, 0x09, 0xC8}, "Flock-BLE",   "",               -71, false, "Flock Bluetooth (XUNTONG)" },
    // AXON
    { DetectionType::AXON,        {0x02, 0xAB, 0x03}, "Axon-Body3",  "",               -66, true,  "Axon Body 3" },
    { DetectionType::AXON,        {0x02, 0xAB, 0x02}, "Axon-Body2",  "",               -67, true,  "Axon Body 2" },
    { DetectionType::AXON,        {0x02, 0xAB, 0x04}, "Axon-Body4",  "",               -65, true,  "Axon Body 4" },
    { DetectionType::AXON,        {0x02, 0xAF, 0x1D}, "Axon-Field",  "",               -72, true,  "Axon field network" },
    { DetectionType::AXON,        {0x00, 0x25, 0xDF}, "Axon",        "",               -74, true,  "Axon / TASER hardware" },
    { DetectionType::AXON,        {0xE4, 0x05, 0x40}, "Axon-Body",   "",               -73, true,  "Axon body cam address" },
    { DetectionType::AXON,        {0x28, 0x24, 0xFF}, "Axon-Signal", "",               -70, true,  "Axon Signal sensor" },
    // META
    { DetectionType::META,        {0x02, 0xFD, 0x5F}, "RayBanMeta",  "",               -66, false, "Ray-Ban Meta glasses" },
    { DetectionType::META,        {0x02, 0x01, 0xAB}, "Meta",        "",               -64, false, "Meta radio (glasses or Quest)" },
    { DetectionType::META,        {0x02, 0x0D, 0x53}, "Luxottica",   "",               -67, false, "Luxottica frames" },
    { DetectionType::META,        {0x02, 0x03, 0xC2}, "Snap",        "",               -65, false, "Snap Spectacles" },
    // SKIMMER -- over Bluetooth a name match is labelled just "BLE"
    { DetectionType::SKIMMER,     {0x98, 0xD3, 0x00}, "BLE",         "HC-05",          -49, false, "HC-05 module" },
    { DetectionType::SKIMMER,     {0x02, 0x42, 0x04}, "BLE",         "RN42",           -52, false, "RN42 module" },
    { DetectionType::SKIMMER,     {0x02, 0xB7, 0x04}, "BLE",         "BT04-A",         -55, false, "BT04-A module" },
    { DetectionType::SKIMMER,     {0x20, 0x13, 0x00}, "Skim-Linvor", "linvor",         -51, false, "Linvor HC-06" },
    { DetectionType::SKIMMER,     {0x02, 0x11, 0x01}, "Skim-SPP",    "",               -53, false, "Bluetooth serial port" },
    // RAVEN, AIRTAG, DRONE
    { DetectionType::RAVEN,       {0x02, 0x31, 0x00}, "Raven",       "Gunshot Sensor", -79, false, "Raven gunshot sensor" },
    { DetectionType::AIRTAG,      {0x02, 0x00, 0x4C}, "Apple",       "AirTag",         -42, false, "Apple AirTag" },
    { DetectionType::DRONE,       {0x02, 0xFF, 0xFA}, "DroneID",     "OpenDroneID",    -71, false, "Drone Remote ID" },
    // ALPR -- Motorola Solutions, which absorbed Vigilant, and Genetec AutoVu
    { DetectionType::ALPR,        {0x00, 0x04, 0x7D}, "ALPR-Mtrla",  "",               -76, true,  "Motorola plate reader" },
    { DetectionType::ALPR,        {0x00, 0xBF, 0x15}, "ALPR-Gentec", "",               -75, true,  "Genetec AutoVu" },
    // CAMERA -- each maker's own prefix from the OUI table
    { DetectionType::CAMERA,      {0x2C, 0xAA, 0x8E}, "Wyze",        "",               -63, true,  "Wyze camera" },
    { DetectionType::CAMERA,      {0xB8, 0xD7, 0xAF}, "Wyze-Mod",    "",               -66, true,  "Wyze module" },
    { DetectionType::CAMERA,      {0xF0, 0x27, 0x2D}, "Amazon",      "",               -62, true,  "Amazon device" },
    { DetectionType::CAMERA,      {0xC0, 0x56, 0xE3}, "Hikvision",   "",               -64, true,  "Hikvision camera" },
    { DetectionType::CAMERA,      {0x00, 0xE0, 0x4C}, "Realtek",     "",               -67, true,  "Realtek chip" },
    { DetectionType::CAMERA,      {0xBC, 0xDD, 0xC2}, "Arlo",        "",               -65, true,  "Arlo camera" },
    { DetectionType::CAMERA,      {0x4C, 0x69, 0x05}, "Blink",       "",               -61, true,  "Blink camera" },
    { DetectionType::CAMERA,      {0xA4, 0xC1, 0x38}, "Tuya",        "",               -68, true,  "Tuya device" },
    { DetectionType::CAMERA,      {0xE0, 0xA7, 0x00}, "Verkada",     "",               -70, true,  "Verkada camera" },
    { DetectionType::CAMERA,      {0x70, 0x1A, 0xD5}, "Avigilon",    "",               -71, true,  "Avigilon camera" },
    { DetectionType::CAMERA,      {0x00, 0x40, 0x8C}, "Axis",        "",               -69, true,  "Axis camera" },
    // The tag networks, Ring, and the rogue-radio behaviours
    { DetectionType::SAMSUNG_TAG, {0x02, 0xFD, 0x5A}, "Samsung",     "Galaxy SmartTag",-58, false, "Samsung SmartTag" },
    { DetectionType::GOOGLE_TAG,  {0x02, 0xFE, 0xAA}, "Google",      "Find My Device", -61, false, "Google Find My tracker" },
    { DetectionType::TILE,        {0x02, 0xFE, 0xED}, "Tile",        "Tile Mate",      -81, false, "Tile tracker" },
    { DetectionType::RING,        {0xFC, 0x65, 0xDE}, "Ring",        "Ring Doorbell",  -55, true,  "Ring doorbell" },
    { DetectionType::DEAUTH,      {0x02, 0xDE, 0xAD}, "Deauth",      "Deauth Flood",   -47, true,  "WiFi deauth flood" },
    // The name field is the impersonated SSID, as processWiFiQ() writes it.
    { DetectionType::EVILTWIN,    {0x02, 0xE7, 0x11}, "EvilTwin",    "HomeNet-5G",     -52, true,  "Evil twin AP" },
    // Six hex of the proximity UUID, then major.minor -- as the detector does.
    { DetectionType::IBEACON,     {0x02, 0x00, 0x4C}, "iBeacon",     "B9407F 10.42",   -59, false, "iBeacon" },
    // HACKER -- Flipper Devices' own block, and the name a Flipper advertises
    { DetectionType::HACKER,      {0x0C, 0xFA, 0x22}, "Flipper",     "Flipper Ozzyx",  -49, false, "Flipper Zero" },
    { DetectionType::HACKER,      {0x02, 0x50, 0x57}, "Pwnagotchi",  "rikki",          -62, true,  "Pwnagotchi" },
    { DetectionType::HACKER,      {0x06, 0x50, 0x49}, "Pineapple",   "",               -60, true,  "WiFi Pineapple" },
    { DetectionType::HACKER,      {0x06, 0xDE, 0xA7}, "Deauther",    "",               -57, true,  "ESP deauther" },
    { DetectionType::HACKER,      {0x02, 0xC0, 0xCA}, "Hak5-LA",     "",               -63, true,  "Hak5 address" },
};
inline const size_t kSimProfileCount = sizeof(kSimProfiles) / sizeof(kSimProfiles[0]);

inline const SimDetectionProfile* simProfileFor(DetectionType t) {
    for (size_t i = 0; i < kSimProfileCount; i++)
        if (kSimProfiles[i].type == t) return &kSimProfiles[i];
    return nullptr;
}

// Builds one synthetic sighting. `serial` varies the low three MAC
// bytes so repeated triggers of the same device read as different units
// of the same make rather than one device seen twice -- which is both
// the more interesting case for testing a scrolling log, and the
// scenario that started all this (a commute past dozens of separate
// AirTags).
//
// firstSeen is `now`, which matters: the CLEAR screen only raises the
// full-screen ALERT for a detection first seen within the last 200ms.
inline void simMakeDetection(Detection& d, const SimDetectionProfile& p, uint32_t now,
                             int rssi = 0, uint16_t serial = 0) {
    memset(&d, 0, sizeof(d));
    memcpy(d.mac, p.oui, 3);
    d.mac[3] = (uint8_t)(0xA0 + (serial >> 8));
    d.mac[4] = (uint8_t)(serial & 0xFF);
    d.mac[5] = (uint8_t)(0x5C ^ serial);
    d.rssi      = (int8_t)(rssi != 0 ? rssi : p.rssi);
    // A channel is what marks a WiFi sighting as one -- LOG's long-press
    // decides BLE or WiFi watching on channel == 0.
    d.channel   = p.wifi ? (uint8_t)(1 + (serial % 11)) : 0;
    d.type      = p.type;
    // Without this a memset leaves conf at 0, which is LOW -- and every
    // synthetic sighting would render as a shaky match.
    d.conf      = confidenceFor(p.type);
    snprintf(d.vendor, sizeof(d.vendor), "%s", p.vendor);
    snprintf(d.name,   sizeof(d.name),   "%s", p.name);
    d.firstSeen = now;
    d.lastSeen  = now;
    d.hits      = 1;
    d.active    = true;
}

inline bool simMakeDetection(Detection& d, DetectionType type, uint32_t now,
                             int rssi = 0, uint16_t serial = 0) {
    const SimDetectionProfile* p = simProfileFor(type);
    if (!p) return false;
    simMakeDetection(d, *p, now, rssi, serial);
    return true;
}

// Every profile as JSON, for both emulators' pickers:
//   [[index, "TYPE", "label"], ...]
// Built once; the labels are ASCII with no quotes, so nothing needs escaping.
inline const char* simProfileCatalog() {
    static char buf[6144];
    if (buf[0]) return buf;
    size_t o = (size_t)snprintf(buf, sizeof buf, "[");
    for (size_t i = 0; i < kSimProfileCount && o < sizeof buf; i++)
        o += (size_t)snprintf(buf + o, sizeof buf - o, "%s[%u,\"%s\",\"%s\"]", i ? "," : "",
                              (unsigned)i, detectionTypeName(kSimProfiles[i].type), kSimProfiles[i].label);
    if (o < sizeof buf) snprintf(buf + o, sizeof buf - o, "]");
    return buf;
}
