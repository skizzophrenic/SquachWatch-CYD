// SquachWatch-CYD — SHARE LOGS: the logs to a computer, over WiFi.
//
// The board joins a saved network (the WIFI NETWORKS list: the one marked USE
// first, then the rest) and serves a small web page on port 80 with every log
// it keeps: the SD card's daily squachwatch-*.log files and any wigle-*.csv,
// and the black box as blackbox.csv -- the same text BLACKBOX prints on the
// console. Open http://<the address on screen>/ in a browser on the same
// network, or `curl.exe -u squach:<code> -O http://<address>/blackbox.csv`.
//
// WHAT GUARDS IT. The board has to be unlocked to start it (the PIN, when one
// is set), and every request needs user "squach" and the six-digit code the
// screen shows, which is new every time sharing starts. Five wrong codes stop
// the share. It stops on its own after IDLE_MS with no request, after MAX_MS
// whatever happens, and the moment the board locks. Plain HTTP: anyone on the
// network can read what is sent, so share on a network you trust.
//
// WHAT IT COSTS. The WiFi radio cannot sniff and serve at once, so detection
// is paused while sharing, exactly as for an update over WiFi (the caller
// calls DetectionEngine::startUpdateRadio()/stopUpdateRadio()). Files are
// sent from loop(), so the screen holds still while a big one goes out.
//
// Compiled only with -DSQW_LOG_SHARE=1 (cyd, cyd-ili9341, freenove32, cyd35-fast).
#pragma once
#include <stdint.h>

namespace LogShare {

enum class State : uint8_t {
    OFF = 0,
    JOINING,      // WiFi.begin() on a saved network; see network()
    SERVING,      // joined, the server up: address() and code() are live
    FAILED,       // see failText(); STOP leaves
};

static const uint32_t IDLE_MS = 5UL * 60UL * 1000UL;    // no request for this long: stop
static const uint32_t MAX_MS  = 20UL * 60UL * 1000UL;   // and never longer than this

// Starts joining. False, with whyNot() saying why, when the board is locked,
// no network is saved, or a WiFi update holds the radio. The caller pauses
// detection first.
bool        begin(uint32_t now);
// Stops the server and leaves the network. The caller resumes detection.
void        end();
void        tick(uint32_t now);

bool        active();          // anything but OFF
State       state();
const char* whyNot();
const char* failText();
const char* network();         // the SSID being joined or used
const char* address();         // "192.168.1.50" once SERVING
const char* code();            // the six digits, once SERVING
uint16_t    requests();        // answered since begin()
uint32_t    secondsLeft(uint32_t now);   // until the idle or hard stop, whichever is sooner
bool        endedByTimeout();  // the last end was the timer's or the wrong codes', not STOP's

}  // namespace LogShare
