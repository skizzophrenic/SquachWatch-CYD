// SquachWatch-CYD PC emulator — stand-in DetectionEngine + SdLog.
//
// This replaces src/detection.cpp and src/sd_log.cpp in the sim build.
// The real ones are ~900 lines wired directly into WiFi.h, esp_wifi.h,
// NimBLEDevice.h, esp_bt.h and SD.h; faithfully stubbing that whole
// surface would be a large side quest on its own, and none of it
// affects what the emulator exists to show -- how the UI *renders*.
//
// So: same class from the same header (every ui_*.cpp still takes the
// real `const DetectionEngine&`, unchanged), backed here by plain
// in-memory data with no radios behind it. Scanning, signature
// matching, watch/hunt alerting and SD logging are all inert. What the
// screens read back -- log entries, per-type counts, lifetime totals,
// watch/hunt labels and RSSI history -- is whatever seed() puts there.
//
// The trade this makes: the emulator will faithfully show you a LOG
// screen full of detections, but it is NOT exercising the real matching
// logic that decides what counts as a detection in the first place.
#include "detection.h"
#include <cstring>
#if defined(CYD_GPS)
#include "wardrive.h"
#include "wifi_auth.h"
#include <Arduino.h>
#include <cstdio>
#endif

// ---- SdLog: no card, ever -------------------------------------------
bool SdLog::begin() { _ready = false; return false; }
void SdLog::logEvent(const Detection&) {}
void SdLog::tick() {}
void SdLog::wipe() {}
void SdLog::openDaily() {}

// ---- DetectionEngine -------------------------------------------------
#if SQUACH_MESH
// The emulator has no radio, so the probe has nothing to count. Stubs keep
// the diagnostics screen renderable there without an #if around every row.
namespace MeshProbe {
    void  begin() {}
    void  tick(uint32_t) {}
    void  noteAdvert() {}
    Stats stats() { return Stats{}; }
    bool  concluded() { return false; }
}

// Mesh itself is no longer stubbed: src/mesh.cpp is compiled as-is, and
// sim/meshsim.cpp stands in for the radio underneath it.
#endif

bool DetectionEngine::init() { return true; }
// The LOG DUMP console command: no SD log here, so nothing to print.
void logDump() {}
#if defined(CYD_GPS)
// The emulator has no radio, so these stand in for what the WiFi and
// Bluetooth callbacks would report to wardriving: four networks and two
// tags, once a second of millis().
void DetectionEngine::loop() {
    static uint32_t last = 0;
    const uint32_t now = millis();
    if (last && now - last < 1000) return;
    last = now ? now : 1;
    static const uint8_t CH[4] = { 1, 6, 11, 6 };
    for (uint8_t i = 0; i < 4; i++) {
        const uint8_t mac[6] = { 0x02, 0x53, 0x51, 0x00, 0x00, (uint8_t)(i + 1) };
        char ssid[12];
        snprintf(ssid, sizeof ssid, "SimNet-%u", (unsigned)(i + 1));
        // Three WPA2 networks and one open one.
        const uint16_t auth = i < 3 ? (uint16_t)(WifiAuth::ESS | WifiAuth::PRIV | WifiAuth::RSN | WifiAuth::PSK | WifiAuth::CCMP)
                                    : (uint16_t)WifiAuth::ESS;
        Wardrive::noteWifi(mac, ssid, auth, CH[i], (int8_t)(-58 - i));
    }
    for (uint8_t i = 0; i < 2; i++) {
        const uint8_t mac[6] = { 0x02, 0x53, 0x51, 0x00, 0x01, (uint8_t)(i + 1) };
        Wardrive::noteBle(mac, "SimTag", (int8_t)(-62 - i), true, 0x004C);
    }
}
#else
void DetectionEngine::loop() {}
#endif
// No radio, so nothing to restart and nothing freed.
ScanFlushStats scanFlushStats() { return ScanFlushStats{ 0, 0, 0 }; }
BootHeap bootHeap()             { return BootHeap{ 0, 0, 0, 0 }; }
bool     scanPassiveNow()       { return false; }
uint32_t advertsDropped()       { return 0; }
uint32_t advertRate()           { return 0; }
uint32_t wifiFramesSeen()       { return 0; }
uint32_t advertsSeen()          { return 0; }
static const volatile uint32_t s_kinds0[5] = { 0, 0, 0, 0, 0 };
const volatile uint32_t* advertKinds() { return s_kinds0; }
void     setScanWindow(uint8_t) {}
void     setScanWindowBase(uint8_t) {}
void     setScanBoost(bool) {}
void     setScanInterval(uint16_t, uint8_t) {}
void     setScanPin(uint8_t)    {}

