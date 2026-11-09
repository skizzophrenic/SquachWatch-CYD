// SquachWatch-CYD emulator -- HUMAN SENSING's engine, pretended.
//
// The real engine (src/sense_cyd/sense_cyd.cpp) joins a network and reads the
// radio's channel state, which a PC cannot do. This stands in for it so the
// real screen (src/sense_cyd/ui_sensing_cyd.cpp) and the real store
// (sense_store.cpp) can be rendered at every panel size: statuses at 10 a
// second into SenseStore, and a spectrum ring the screen reads the way it
// reads the engine's. Built with SENSE_CYD_APP on its own objects only (see
// the Makefile), so the rest of the emulator is untouched.
//
// SQUACHSIM_SENSE picks the room: clear (default), presence, motion, cal,
// low, join, nowifi.
#include <Arduino.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "sense_cyd.h"
#include "sense_store.h"

volatile uint8_t g_senseConsole = 0;

namespace {
enum Room { R_CLEAR, R_PRESENCE, R_MOTION, R_CAL, R_LOW, R_JOIN, R_NOWIFI };
Room s_room = R_CLEAR;
uint32_t s_next = 0, s_seed = 12345, s_lastMotion = 0;
const int ROWS = 108, BINS = 32;
uint8_t s_rows[ROWS][BINS];
int s_head = 0, s_count = 0, s_peak = -1;

float rnd() { s_seed = s_seed * 1664525u + 1013904223u; return (s_seed >> 8) / 16777216.0f; }

void step(uint32_t at) {
    SenseReport r = {};
    r.upMs = at; r.chan = 6; r.rssi = -52; r.rate = 46; r.rel = 90; r.cal = 100;
    r.mode = (s_room == R_JOIN || s_room == R_NOWIFI) ? SENSE_MODE_JOINING : SENSE_MODE_STA;
    float sc;
    switch (s_room) {
        case R_MOTION:   sc = 55 + 35 * fabsf(sinf(at / 700.0f)) + rnd() * 12; break;
        case R_PRESENCE: sc = 12 + rnd() * 10 + (rnd() < 0.05f ? 25 : 0); break;
        default:       sc = 4 + rnd() * 8; break;
    }
    r.score = (uint8_t)(sc > 100 ? 100 : sc);
    r.state = s_room == R_CAL ? SENSE_STATE_CAL : s_room == R_LOW ? SENSE_STATE_LOWDATA
            : (r.score >= 28 ? SENSE_STATE_MOTION : SENSE_STATE_STILL);
    if (s_room == R_CAL) r.cal = 64;
    if (r.state == SENSE_STATE_MOTION) s_lastMotion = at;
    r.presence = s_room == R_PRESENCE || s_room == R_MOTION || (s_lastMotion && at - s_lastMotion < 15000);
    SenseStore::status(r, at);
    SenseStore::router(0x5a17, r.rssi, r.rate, r.score, r.cal, r.rel, at);
    // The spectrum: a breathing-speed rhythm when someone sits still, a broad
    // smear while they move, faint noise otherwise.
    uint8_t* row = s_rows[s_head];
    int best = -1; float bestV = 120;
    for (int b = 0; b < BINS; b++) {
        float v = rnd() * 60 - 10;
        if (b < 2) v = 0;
        else if (s_room == R_PRESENCE || s_room == R_CLEAR) v += 170 * expf(-((b - 4) * (b - 4)) / 1.2f) * (s_room == R_PRESENCE ? 1.0f : 0.55f) * (0.8f + rnd() * 0.3f);
        else if (s_room == R_MOTION) { const float c = 9 + 4 * sinf(at / 900.0f); v += 120 * expf(-((b - c) * (b - c)) / 30) + rnd() * 60; }
        row[b] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
        if (b >= 2 && v > bestV) { bestV = v; best = b; }
    }
    s_peak = best;
    s_head = (s_head + 1) % ROWS;
    if (s_count < ROWS) s_count++;
}
}  // namespace

// The emulator's driver: begin() fills the last 30 s, step() keeps it going.
void simSenseBegin(uint32_t now) {
    const char* e = getenv("SQUACHSIM_SENSE");
    s_room = !e ? R_CLEAR : !strcmp(e, "presence") ? R_PRESENCE : !strcmp(e, "motion") ? R_MOTION
           : !strcmp(e, "cal") ? R_CAL : !strcmp(e, "low") ? R_LOW : !strcmp(e, "join") ? R_JOIN
           : !strcmp(e, "nowifi") ? R_NOWIFI : R_CLEAR;
    SenseStore::begin();
    s_head = s_count = 0;
    if (s_room == R_NOWIFI) { s_next = now; return; }
    const Room keep = s_room;
    // 30 s back, whatever the clock reads: the times are uint32 and the store
    // only ever subtracts them, so a start before zero wraps harmlessly.
    for (int32_t ago = 30000; ago > 0; ago -= 100) {
        // eight seconds of walking from 22 s to 14 s ago, for the graph and PEAK
        if (keep != R_CAL && keep != R_LOW && keep != R_JOIN) s_room = (ago < 22000 && ago > 14000) ? R_MOTION : keep;
        step(now - (uint32_t)ago);
    }
    s_room = keep;
    s_next = now;
}
void simSenseStep(uint32_t now) {
    if (s_room == R_NOWIFI) return;
    while ((int32_t)(now - s_next) >= 0) { step(s_next); s_next += 100; }
}

// What the screen asks the engine.
const char* senseCydNetwork() { return s_room == R_NOWIFI ? "" : "SquachNet"; }
uint16_t    senseCydReason()  { return 0; }
uint8_t     senseCydMode()    { return s_room == R_JOIN || s_room == R_NOWIFI ? SENSE_MODE_JOINING : SENSE_MODE_STA; }
void        senseCydCommand(const char*) {}
int            senseCydSpecRows() { return s_count; }
int            senseCydSpecRowsMax() { return ROWS; }
const uint8_t* senseCydSpecRow(int r) { return (r < 0 || r >= s_count) ? nullptr : s_rows[(s_head - 1 - r + ROWS * 2) % ROWS]; }
int            senseCydSpecPeak() { return s_peak; }
float          senseCydSpecBinHz() { return 10.0f / 128; }
