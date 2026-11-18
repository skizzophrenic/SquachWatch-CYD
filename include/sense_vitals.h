// SquachWatch-CYD -- HUMAN SENSING: VITALS, breathing (and, experimentally,
// heart rate) from the chest signal (SENSE_RICH boards).
//
// Ported from the Tab5 Monster C5 CSI Human Sensing project, where it runs on
// the screen side of a UART. Here the engine (sense_cyd.cpp) runs the chest
// signal (csi_vitals.*) on the router's readings while VITALS is up and hands
// each 50 ms frame to feed(). Sitting still near the board, c1 is mostly the
// chest rising and falling. This module:
//
//   - filters it into a breathing wave and a heart band (0.8-2.2 Hz);
//   - finds breaths (peaks with hysteresis) and works out the rate, rhythm,
//     depth against your own breaths, inhale:exhale, sighs and holds;
//   - takes a 512-point spectrum every 0.3 s for the waterfall, the breathing
//     peak and its signal-to-noise, and the heart band's peak, skipping the
//     breathing's harmonics;
//   - only calls the heart number a lock when its peak stands out, you have
//     been still a while, and the last few estimates agree.
//
// Not a medical device: a sensor demo. Nothing here diagnoses anything.
#pragma once
#include <stdint.h>
#include "sense_cyd.h"

#if SENSE_RICH

namespace Vitals {

static const float    FS      = 20.0f;   // chest-signal frames a second
static const uint16_t HN      = 1200;    // 60 s of filtered signal kept
static const uint8_t  BINS    = 64;      // spectrum bins shown: 0-2.5 Hz
static const uint8_t  ROWS    = 140;     // waterfall rows: 42 s at one per 0.3 s
static const uint8_t  EVN     = 16;

// The buffers (~40 KB, PSRAM) for the time VITALS is up, and back.
bool begin(uint32_t now);
void end();
// One chest-signal frame from the engine (20 a second while VITALS is up).
void feed(float c1, uint8_t q);
// The numbers from the frames fed since the last call. Cheap when nothing is new.
void update(uint32_t now);
void newSession(uint32_t now);           // the NEW SESSION button
uint32_t lastFrameAt();                  // millis() of the last frame fed, 0 for none

struct Metrics {
    bool     live;            // frames in the last 1.5 s
    bool     motion;          // moving now: the numbers are paused
    float    still;           // s since the last movement
    float    bpm;             // breaths a minute, <0 = not yet
    int      rhythm;          // 0-100, <0 = not yet
    int      depth;           // last breaths vs your median, %, <0 = not yet
    float    ie;              // exhale / inhale time, <0 = not yet
    float    pause;           // s since the last breath (a hold when > 8 and still)
    int      sighs;
    int      total;           // breaths this session
    float    peakHz;          // the breathing peak in the spectrum
    int      snr;             // dB, breathing peak over the noise floor
    float    hr;              // beats a minute, the heart band's estimate
    float    conf;            // 0-1: shown as a number from 0.35
    int      rate;            // the router's readings a second
    uint8_t  q;               // the radio's share of movement in c1
    float    session;         // s since the session started
    float    breathNow;       // the breathing wave now, normalised -1..1
    bool     inhale;          // rising
};
const Metrics& metrics();

// The last n frames of the filtered signals, newest first (k = 0 is now).
uint16_t count();
float breathAt(uint16_t k);              // breathing wave, normalised to recent breaths
float heartAt(uint16_t k);               // heart band, normalised
bool  motionAt(uint16_t k);

// The current spectrum (0..BINS-1, 0-1 on a log scale) and the waterfall
// (row 0 = newest), 0-255.
const float*   spectrum();
const uint8_t* waterfallRow(uint8_t r);
uint8_t        waterfallRows();
float binHz(int b);

// Breath peaks, newest first, in seconds ago.
uint8_t breathTicks(float* agoOut, uint8_t cap);

// The lab log: moves, holds, sighs, heart lock-ons, newest first.
enum class Ev : uint8_t { MOVED, HOLD, SIGH, HEART_LOCK };
struct Event { float t; Ev kind; uint8_t val; };   // t = session seconds
uint8_t events(Event* out, uint8_t cap);

}  // namespace Vitals
#endif  // SENSE_RICH