void DetectionEngine::clearLog() {
    _logCount = 0;
    _logHead = 0;
    _latest = nullptr;
    memset(_typeCounts, 0, sizeof(_typeCounts));
}

const Detection* DetectionEngine::logAt(uint8_t idx) const {
    if (idx >= _logCount) return nullptr;
    // Newest first, matching the real ring-buffer walk order the LOG
    // screen's scrolling assumes.
    uint8_t slot = (uint8_t)((_logHead + LOG_CAP - 1 - idx) % LOG_CAP);
    return &_log[slot];
}

void DetectionEngine::resetLifetime() {
    _lifetimeTotal = 0;
    memset(_typeCounts, 0, sizeof(_typeCounts));
}

void DetectionEngine::pushLog(const Detection& d) {
    _log[_logHead] = d;
    _latest = &_log[_logHead];
    _latestNew = true;   // the stand-in only ever pushes; nothing here reactivates
    _logHead = (uint8_t)((_logHead + 1) % LOG_CAP);
    if (_logCount < LOG_CAP) _logCount++;
    if ((uint8_t)d.type < (uint8_t)DetectionType::COUNT) {
        _typeCounts[(uint8_t)d.type]++;
        _lifetimeByType[(uint8_t)d.type]++;   // the DEX and the outfits read this
    }
    _lifetimeTotal++;
}

// Radio-fed entry points: inert here, nothing calls them in the sim.
void DetectionEngine::postWiFi(const uint8_t*, int8_t, uint8_t, const char*, bool, bool, bool) {}
void DetectionEngine::postDeauth(const uint8_t*, int8_t, uint8_t, bool) {}
void DetectionEngine::postBle(Detection d) { pushLog(d); }

// Not a stub. Everything else in this file is inert because it would need
// a radio, but the Remote ID decoder is pure arithmetic over a byte
// buffer -- so the emulator runs the REAL one, and the drone info panel it
// renders is showing genuinely decoded values rather than a mock-up.
void DetectionEngine::mergeRemoteId(const uint8_t* mac, const uint8_t* payload,
                                    uint8_t len) {
    if (!mac || !payload) return;
    if (memcmp(mac, _ridMac, 6) != 0) {
        RemoteId::reset(_rid);
        memcpy(_ridMac, mac, 6);
    }
    RemoteId::merge(payload, len, _rid, millis());
}
void DetectionEngine::postBtClassic(Detection d) { pushLog(d); }
void DetectionEngine::postRawBle(RawBleResult r) {
    if (_rawBleCount < RAW_BLE_CAP) _rawBle[_rawBleCount++] = r;
}

