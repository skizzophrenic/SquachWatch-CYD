// SquachWatch-CYD -- HUMAN SENSING <-> Ragnar RuSense link (SENSE_CYD_APP).
//
// RuSense is Ragnar's camera-free sensing: a mesh of ESP32-S3/C6 CSI nodes
// feeding a sensing server on the Pi. This links the board's own HUMAN SENSING
// to it, both ways, over the network the board has already joined to sense:
//   PULL  GET  /api/cyd/rusense   the mesh's verdict (present, people,
//                                 confidence, breathing / heart rate, nodes up)
//                                 shown beside the board's own
//   PUSH  POST /api/cyd/presence  the board's verdict (state, score, trust) to
//                                 Ragnar, as an extra presence source
// Both carry "Authorization: Bearer <token>", a CYD device token made on Ragnar
// (Mesh > CYD Nodes). Ragnar lets that token reach only the CYD endpoints.
//
// The HTTP runs on its own small task: the 50 Hz pings run on the loop and a
// request there would stall them. The task exists only while HUMAN SENSING is
// up and the link is set, like the engine's other buffers.
//
// Console (SENSE RAGNAR ...):
//   SENSE RAGNAR <host[:port]> <token>   set Ragnar and the token (port 8000 if none)
//   SENSE RAGNAR LINK off|pull|push|both
//   SENSE RAGNAR OFF                     forget host and token
//   SENSE RAGNAR STAT                    what the link is doing
#pragma once
#include <stdint.h>
#include "sense_cyd.h"

#if defined(SENSE_CYD_APP)
namespace SenseRagnar {

enum : uint8_t { LINK_OFF = 0, LINK_PULL = 1, LINK_PUSH = 2, LINK_BOTH = 3 };

// The mesh's verdict, as last read. -1 means "the server did not say".
struct View {
    bool     ok;          // Ragnar answered and RuSense is live
    bool     present;
    uint8_t  people;
    int8_t   conf;        // 0-100, -1 none
    bool     model;       // a trained model is loaded (confidence is calibrated)
    bool     vote;        // this board's vote is counting on Ragnar
    int16_t  hr;          // beats a minute, -1 none
    int16_t  br10;        // breaths a minute x10, -1 none
    int8_t   up, nodes;   // CSI nodes streaming / known, -1 none
    int32_t  sightAgo;    // seconds since the last sighting ended, 0 open, -1 none
    int32_t  sightS;      // how long it lasted
    char     motion[12];  // "absent", "present_still", "active"...
};

void start();                          // HUMAN SENSING opened (senseCydStart)
void stop();                           // closed (senseCydStop)
void report(const SenseReport& r);     // the engine's status, 10 a second
void command(char* arg);               // "SENSE RAGNAR <arg>"

uint8_t linkMode();                    // LINK_*, from Settings
bool    configured();                  // host and token set, link not OFF
bool    pulling();                     // configured with PULL
// The last view and its age in ms; false if none yet or none for 6 s.
bool    view(View& out, uint32_t now);
// What the link is doing, for the screen: "", "OFFLINE", "NO TOKEN"...
const char* trouble();

}  // namespace SenseRagnar
#endif
