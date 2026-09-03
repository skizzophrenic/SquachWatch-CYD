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

// ---- SdLog: no card, ever -------------------------------------------
bool SdLog::begin() { _ready = false; return false; }
void SdLog::logEvent(const Detection&) {}
void SdLog::tick() {}
void SdLog::openDaily() {}

// ---- DetectionEngine -------------------------------------------------
bool DetectionEngine::init() { return true; }
void DetectionEngine::loop() {}

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
    _logHead = (uint8_t)((_logHead + 1) % LOG_CAP);
    if (_logCount < LOG_CAP) _logCount++;
    if ((uint8_t)d.type < (uint8_t)DetectionType::COUNT) _typeCounts[(uint8_t)d.type]++;
    _lifetimeTotal++;
}

// Radio-fed entry points: inert here, nothing calls them in the sim.
void DetectionEngine::postWiFi(const uint8_t*, int8_t, uint8_t, const char*) {}
void DetectionEngine::postDeauth(const uint8_t*, int8_t, uint8_t) {}
void DetectionEngine::postBle(Detection d) { pushLog(d); }
void DetectionEngine::postBtClassic(Detection d) { pushLog(d); }
void DetectionEngine::postRawBle(RawBleResult r) {
    if (_rawBleCount < RAW_BLE_CAP) _rawBle[_rawBleCount++] = r;
}

// Raw scanner: reports "done, nothing found" so the raw-scan screen
// renders its empty-result state rather than spinning forever.
void DetectionEngine::startRawBleScan() { _rawBleCount = 0; }
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
void DetectionEngine::checkWatchBle(const uint8_t*, int8_t) {}
void DetectionEngine::checkWatchWifi(const uint8_t*, int8_t) {}
void DetectionEngine::recordWatchRssi(int8_t rssi) {
    _watchRssiHist[_watchRssiHead] = rssi;
    _watchRssiHead = (uint8_t)((_watchRssiHead + 1) % WATCH_RSSI_CAP);
    if (_watchRssiCount < WATCH_RSSI_CAP) _watchRssiCount++;
}

void DetectionEngine::huntBle(const uint8_t* mac, const char* name) {
    _huntKind = WatchKind::BLE;
    if (mac) memcpy(_huntMac, mac, 6);
    snprintf(_huntLabel, sizeof(_huntLabel), "%s", name ? name : "");
    _huntRssiCount = 0;
}
void DetectionEngine::huntWifi(const uint8_t* bssid, const char* ssid) {
    _huntKind = WatchKind::WIFI;
    if (bssid) memcpy(_huntMac, bssid, 6);
    snprintf(_huntLabel, sizeof(_huntLabel), "%s", ssid ? ssid : "");
    _huntRssiCount = 0;
}
void DetectionEngine::clearHunt() { _huntKind = WatchKind::NONE; _huntLabel[0] = 0; _huntRssiCount = 0; }
int8_t DetectionEngine::huntRssiAt(uint8_t idx) const {
    if (idx >= _huntRssiCount) return 0;
    uint8_t slot = (uint8_t)((_huntRssiHead + WATCH_RSSI_CAP - _huntRssiCount + idx) % WATCH_RSSI_CAP);
    return _huntRssiHist[slot];
}
void DetectionEngine::checkHuntBle(const uint8_t*, int8_t) {}
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
