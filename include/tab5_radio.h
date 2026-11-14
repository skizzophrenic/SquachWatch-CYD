// SquachWatch-CYD — the M5Stack Tab5's radio: an ESP32-C6 over SDIO.
//
// The ESP32-P4 has no radio of its own. The Tab5's ESP32-C6 runs Espressif's
// ESP-Hosted slave with our sniff extension on top (tools/tab5-c6/
// squach_sniff.c), and the P4 talks to it over SDIO through the ESP-Hosted
// host library the Arduino core bundles. Stock ESP-Hosted carries WiFi
// connections, not promiscuous mode, so the sniffer is our own protocol on
// ESP-Hosted's custom-message channel: the host switches sniffing on, hands
// the C6 the channel list and dwell, and the C6 forwards every management
// frame the detector reads (beacon, probe request, deauth, disassoc) whole,
// and data frames as their 24-byte header, one per source per two seconds.
//
// This module owns the link: power to the C6 (a bit on the second PI4IOE
// expander, which M5GFX programs at panel init and this leaves on), the SDIO
// pins, the handshake, and a queue of frames the RPC receive task fills and
// the loop drains. Nothing in here parses a frame; detection.cpp does that
// with the same code every other board runs on its own radio.
//
// Bluetooth: bleBegin() switches on the C6's controller, whose HCI rides the
// same link; the NimBLE host runs on the P4 and detection.cpp drives it
// through esp-nimble-cpp, the same scanner every other board runs.
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace Tab5Radio {

// A forwarded frame, as the C6 sends it: the packed header, then data[] of
// dataLen bytes. sigLen is the frame's length on air (the detector's
// sig_len); dataLen is what came over the link (whole up to 600 bytes for a
// management frame, 24 for a data frame).
static const uint16_t FRAME_MAX = 600;
struct Frame {
    int8_t   rssi;
    uint8_t  channel;
    uint8_t  type;       // 0 management, 2 data
    uint8_t  subtype;
    uint16_t sigLen;
    uint16_t dataLen;
    uint8_t  data[FRAME_MAX];
};

struct Stats {
    // The C6's own counters, once a second while sniffing (EVT_STATS).
    uint32_t seen, forwarded, dropped;
    uint8_t  channel;
    uint32_t statsAt;        // millis() of the last EVT_STATS, 0 if none yet
    // Ours.
    uint32_t received;       // frames that reached the queue
    uint32_t queueFull;      // frames lost because the loop was behind
    uint32_t acks;           // EVT_ACK replies seen
    uint16_t lastAckCmd;
    int32_t  lastAckStatus;
};

// Power the C6, configure the SDIO pins, bring the link up (one retry), and
// say hello. False when the link did not come up or the C6 does not run the
// sniff extension; the console says which. Safe to call again: a second call
// on a link that is up returns true at once.
bool begin();
bool up();
// The C6's Bluetooth controller, after begin(). False when the link is down
// or the slave refused; the console says which. Call before NimBLE starts.
bool bleBegin();
// The slave's hello tag ("squach-sniff 1"), or "" before a hello was answered.
const char* tag();

// The sniff protocol. Each returns false when the link is down or the send
// was refused. The ACK arrives later, on the receive task (see Stats).
bool sniff(bool on);
bool setChannel(uint8_t ch);                                  // 1-14, stops hopping
bool hop(const uint8_t* chans, uint8_t n, uint16_t dwellMs);  // n = 0 stops hopping
bool hopStop();

// The oldest frame waiting, copied out; false when the queue is empty.
bool next(Frame& out);
// How many are waiting now.
uint16_t pending();

Stats stats();

}  // namespace Tab5Radio
