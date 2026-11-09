// SquachWatch-CYD -- HUMAN SENSING: WiFi motion sensing on the CYD's own radio
// (SENSE_CYD_APP builds: the cyd-sense, cyd-fast-sense and cyd-ili9341-sense envs).
//
// The engine is src/sense_cyd/sense_cyd.cpp: join a saved network, ping the
// router 50 times a second, and run a motion model on the channel state (CSI)
// of the router's replies. It keeps the screen's numbers in SenseStore
// (include/sense_store.h); the screen is src/sense_cyd/ui_sensing_cyd.cpp.
//
// The CYD has one radio, so sensing and the detector take turns: main.cpp
// starts the engine when the HUMAN SENSING screen opens (after the detector has
// let go of the radio, DetectionEngine::startUpdateRadio) and stops it when the
// screen goes (then stopUpdateRadio).
//
// What it can tell: a still room from a room with something moving in it, on
// the radio path between the router and the board. Not who, how many, or where.
#pragma once
#include <stdint.h>

#if defined(SENSE_CYD_APP)
// The state of the room, as the motion model calls it.
#define SENSE_STATE_CAL      0   // calibrating: hold still
#define SENSE_STATE_LOWDATA  1   // too few readings, or too little trust in them, to say
#define SENSE_STATE_STILL    2
#define SENSE_STATE_POSSIBLE 3
#define SENSE_STATE_MOTION   4

// What the radio is doing.
#define SENSE_MODE_OFF       0
#define SENSE_MODE_STA       2   // joined, pinging the router
#define SENSE_MODE_JOINING   3

#define SENSE_LINE_HZ        10  // status updates a second

// One status update from the engine, ten a second.
struct SenseReport {
  uint32_t upMs;
  uint8_t  score;     // 0-100 motion score
  uint8_t  state;     // SENSE_STATE_*
  uint8_t  rel;       // 0-100 how far to trust the score
  uint8_t  cal;       // 0-100 calibration progress
  uint8_t  mode;      // SENSE_MODE_*
  uint16_t rate;      // readings a second
  uint16_t chan;      // WiFi channel
  uint16_t live;      // 0-1000, the per-reading change against the calibration
  int8_t   rssi;      // dBm
  uint32_t drop;      // readings lost to a full queue since the start
  bool     presence;  // MOTION now, or within the hold time (15 s)
};

void        senseCydStart(const char* ssid, const char* pass);   // ssid "" = none saved
void        senseCydStop();
void        senseCydTick();                 // every loop pass; nothing when stopped
bool        senseCydRunning();
void        senseCydCommand(const char* cmd);   // "cal", "rates", ... (the console's SENSE <cmd>)
const char* senseCydNetwork();              // the network it was given, "" for none
uint16_t    senseCydReason();               // the driver's last disconnect reason
uint8_t     senseCydMode();                 // SENSE_MODE_*

// SENSE OPEN / SENSE CLOSE / SENSE OPEN NOWIFI on the console (bench): 1 open the
// screen, 2 back to the detector, 3 open it as if no network were saved (the
// ADD WIFI state, without touching the saved list); main.cpp acts and clears it.
extern volatile uint8_t g_senseConsole;

// SPECTRUM.EXE: rows of 32 bins (0-2.42 Hz), 0-255 over the room's recent level,
// row 0 the newest, three a second; the peak bin standing 8 dB clear, or -1.
int            senseCydSpecRows();
int            senseCydSpecRowsMax();   // the most rows it keeps (108, ~11 s)
const uint8_t* senseCydSpecRow(int r);
int            senseCydSpecPeak();
float          senseCydSpecBinHz();
#endif
