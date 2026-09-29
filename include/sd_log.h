// SquachWatch-CYD — optional SD card event log
// If the SD card is mounted at boot, each Detection is appended to
// /squachwatch-YYYYMMDD.log as one CSV line.
// Targeted scans and evil portal detection captures are saved with
// human-readable filenames: /scan_<SSID>.log and /evilportal_<SSID>.log.
// If the card is absent, every call is a silent no-op.
#pragma once
#include <Arduino.h>
#include "state.h"

class SdLog {
public:
    bool begin();              // returns true if card mounted
    bool ready() const { return _ready; }
    void logEvent(const Detection& d);
    void logTargetScan(const char* ssid, const uint8_t* bssid, int8_t rssi, uint8_t channel = 0, const char* mode = "WATCH");
    void tick();               // flush / housekeeping (called from loop)
    // Deletes every squachwatch log file on the card (squachwatch-*, evilportal_*, scan_*).
    // For the security wipe -- the phrase and the ignore list live in NVS, but the
    // detection history a wipe must also erase is here. A no-op when no card is mounted.
    void wipe();

    static void sanitizeFilename(const char* in, char* out, size_t maxLen);

private:
    bool     _ready = false;
    uint32_t _lastFlush = 0;
    char     _filename[64] = {0};
    void     openDaily();
    static void formatTimestamp(char* out, size_t maxLen);
};