// Raw scanner: "done" at once, with a room's worth of devices so the list
// and its phone labels can be seen. Each is made from advert bytes of the
// kind the real ones send, through the firmware's own rawBleKindOf().
static RawBleResult simRaw(uint8_t tag, int8_t rssi, const char* name, const uint8_t* adv, size_t n) {
    RawBleResult r;
    memset(&r, 0, sizeof r);
    const uint8_t mac[6] = { 0x5A, 0x11, 0x5C, 0x00, 0x00, tag };
    memcpy(r.mac, mac, 6);
    r.rssi = r.prev = rssi;
    if (name) strncpy(r.name, name, sizeof r.name - 1);
    r.kind = rawBleKindOf(adv, n);
    return r;
}
void DetectionEngine::startRawBleScan() {
    _rawBleCount = 0;
    static const uint8_t IPHONE[]  = { 0x02, 0x01, 0x1A, 0x0A, 0xFF, 0x4C, 0x00, 0x10, 0x05, 0x03, 0x1C, 0x12, 0x34, 0x56 };
    static const uint8_t AIRPODS[] = { 0x0B, 0xFF, 0x4C, 0x00, 0x07, 0x06, 0x01, 0x0E, 0x20, 0x55, 0xAA, 0x01 };
    static const uint8_t ANDROID[] = { 0x02, 0x01, 0x06, 0x03, 0x03, 0xF3, 0xFE, 0x05, 0x16, 0xF3, 0xFE, 0x01, 0x02 };
    static const uint8_t GALAXY[]  = { 0x02, 0x01, 0x06, 0x07, 0xFF, 0x75, 0x00, 0x42, 0x09, 0x01, 0x02 };
    static const uint8_t FINDMY[]  = { 0x05, 0xFF, 0x4C, 0x00, 0x12, 0x00 };
    static const uint8_t PLAIN[]   = { 0x02, 0x01, 0x06 };
    postRawBle(simRaw(1, -54, nullptr, AIRPODS, sizeof AIRPODS));
    postRawBle(simRaw(2, -61, nullptr, IPHONE, sizeof IPHONE));
    postRawBle(simRaw(3, -66, "Flip 5", PLAIN, sizeof PLAIN));
    postRawBle(simRaw(4, -72, nullptr, ANDROID, sizeof ANDROID));
    postRawBle(simRaw(5, -77, nullptr, GALAXY, sizeof GALAXY));
    postRawBle(simRaw(6, -81, nullptr, FINDMY, sizeof FINDMY));
    postRawBle(simRaw(7, -88, nullptr, PLAIN, sizeof PLAIN));
}
bool DetectionEngine::rawBleScanDone() const { return true; }
const RawBleResult* DetectionEngine::rawBleAt(uint8_t idx) const {
    return idx < _rawBleCount ? &_rawBle[idx] : nullptr;
}
void DetectionEngine::startRawWifiScan() {}
bool DetectionEngine::rawWifiScanDone() const { return true; }
uint8_t DetectionEngine::rawWifiCount() const { return 0; }
const char* DetectionEngine::rawWifiSsid(uint8_t) const { return ""; }
int8_t DetectionEngine::rawWifiRssi(uint8_t) const { return 0; }
uint8_t DetectionEngine::rawWifiChannel(uint8_t) const { return 0; }
bool DetectionEngine::rawWifiOpen(uint8_t) const { return false; }
const uint8_t* DetectionEngine::rawWifiBssid(uint8_t) const { return nullptr; }
void DetectionEngine::stopRawScan() {}
void DetectionEngine::startUpdateRadio() {}
void DetectionEngine::stopUpdateRadio() {}

// ---- watch / hunt ----------------------------------------------------
void DetectionEngine::watchBle(const uint8_t* mac, const char* name) {
    _watchKind = WatchKind::BLE;
    if (mac) memcpy(_watchMac, mac, 6);
    snprintf(_watchLabel, sizeof(_watchLabel), "%s", name ? name : "");
    _watchRssiCount = 0;
}
void DetectionEngine::watchWifi(const uint8_t* bssid, const char* ssid) {
    _watchKind = WatchKind::WIFI;
    if (bssid) memcpy(_watchMac, bssid, 6);
    snprintf(_watchLabel, sizeof(_watchLabel), "%s", ssid ? ssid : "");
    _watchRssiCount = 0;
}
void DetectionEngine::clearWatch() { _watchKind = WatchKind::NONE; _watchLabel[0] = 0; _watchRssiCount = 0; }
int8_t DetectionEngine::watchRssiAt(uint8_t idx) const {
    if (idx >= _watchRssiCount) return 0;
    uint8_t slot = (uint8_t)((_watchRssiHead + WATCH_RSSI_CAP - _watchRssiCount + idx) % WATCH_RSSI_CAP);
    return _watchRssiHist[slot];
}
bool DetectionEngine::watchHitPending() { bool f = _watchHitFlag; _watchHitFlag = false; return f; }
// Records the signal when the address is the watched one, as the firmware
// does, so the emulator's watch alert has a trend to draw.
void DetectionEngine::checkWatchBle(const uint8_t* mac, int8_t rssi) {
    if (mac && isWatched(mac, true)) recordWatchRssi(rssi);
}
void DetectionEngine::checkWatchWifi(const uint8_t*, int8_t) {}
void DetectionEngine::recordWatchRssi(int8_t rssi) {
    _watchRssiHist[_watchRssiHead] = rssi;
    _watchRssiHead = (uint8_t)((_watchRssiHead + 1) % WATCH_RSSI_CAP);
    if (_watchRssiCount < WATCH_RSSI_CAP) _watchRssiCount++;
}

