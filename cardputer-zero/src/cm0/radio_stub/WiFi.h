// SquachWatch-CM0 -- the Arduino WiFi class stub.
//
// Two calls in this build's compiled code: WiFi.scanDelete() (the raw
// scan's cleanup, detection.cpp:95) and the WiFi.mode(WIFI_OFF)/
// mode(WIFI_STA) pair the slim init falls back to (detection.cpp:975).
// The rest of the surface exists so a board-guarded caller compiles the
// same way it does on the device.
//
// The answers say "no network": OFF mode, idle status, zero networks.
// NTP/OTA over Wi-Fi (Phase 4's join logic) will find the same truth the
// device's own radio reports when it is not associated.
#pragma once
#include <cstdint>
#include <string>

// The raw scan's screen reads (detection.cpp, the non-hosted branch) use
// the ESP32 core's WString type under this name.
using String = std::string;

enum WIFICIPHER {
    WIFI_AUTH_OPEN = 0,
    WIFI_AUTH_WPA_PSK,
    WIFI_AUTH_WPA2_PSK,
    WIFI_AUTH_WPA_WPA2_PSK
};

enum WiFiMode_t { WIFI_OFF = 0, WIFI_STA, WIFI_AP, WIFI_APSTA };

enum WiFiStatus_t {
    WL_IDLE_STATUS = 0,
    WL_SCAN_COMPLETED,
    WL_NO_SSID_FOUND,
    WL_GOT_IP,
    WL_CONNECTION_LOST,
    WL_AUTH_FAILED
};

class WiFiClass {
public:
    bool begin(const char* ssid, const char* pass = nullptr, int chan = 0) {
        (void)ssid; (void)pass; (void)chan;
        return false;
    }
    bool mode(WiFiMode_t m) { return m == WIFI_OFF || m == WIFI_STA; }
    WiFiMode_t mode() { return WIFI_OFF; }
    WiFiStatus_t status() { return WL_IDLE_STATUS; }

    // The scan is real (radio_stub.cpp -> wifi_mon's passive sweep on the
    // sniffer): the raw Wi-Fi screen's scanNetworks(async, no hidden,
    // passive, 150 ms) / scanComplete() / per-index getters. The 4th
    // argument is the ESP32 core's max_ms_per_chan (the dwell). Only the
    // async, passive form is honoured -- the sniffer never transmits.
    // Without a sniffer the scan finishes at once with no networks.
    int scanNetworks(bool async = false, bool showHiddenAPs = false,
                     bool passive = true, uint32_t maxMsPerChan = 300);
    int8_t scanComplete();
    void scanDelete();

    String SSID() { return String(); }
    String SSID(uint8_t i);
    int RSSI() { return -127; }
    int RSSI(uint8_t i);
    uint8_t channel() { return 1; }
    uint8_t channel(uint8_t i);
    WIFICIPHER encryptionType(uint8_t i);
    const uint8_t* BSSID() { return nullptr; }
    const uint8_t* BSSID(uint8_t i);
    bool disconnect() { return true; }
    bool setTxPower(int8_t power) { (void)power; return false; }
};

extern WiFiClass WiFi;
