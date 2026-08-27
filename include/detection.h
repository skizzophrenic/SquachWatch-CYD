// SquachWatch-CYD — DetectionEngine public API
#pragma once
#include "state.h"
#include "sd_log.h"
#include <Preferences.h>

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

    // SD log helper accessor.
    SdLog& sd() { return _sd; }

private:
    static const uint8_t  LOG_CAP       = 32;
    static const uint8_t  WIFI_Q_CAP    = 8;
    static const uint32_t STALE_MS      = 60000;
    static const uint32_t ALERT_GRACE_MS= 200;

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

    void pushLog(const Detection& d);
    void processWiFiQ();
    void expireStale();
};