void DetectionEngine::huntBle(const uint8_t* mac, const char* name, uint8_t rawKind) {
    huntSwapReset(rawKind);
    _huntKind = WatchKind::BLE;
    if (mac) memcpy(_huntMac, mac, 6);
    snprintf(_huntLabel, sizeof(_huntLabel), "%s", name ? name : "");
    _huntRssiCount = 0;
}
void DetectionEngine::huntWifi(const uint8_t* bssid, const char* ssid) {
    huntSwapReset(0);
    _huntKind = WatchKind::WIFI;
    if (bssid) memcpy(_huntMac, bssid, 6);
    snprintf(_huntLabel, sizeof(_huntLabel), "%s", ssid ? ssid : "");
    _huntRssiCount = 0;
}
void DetectionEngine::clearHunt() { huntSwapReset(0); _huntKind = WatchKind::NONE; _huntLabel[0] = 0; _huntRssiCount = 0; }
int8_t DetectionEngine::huntRssiAt(uint8_t idx) const {
    if (idx >= _huntRssiCount) return 0;
    uint8_t slot = (uint8_t)((_huntRssiHead + WATCH_RSSI_CAP - _huntRssiCount + idx) % WATCH_RSSI_CAP);
    return _huntRssiHist[slot];
}
// The emulator has no radio, but a test can hand this samples by hand -- the
// hunt screen's CAUGHT state is checked that way (main_sim.cpp, "hunt" --pose 1).
void DetectionEngine::checkHuntBle(const uint8_t* mac, int8_t rssi) {
    if (_huntKind != WatchKind::BLE || memcmp(mac, _huntMac, 6) != 0) return;
    _huntSeenMs = millis(); _huntSeenRssi = rssi;
    recordHuntRssi(rssi);
}
void DetectionEngine::checkHuntWifi(const uint8_t*, int8_t) {}
void DetectionEngine::recordHuntRssi(int8_t rssi) {
    _huntRssiHist[_huntRssiHead] = rssi;
    _huntRssiHead = (uint8_t)((_huntRssiHead + 1) % WATCH_RSSI_CAP);
    if (_huntRssiCount < WATCH_RSSI_CAP) _huntRssiCount++;
}

// ---- housekeeping the real engine runs from loop() -------------------
void DetectionEngine::processWiFiQ() {}
void DetectionEngine::processDeauthQ() {}
void DetectionEngine::expireStale() {}
void DetectionEngine::hopChannel() {}
void DetectionEngine::decayChannelActivity() {}

// The radios' rest, which the T-Deck build's LoRa duty cycle asks for (and
// the watch's). Nothing here to rest: they report awake.
bool DetectionEngine::restRadios(bool) { return false; }
void DetectionEngine::wakeRadios() {}
bool DetectionEngine::radiosResting() { return false; }

// The watch build's boot report and its count of new Bluetooth arrivals
// (detection.cpp has the real ones; no radios here, so none arrive).
char g_bootRadioLine[192] = "";
uint32_t bleArrivals() { return 0; }
void bleArrivalsRoll(uint32_t) {}
