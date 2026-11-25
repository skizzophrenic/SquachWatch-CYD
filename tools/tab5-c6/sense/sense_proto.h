// Tab5 Monster C5 CSI Human Sensing -- the line protocol between a sensing radio and the screen.
//
// The sensing radio is a second ESP32 (an ESP32-C5 on the M5MonsterC5, on the
// Tab5's M5-Bus) that does nothing but watch the WiFi channel's shape (CSI)
// and score motion in it. It talks to the app over a plain UART at 115200,
// 8N1, in ASCII lines, so a laptop terminal reads it just as well as the
// Tab5 does and nothing here needs a byte layout agreed on both sides.
//
// Radio -> app, ten times a second:
//
//   $S,<up_ms>,<score>,<state>,<rate>,<chan>,<band>,<rssi>,<rel>,<cal>,<tgts>,<mode>,<amp>,<phase>,<corr>,<live>,<drop>,<pres>
//
//   up_ms  the radio's millis()                 score  0-100 fused motion score
//   state  SENSE_STATE_*                        rate   CSI samples a second, all targets
//   chan   primary channel (1-14, or 5 GHz)     band   2 or 5
//   rssi   dBm of the best target               rel    0-100 how far to trust the score
//   cal    0-100 calibration progress           tgts   transmitters being modelled
//   mode   SENSE_MODE_*                         amp/phase/corr  the three sub-scores
//   live   0-1000 per-sample decorrelation z x100, for the graph
//   drop   CSI samples lost to a full queue since start
//   pres   1 = PRESENCE: MOTION now, or within the hold time (default 15 s)
//          (absent on radios before 2026-11-07; the parser derives it then)
//
// Radio -> app, once a second, one line per transmitter being modelled (up to 4):
//
//   $T,<idx>,<tag>,<rssi>,<rate>,<score>,<cal>,<rel>
//
//   idx    0-3, the radio's slot          tag   last two bytes of its address, hex
//                                               (a stable id for the screen, not shown)
//   rssi   dBm                            rate  its CSI samples a second
//   score  its own 0-100 motion score     cal   0-100 calibration   rel  0-100 trust
//
// Each transmitter's path through the room is disturbed by movement near it,
// so the per-path scores say roughly which side of the room moved. They say
// nothing about distance to a person or how many people there are.
//
// Radio -> app, on boot, on "info" and once a minute:
//
//   $I,<fw>,<mac>,<mode>,<chan>,<ssid-or-->,<heap>
//
// Radio -> app, twenty times a second while VITALS is on (vitals on):
//
//   $V,<up_ms>,<c1>,<c2>,<n>,<q>
//
//   c1, c2  the chest signal: the two strongest shared rhythms across the
//           tones of one transmitter (src/sense_c5/csi_vitals.h), x10000
//   n       CSI samples in this 50 ms frame (0 = none: the last one repeated)
//   q       0-100 how much of the channel's movement c1 carries
//
// Radio -> app, anything else: a line starting with '#', for a human.
//
// App -> radio, one command per line, '\n' terminated:
//
//   park <chan>            promiscuous on one channel; model the busiest transmitters
//   join <ssid> <pass>     join a network (saved), ping the gateway, model the AP
//   forget                 drop the saved network, back to park
//   scan                   list access points on both bands, loudest first (a few seconds' gap)
//   rates                  '#' lines per target: samples/s, RSSI, tones, sub-scores
//   download               reboot into the ROM's serial download mode (for flashing)
//   cal                    recalibrate: keep the room still for the calibration time
//   win <ms>               model window, 200-5000 ms (default 1000)
//   caltime <ms>           calibration time, 5000-120000 ms (default 30000)
//   hold <ms>              how long PRESENCE outlasts the last MOTION, 0-120000 ms (default 15000)
//   info                   send a $I line now
//   raw 0|1                also print one '#' line per CSI sample (bench only)
//   vitals on [<ssid>TAB<pass>]  VITALS: join that network for this session only
//                          (never saved), ping the router 50x a second, send $V.
//                          Without a network: stay parked, follow the busiest transmitter
//   vitals off             stop $V and put the radio back as it was
//   vitalsnet <ssid>TAB<pass>  the network "vitals on" joins when none is given
//                          (saved, kept apart from join's); "vitalsnet forget" clears it
//   help
#pragma once
#include <stdint.h>
#include <stdlib.h>

#define SENSE_BAUD          115200
#define SENSE_LINE_HZ       10
#define SENSE_LINE_MAX      160

// The same lines and commands over the Tab5's OWN radio (the ESP32-C6 behind
// ESP-Hosted, src/radio/sense_c6.cpp), when no Monster C5 is on the bus:
// each line is one ESP-Hosted custom-message event, each command one
// custom message. IDs sit beside SquachWatch's sniff extension (0x5100-0x5201)
// in the same C6 image, so one radio image serves both apps.
#define SENSE_CMD_TEXT      0x5110   // tablet -> radio: one command line, no newline
#define SENSE_EVT_LINE      0x5210   // radio -> tablet: one $S/$T/$V/$I/# line, no newline
#define SENSE_CMD_MAX       96
// The C6 image's hello tag (SquachWatch's CMD_HELLO 0x5103 -> EVT_HELLO 0x5181):
// "squach-sniff 1" sniffs only; "squach-sniff 2" sniffs and senses.
#define SENSE_C6_HELLO_TAG  "squach-sniff 2"

