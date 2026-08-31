// SquachWatch-CYD — SD log implementation
#include "sd_log.h"
#include <SD.h>
#include <stdio.h>
#if !defined(CYD)
#include <TFT_eSPI.h>
// The single TFT_eSPI instance main.cpp already owns and has already
// init()'d by the time SdLog::begin() runs (see the comment below for
// why AWOK/cyd35 specifically need this reference).
extern TFT_eSPI tft;
#endif

// CYD SD card CS — see docs/PINOUT.md. AWOK: CS=14 on the on-board
// slot, sharing the DISPLAY'S VSPI bus (18/23/19). GPIO5 on this board
// is TFT_RST — reusing the CYD's CS=5 would fight the display. cyd35
// shares its display's VSPI bus too (14/13/12, not 18/19/23) but its
// real SD-slot CS is unconfirmed -- 5 is a placeholder guess (SD has
// failed to mount on every real unit tested so far regardless).
#if defined(AWOK)
    #define SD_CS_PIN 14
#else
    #define SD_CS_PIN 5
#endif

bool SdLog::begin() {
    if (_ready) return true;
#if defined(CYD35)
    // SD.begin(csPin) defaults its SPIClass& parameter to the Arduino
    // *global* `SPI` object -- a separate, never-begun C++ instance
    // from TFT_eSPI's own internal one, even though both ultimately
    // target the same VSPI hardware. SDFS::begin() (ESP32 core's
    // SD.cpp) unconditionally calls that object's own spi.begin() with
    // NO arguments; for a never-begun SPIClass, SPIClass::begin() falls
    // back to the compiled-in esp32dev board defaults -- SCK=18,
    // MISO=19, MOSI=23 -- regardless of this board's real shared-bus
    // pins (14/13/12 here). Root-caused on real cyd35 hardware: those
    // extra pins get ADDITIONALLY attached to VSPI's signals via the
    // GPIO matrix (spiAttachSCK() etc. are additive, not exclusive),
    // corrupting MISO for every touch read afterward even though the
    // display's write-only path looked completely fine.
    //
    // Passing TFT_eSPI's own already-init()'d SPI instance instead
    // makes SDFS::begin()'s internal spi.begin() call a genuine no-op
    // (SPIClass::begin() returns immediately if already begun -- see
    // its own guard), so nothing extra ever gets attached to the bus.
    //
    // AWOK deliberately does NOT get this treatment despite sharing
    // the same VSPI-bus shape: real hardware regression testing showed
    // its touch stops responding once SD.begin() runs with the shared
    // instance passed in (SD.begin() still attempts real transactions
    // over that peripheral even though `begin()` itself becomes a
    // no-op, and AWOK's touch chip is apparently more sensitive to
    // that than cyd35's) -- so it keeps the plain no-args SD.begin()
    // below, same as before this fix existed.
    if (!SD.begin(SD_CS_PIN, tft.getSPIinstance())) {
#elif defined(AWOK)
    if (!SD.begin(SD_CS_PIN)) {
#else
    // Original board only: a genuinely separate, dedicated SD bus (not
    // shared with the display), so it does need its own explicit begin()
    // -- SD.begin()'s internal default-pin fallback happens to match
    // this board's real wiring too, but stay explicit for clarity.
    SPI.begin(18, 19, 23, SD_CS_PIN);  // SCK, MISO, MOSI, CS
    if (!SD.begin(SD_CS_PIN)) {
#endif
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
