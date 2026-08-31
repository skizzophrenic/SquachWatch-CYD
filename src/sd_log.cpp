// SquachWatch-CYD — SD log implementation
#include "sd_log.h"
#include <SD.h>
#include <stdio.h>

// CYD SD card CS — see docs/PINOUT.md. AWOK: CS=14 on the on-board
// slot, sharing the DISPLAY'S VSPI bus (18/23/19). GPIO5 on this board
// is TFT_RST — reusing the CYD's CS=5 would fight the display.
#if defined(AWOK)
    #define SD_CS_PIN 14
#else
    #define SD_CS_PIN 5
#endif

bool SdLog::begin() {
    if (_ready) return true;
    // TFT_eSPI has already initialized VSPI by the time this runs on
    // AWOK (shared bus with the display), so SPI.begin() here would
    // just re-init a bus that's already up -- skip it and hand
    // SD.begin() the CS pin directly.
#if !defined(AWOK)
    SPI.begin(18, 19, 23, SD_CS_PIN);  // SCK, MISO, MOSI, CS
#endif
    if (!SD.begin(SD_CS_PIN)) {
        _ready = false;
        return false;
    }
    _ready = true;
    openDaily();
    return true;
}

void SdLog::openDaily() {
    if (!_ready) return;
    uint32_t t = millis();
    uint32_t day = t / (24UL * 60UL * 60UL * 1000UL);
    snprintf(_filename, sizeof(_filename), "/squachwatch-%lu.log", (unsigned long)day);
}

void SdLog::logEvent(const Detection& d) {
    if (!_ready) return;
    File f = SD.open(_filename, FILE_APPEND);
    if (!f) return;
    char line[96];
    char mac[18];
    snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
             d.mac[0], d.mac[1], d.mac[2], d.mac[3], d.mac[4], d.mac[5]);
    // Sanitize any commas in vendor / name
    char vendorSafe[12], nameSafe[20];
    strncpy(vendorSafe, d.vendor, sizeof(vendorSafe) - 1); vendorSafe[sizeof(vendorSafe)-1] = 0;
    strncpy(nameSafe,   d.name,   sizeof(nameSafe)   - 1); nameSafe[sizeof(nameSafe)-1]   = 0;
    for (char* p = vendorSafe; *p; p++) if (*p == ',') *p = '.';
    for (char* p = nameSafe;   *p; p++) if (*p == ',') *p = '.';
    snprintf(line, sizeof(line),
             "%lu,%s,%d,%s,%u,%s,%s\n",
             (unsigned long)millis(),
             detectionTypeName(d.type),
             d.rssi,
             mac,
             d.channel,
             vendorSafe,
             nameSafe);
    f.print(line);
    f.close();
}

void SdLog::tick() {
    if (!_ready) return;
    uint32_t now = millis();
    if (now - _lastFlush > 5000) {
        _lastFlush = now;
        // Reopen daily file once an hour (or on day change)
        static uint32_t lastDayCheck = 0;
        if (now - lastDayCheck > 3600000) {
            lastDayCheck = now;
            openDaily();
        }
    }
}
