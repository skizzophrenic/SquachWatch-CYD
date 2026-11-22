// SquachWatch-CM0 -- wifi_mon: the USB adapter's monitor vif.
//
// Phase 2 of the port. The firmware's real detection.cpp drives the radio
// through the ESP32 API (radio_stub/); this file is the Linux half: it
// finds the adapter (a USB-attached Wi-Fi netdev, or SQUACH_WIFI_IF), puts
// it in monitor mode (a `sqwmon0` vif, or the interface itself when the
// driver will not take a second vif), reads raw 802.11 frames on AF_PACKET,
// and hands them to the promiscuous rx callback detection.cpp registered --
// the same callback shape, so the OUI/SSID/deauth-flood engine runs
// unchanged.
//
// Threading: the feed thread only reads, parses and queues. The firmware's
// callback runs on the UI thread, from wifiMonPump(), which main_linux calls
// before every loop(). That is the Tab5 board's shape (drainHostedFrames()
// runs the parser from loop()), and it means no firmware code ever runs on a
// second thread -- the firmware's rx rings are volatile-only SPSC queues
// written for the ESP32, with no barriers an aarch64 core would honour.
//
// Channel hopping: the firmware asks for a channel through
// esp_wifi_set_channel (the stub forwards it here); the feed thread tunes
// the monitor vif via nl80211 (generic netlink, NL80211_CMD_SET_WIPHY with
// the frequency in MHz, acked) and falls back to `iw dev <if> set freq`.
//
// App, not kiosk: wifiMonStop() deletes the vif (or restores the
// interface's original type and link state) and joins the thread, so an
// exited squachd leaves the adapter exactly as it found it. A start that
// fails half way undoes what it did before returning.
//
// Privileges: creating the vif needs CAP_NET_ADMIN, the packet socket
// CAP_NET_RAW. Run as root, or `make caps` (setcap on the binary): the
// start raises both into the ambient set so the `iw`/`ip` children inherit
// them.
#pragma once
#include <cstddef>
#include <cstdint>
#include "esp_wifi.h"

namespace cm0 {

// Find the adapter, create the monitor interface, open the packet socket,
// start the feed thread. Returns false when no adapter is present (or it
// will not go to monitor) -- the firmware then runs on its empty stub
// radio, exactly as in Phase 1, and nothing is left changed. Safe to call
// twice (the second call is a no-op).
bool wifiMonStart();

// Stop the feed thread, close the sockets, delete the monitor vif (or
// restore the converted interface). Idempotent.
void wifiMonStop();

// UI thread: deliver up to `maxFrames` queued frames to the registered
// callback. Returns how many were delivered.
size_t wifiMonPump(size_t maxFrames = 256);

// The four setter forwards from radio_stub.cpp. Registration happens
// during the firmware's setup(); frames are queued only while the thread
// is running and the stub's promiscuous flag is on.
void wifiMonRegisterRx(wifi_promiscuous_rx_cb_t cb);
void wifiMonSetChannel(uint8_t channel);
void wifiMonSetPromisc(bool on);
void wifiMonSetFilter(uint32_t mask);

// Whether the sniffer adapter's wiphy lists `channel` as enabled (present
// and not "(disabled)" under the current regulatory domain). Probed once
// from `iw phy <phyN> info`, cached; false for everything when no adapter
// is visible. The radio stub uses it for 5 GHz so the firmware's boot
// probe gets the per-channel answer the ESP32 driver would give.
bool wifiMonChannelUsable(uint8_t channel);

// ---- the passive network scan (the firmware's raw Wi-Fi screen) ----
//
// WiFi.scanNetworks(async, no hidden, passive, dwell) in the stub lands
// here: the feed thread visits every usable channel (2.4 GHz, and 5 GHz in
// a SQW_WIFI_5G build) for `dwellMs`, listening only -- no probe request is
// ever sent -- and lists the APs whose beacons / probe responses it heard.
struct WifiScanAp {
    uint8_t bssid[6];
    char    ssid[33];
    int8_t  rssi;      // strongest heard during the sweep, dBm
    uint8_t channel;   // DS Parameter Set IE, else the radiotap frequency
    bool    privacy;   // beacon Capability "Privacy" bit: false = open
};
// Start a sweep. False when no sniffer is running (the stub then reports
// a finished scan with no networks, as before).
bool wifiMonScanStart(uint16_t dwellMs);
// Arduino scanComplete() semantics: -1 running, -2 none started/cleared,
// else the number of networks (strongest first).
int wifiMonScanState();
// Copy up to `cap` results (only once the sweep is done).
size_t wifiMonScanResults(WifiScanAp* out, size_t cap);
// Abort a running sweep / drop the results (scanDelete()).
void wifiMonScanClear();

// ---- test hooks (wifi_mon_test.cpp) ----

// Push one radiotapped frame through the real parse -> filter -> queue
// path, then pump the queue on the calling thread. Returns the delivered
// type (0 mgmt / 2 data / 3 ctl) or -1 if dropped. `appliedChan` is the
// fallback channel when the radiotap header carries no frequency (0 = the
// last applied channel).
int wifiMonTestInject(const uint8_t* raw, size_t rawLen, uint8_t appliedChan);

// Feed one radiotapped frame to the scan table. Returns the table size
// after it, 0 when the frame was not a usable AP advertisement (wrong type,
// hidden SSID), -1 when the radiotap header was rejected.
int wifiMonTestScanAbsorb(const uint8_t* raw, size_t rawLen);
// Mark the sweep finished (sorts the table) so wifiMonScanResults reads it.
void wifiMonTestScanFinish();

// The `iw phy info` parser behind wifiMonChannelUsable, on a given text,
// so the test can pin it without an adapter. Fills `usable[256]`.
void wifiMonTestParsePhyInfo(const char* text, bool usable[256]);

// The nl80211 SET_WIPHY request bytes for (family, ifindex, mhz), so the
// test can pin the wire format. Returns the message length.
size_t wifiMonTestBuildSetFreq(uint8_t* out, size_t cap, uint16_t family,
                               int ifindex, uint32_t mhz);

}   // namespace cm0
