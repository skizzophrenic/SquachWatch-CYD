// SquachWatch-CYD — places: where the board has been, as a count of stops.
//
// The alert card's trail ("it was at every stop") needs to know when you have
// moved somewhere else, and most boards have no GPS. What every board has is
// the WiFi around it: the set of the strongest networks in earshot is a
// fingerprint of where you are, and when that set is replaced wholesale you
// have moved. A place is one such fingerprint; a new one starts when, for two
// checks in a row, fewer than a quarter of the place's anchor networks can
// still be heard. A board with a GPS fix starts a new place when the fix is
// NEW_PLACE_M from where this one began, which is sharper and works in the
// countryside where there is nothing to fingerprint.
//
// Pure arithmetic over what it is fed (host-tested): nothing here touches a
// radio. detection.cpp feeds it every beacon's BSSID and RSSI, the GPS fix when
// there is a fresh one, and the saved networks' names so a place can be HOME.
#pragma once
#include <stdint.h>

namespace Places {

static const uint8_t  CAP          = 8;      // places remembered; ids keep counting past it
static const uint8_t  ANCHOR_N     = 8;      // networks a place's fingerprint holds
static const uint32_t HEARD_MS     = 30000;  // a network heard this lately is still here
static const uint32_t CHECK_MS     = 15000;  // how often "did we move" is asked
static const uint8_t  MIN_ANCHORS  = 3;      // fewer networks than this: no fingerprint, no moving by WiFi
static const uint16_t NEW_PLACE_M  = 300;    // GPS: this far from where the place began is a new place

void begin();
// A beacon from an access point, with its network name (may be empty). A
// name the home check knows marks the current place HOME.
void noteBssid(const uint8_t* bssid, int8_t rssi, const char* ssid, uint32_t nowMs);
// Who knows the saved networks: main.cpp hands this in at boot.
void setHomeCheck(bool (*isHome)(const char* ssid));
// A fresh GPS position (or a phone's). Degrees x 10^7.
void noteFix(int32_t lat7, int32_t lon7, uint32_t nowMs);
// From loop(): decides whether the board has moved.
void tick(uint32_t nowMs);

// The place the board is at now: 1, 2, 3... (0 before anything was heard).
uint8_t  current();
uint32_t startMs(uint8_t id);        // when the board arrived there (0 if forgotten)
bool     isHome(uint8_t id);
bool     hasPos(uint8_t id);
int32_t  lat7(uint8_t id);
int32_t  lon7(uint8_t id);
// Metres between two places, or 0 when either has no position. Capped at
// 65 km, which is further than a trail gets.
uint16_t distanceM(uint8_t a, uint8_t b);
// Places begun between two minutes of uptime, inclusive: how many stops a
// device would have had to be at to be "at every stop".
uint8_t  countBetweenMin(uint16_t fromMin, uint16_t toMin);

// Bench and emulator: start a place by hand, with or without a position.
void forceNew(uint32_t nowMs, bool home = false);
void forceNewAt(uint32_t nowMs, int32_t lat7, int32_t lon7, bool home = false);

void reset();

}  // namespace Places
