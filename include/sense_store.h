// SquachWatch-CYD -- what the HUMAN SENSING screen draws from (SENSE_CYD_APP).
//
// The engine (src/sense_cyd/sense_cyd.cpp) hands it a status ten times a second
// and the router's own numbers once a second; this keeps the last 80 s of
// scores, the router as a path through the room, and a log of movements.
#pragma once
#include <stdint.h>
#include "sense_cyd.h"

#if defined(SENSE_CYD_APP)
namespace SenseStore {

static const uint16_t HISTORY = 800;   // the last 80 s, at 10 a second
static const uint8_t  EVENTS  = 12;    // movements remembered

void begin();                 // forget everything: a new session
void end();
void tick(uint32_t now);      // ages the router out after 5 s without news

// From the engine.
void status(const SenseReport& r, uint32_t now);
void router(uint16_t tag, int8_t rssi, uint16_t rate, uint8_t score, uint8_t cal, uint8_t rel, uint32_t now);

// A status within the last two seconds: the engine is running.
bool fresh(uint32_t now);
bool heard();                 // any status since begin()
const SenseReport& last();
uint32_t lastAt();            // millis() of the last status
// How long since a status last said MOTION, in ms; UINT32_MAX if none has.
uint32_t sinceMotion(uint32_t now);

// The history, oldest first: i in [0, count()).
uint16_t count();
uint8_t  scoreAt(uint16_t i);
uint8_t  stateAt(uint16_t i);  // SENSE_STATE_*
bool     presenceAt(uint16_t i);

// The router: the one path through the room the CYD measures.
struct Router {
    bool     live;            // news in the last 5 s
    uint16_t tag;             // last two bytes of its address: a stable name, never shown
    int8_t   rssi;
    uint16_t rate;
    uint8_t  score, cal, rel;
};
const Router& router();

// Movements: a MOTION stretch, newest first.
struct Event { uint32_t at; uint32_t ms; bool open; };
uint8_t events(Event* out, uint8_t cap);

uint32_t statuses();          // since begin()

}  // namespace SenseStore
#endif
