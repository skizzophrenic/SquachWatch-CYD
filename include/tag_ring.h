// SquachWatch-CYD — RING: make a tracker that may be following you play its
// sound, so it can be found.
//
// The idea came from JBoHack and Hedge.
//
// The board connects to the tracker over Bluetooth for a few seconds and asks
// it to beep. Three sound services exist, tried in this order:
//   AirTag's own:  service 7DFC9000-..., write the single byte 0xAF; the tag
//                  hangs up when it is done.
//   DULT (the shared Apple/Google unwanted-tracker standard): indications on,
//                  then 00 03.
//   Find My accessories (service FD44): indications on, then 01 00 03.
// The UUIDs and command bytes are the ones AirGuard (seemoo-lab, Apache-2.0)
// documents. Trackers only answer while they are away from their owner --
// that is the anti-stalking design -- so "no sound" usually means it is still
// near its owner's phone.
//
// Only on the boards with room for Bluetooth's connecting role (the S3s and
// the C5s, SQW_RING=1 in platformio.ini). The CYDs leave it off: it costs
// about 66 KB of flash and the working memory a connection needs, and a CYD
// has neither to spare.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "state.h"

#ifndef SQW_RING
#define SQW_RING 0
#endif

class DetectionEngine;

namespace TagRing {

enum class Result : uint8_t {
    NONE,        // nothing new to report
    RANG,        // the tracker took the command
    NO_ANSWER,   // no connection: gone, out of range, or a new address
    NO_SOUND,    // connected, but none of the three sound services
    REFUSED,     // the sound service said no (near its owner, most likely)
};

// The kinds RING is offered on. False on every board built without it.
inline bool canRing(DetectionType t) {
#if SQW_RING
    return t == DetectionType::AIRTAG || t == DetectionType::SAMSUNG_TAG ||
           t == DetectionType::GOOGLE_TAG || t == DetectionType::TILE;
#else
    (void)t;
    return false;
#endif
}

#if SQW_RING
// From the scan callback (the Bluetooth host task): a tracker's address and
// its address type, so the connect asks for the right one first. A short
// table of the latest few; the oldest goes.
void noteSeen(const uint8_t mac[6], uint8_t addrType, bool connectable);
// The tracker heard most recently, for the console's RING. False if none.
bool latest(uint8_t mac[6]);
// From the main loop. Pauses the scan, then connects in a task of its own so
// the screen keeps drawing. False if a ring is already under way.
bool start(DetectionEngine& eng, const uint8_t mac[6]);
bool busy();
// From the main loop, every pass: gives the radio back once the task is done
// and returns its result once (NONE otherwise).
Result poll(DetectionEngine& eng);
// RING NAME <text>, for the bench: the next device that takes connections
// and has <text> in its advertised name gets rung, so a connect can be tested
// against something known to answer. findWanted() is cheap: the scan callback
// asks it for every advert.
void armFind(const char* text);
bool findWanted();
void noteNamed(const uint8_t mac[6], uint8_t addrType, const char* name, bool connectable,
               const uint8_t* payload, size_t payloadLen);
bool takeFound(uint8_t mac[6]);
// Two short lines for a toast.
const char* title(Result r);
const char* detail(Result r);
#endif

}  // namespace TagRing