// $S state
#define SENSE_STATE_CAL     0   // calibrating: hold still
#define SENSE_STATE_LOWDATA 1   // too few samples or too little trust to say
#define SENSE_STATE_STILL   2
#define SENSE_STATE_POSSIBLE 3
#define SENSE_STATE_MOTION  4

// $S mode
#define SENSE_MODE_OFF      0
#define SENSE_MODE_PARK     1   // promiscuous on a channel
#define SENSE_MODE_STA      2   // joined a network, pinging the gateway
#define SENSE_MODE_JOINING  3

// Where the radio sits on the Tab5. The M5MonsterC5 plugs into the M5-Bus
// (the long rear connector), which carries the P4's UART0 pins; on the C5
// side that is its UART0 (TX 11, RX 12), the pins JanOS uses too.
#define SENSE_TAB5_TX       37
#define SENSE_TAB5_RX       38
#define SENSE_C5_TX         11
#define SENSE_C5_RX         12

// Parsed from a $S line. Everything the screen needs.
struct SenseReport {
  uint32_t upMs;
  uint8_t  score, state, rel, cal, targets, mode, amp, phase, corr, band;
  uint16_t rate, live, chan;
  int8_t   rssi;
  uint32_t drop;
  bool     presence;
};

// One $T line: a transmitter the radio is modelling.
struct SenseTarget {
  uint8_t  idx;
  uint16_t tag;
  int8_t   rssi;
  uint16_t rate;
  uint8_t  score, cal, rel;
};

static inline bool senseParseTarget(const char* line, SenseTarget& out) {
  if (!line || line[0] != '$' || line[1] != 'T' || line[2] != ',') return false;
  char* end;
  const char* p = line + 3;
  long idx = strtol(p, &end, 10); if (end == p || *end != ',') return false; p = end + 1;
  long tag = strtol(p, &end, 16); if (end == p || *end != ',') return false; p = end + 1;
  long v[5]; int n = 0;
  while (n < 5) { v[n] = strtol(p, &end, 10); if (end == p) break; n++; if (*end != ',') break; p = end + 1; }
  if (n < 5 || idx < 0 || idx > 3) return false;
  out.idx = (uint8_t)idx; out.tag = (uint16_t)tag; out.rssi = (int8_t)v[0]; out.rate = (uint16_t)v[1];
  out.score = (uint8_t)v[2]; out.cal = (uint8_t)v[3]; out.rel = (uint8_t)v[4];
  return true;
}

// One $V line: a frame of the chest signal.
struct SenseVitals {
  uint32_t upMs;
  int32_t  c1, c2;
  uint8_t  n, q;
};

static inline bool senseParseVitals(const char* line, SenseVitals& out) {
  if (!line || line[0] != '$' || line[1] != 'V' || line[2] != ',') return false;
  long v[5]; int n = 0; const char* p = line + 3; char* end;
  while (n < 5) { v[n] = strtol(p, &end, 10); if (end == p) break; n++; if (*end != ',') break; p = end + 1; }
  if (n < 5) return false;
  out.upMs = (uint32_t)v[0]; out.c1 = (int32_t)v[1]; out.c2 = (int32_t)v[2]; out.n = (uint8_t)v[3]; out.q = (uint8_t)v[4];
  return true;
}

// Parses one $S line (without the newline) into out; false if it is not one.
// Lives in the header so both firmwares compile the same parser.
static inline bool senseParseLine(const char* line, SenseReport& out) {
  if (!line || line[0] != '$' || line[1] != 'S' || line[2] != ',') return false;
  long v[17];
  int n = 0;
  const char* p = line + 3;
  while (n < 17) {
    char* end;
    long x = strtol(p, &end, 10);
    if (end == p) break;
    v[n++] = x;
    if (*end != ',') break;
    p = end + 1;
  }
  if (n < 16) return false;
  out.upMs = (uint32_t)v[0];  out.score = (uint8_t)v[1];  out.state = (uint8_t)v[2];
  out.rate = (uint16_t)v[3];  out.chan = (uint16_t)v[4];  out.band = (uint8_t)v[5];
  out.rssi = (int8_t)v[6];    out.rel = (uint8_t)v[7];    out.cal = (uint8_t)v[8];
  out.targets = (uint8_t)v[9]; out.mode = (uint8_t)v[10]; out.amp = (uint8_t)v[11];
  out.phase = (uint8_t)v[12]; out.corr = (uint8_t)v[13];  out.live = (uint16_t)v[14];
  out.drop = (uint32_t)v[15];
  out.presence = n >= 17 ? v[16] != 0 : out.state == SENSE_STATE_MOTION;
  return true;
}
