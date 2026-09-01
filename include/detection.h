// SquachWatch-CYD — DetectionEngine public API
#pragma once
#include "state.h"
#include "sd_log.h"
#include <Preferences.h>

// A single unfiltered BLE sighting from the manual raw scanner (see
// startRawBleScan() below) -- every device seen, not just ones
// matching a known surveillance signature like the log's Detection
// does.
struct RawBleResult {
    uint8_t mac[6];
    int8_t  rssi;
    char    name[24];   // empty if the device didn't advertise one
};

class DetectionEngine {
public:
    bool     init();
    void     loop();
    void     clearLog();
    uint8_t  logCount() const { return _logCount; }
    const Detection* logAt(uint8_t idx) const;     // 0 = newest
    const Detection* latest() const { return _latest; }
    uint16_t countByType(DetectionType t) const { return _typeCounts[(uint8_t)t]; }

    // Lifetime total across reboots (persisted to NVS), unlike the
    // live _typeCounts above which decay when a detection goes stale.
    uint32_t lifetimeTotal() const { return _lifetimeTotal; }

    // Settings-menu "reset stats" action: zeroes the persisted lifetime
    // total and the live per-type counters. Does not touch the log
    // itself — that's clearLog()'s job.
    void resetLifetime();

    // Called from the promiscuous WiFi Rx callback (IRAM_ATTR context).
    // Posts a 6-byte MAC + RSSI for later processing in loop(). ssid is
    // optional (nullptr/empty for probe requests and data frames, which
    // don't carry one) — beacon frames pass the AP's SSID so it can be
    // matched against the SSID-prefix table as a fallback when the OUI
    // itself doesn't match anything (e.g. an Axon/Flock unit in pairing
    // mode, running on a WiFi module OUI we don't otherwise recognize).
    void IRAM_ATTR postWiFi(const uint8_t* mac, int8_t rssi, uint8_t channel,
                            const char* ssid = nullptr);

    // Called from the BLE scan callback when a hit is found.
    void postBle(Detection d);

    // Called from the BT Classic inquiry callback when a name match hits.
    void postBtClassic(Detection d);

    // Called from the BLE scan callback while a raw BLE scan is active
    // (see startRawBleScan()) -- every advertisement, not just known
    // signatures.
    void postRawBle(RawBleResult r);

    // ---- Manual raw scanner (CLEAR screen's SCAN button picker) -----
    // Pauses the continuous signature-matched scan above entirely and
    // dedicates the radio to a single focused sweep -- only one of
    // these (or the continuous scan) is ever active at a time, which
    // is what keeps this from needing its own separate memory budget
    // on top of the continuous scan's already-measured heap usage.
    //
    // BLE stays on the same always-running NimBLE scan (just points its
    // callback at postRawBle() instead of the signature matcher) for a
    // fixed focused dwell; WiFi runs a real one-shot WiFi.scanNetworks()
    // AP sweep instead of the continuous promiscuous-frame sniffer.
    // stopRawScan() ends whichever is active and resumes the continuous
    // scan; safe to call even when neither is running.
    void     startRawBleScan();
    bool     rawBleScanDone() const;
    uint8_t  rawBleCount() const { return _rawBleCount; }
    const RawBleResult* rawBleAt(uint8_t idx) const;   // insertion order, nullptr if idx is out of range

    void     startRawWifiScan();
    bool     rawWifiScanDone() const;
    uint8_t  rawWifiCount() const;
    const char* rawWifiSsid(uint8_t idx) const;        // "" if idx is out of range
    int8_t   rawWifiRssi(uint8_t idx) const;
    uint8_t  rawWifiChannel(uint8_t idx) const;
    bool     rawWifiOpen(uint8_t idx) const;            // true = no encryption

    void     stopRawScan();

    // SD log helper accessor.
    SdLog& sd() { return _sd; }

    // Rough per-channel activity level for the spectrum-waterfall
    // background (0..100, quiet..very active) — not a real dBm
    // reading, just how strong/recent the loudest frame seen on that
    // channel during the last hop cycle was, decaying over time.
    // channel is 1..13; anything else returns 0.
    uint8_t channelActivity(uint8_t channel) const {
        return (channel >= 1 && channel <= 13) ? _channelActivity[channel] : 0;
    }

private:
    static const uint8_t  LOG_CAP       = 32;
    static const uint8_t  WIFI_Q_CAP    = 8;
    static const uint32_t STALE_MS      = 60000;
    static const uint32_t ALERT_GRACE_MS= 200;
    static const uint8_t  RAW_BLE_CAP   = 20;

    // Raw (unfiltered) BLE scan results -- see startRawBleScan(). Not a
    // ring buffer like _log: entries are looked up by MAC and updated
    // in place, so this reads as "everything currently visible" rather
    // than a chronological history.
    RawBleResult _rawBle[RAW_BLE_CAP];
    uint8_t      _rawBleCount = 0;

    struct WiFiQEntry {
        uint8_t mac[6];
        int8_t  rssi;
        uint8_t channel;
        char    ssid[33];  // empty string if none (see postWiFi)
    };

    // WiFi mailbox (filled in IRAM, drained in loop)
    volatile WiFiQEntry _wifiQ[WIFI_Q_CAP];
    volatile uint8_t    _wifiQHead = 0;
    volatile uint8_t    _wifiQTail = 0;

    // Detection log
    Detection  _log[LOG_CAP];
    uint8_t    _logCount = 0;            // number of valid entries (<= LOG_CAP)
    uint8_t    _logHead  = 0;            // next slot to write
    Detection* _latest   = nullptr;      // pointer into _log or null
    uint32_t   _latestChangeMs = 0;
    DetectionType _lastAlertType = DetectionType::UNKNOWN;

    // Live counters (one per DetectionType)
    uint16_t _typeCounts[(uint8_t)DetectionType::COUNT] = {0};

    SdLog       _sd;
    Preferences _prefs;
    uint32_t    _lifetimeTotal = 0;

    // Promiscuous mode only ever receives on whatever channel the
    // radio is currently tuned to — without actively hopping, the
    // sniffer stays parked on one channel and misses anything
    // transmitting on the other 12. See hopChannel().
    uint8_t     _wifiChannel = 1;
    uint32_t    _lastHopMs   = 0;

    // Index 1..13; 0 is unused. Fed from every captured mgmt/data
    // frame in processWiFiQ() (not just ones that match a known
    // signature) so it reflects real ambient channel activity, then
    // decayed over time in loop().
    uint8_t     _channelActivity[14] = {0};
    uint32_t    _channelLastMs[14]   = {0};

    void pushLog(const Detection& d);
    void processWiFiQ();
    void expireStale();
    void hopChannel();
    void decayChannelActivity();
};
