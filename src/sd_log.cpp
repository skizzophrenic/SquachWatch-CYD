// SquachWatch-CYD — SD log implementation
#include "sd_log.h"
#include <SD.h>
#if defined(FREENOVE_S3)
// The Freenove S3's slot is wired for SDMMC, not SPI: every card call in this
// file goes through CARD, which is SD_MMC there and SD everywhere else. Both
// are an fs::FS, so open/remove/cardSize read the same either way.
#include <SD_MMC.h>
#define CARD SD_MMC
#else
#define CARD SD
#endif
#include <stdio.h>
#include <time.h>
#include <ctype.h>
#include "clock.h"
// The Phantoms define CYD (they ARE a CYD) but still need this reference,
// because their touch shares the display's bus and SdLog::begin() has to hand
// SD the display's own SPI instance -- see the comment on that branch below.
#if !defined(CYD) || defined(RLPHANTOM) || defined(RLPHANTOM_R)
#include <TFT_eSPI.h>
#include <esp_heap_caps.h>
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

// Room for two open files, not the library's default five. The FAT driver
// reserves a 4 KB sector buffer per file slot up front, so five slots want a
// 25 KB block -- more than is left once both radios are up (largest block
// measured at 18 KB on the RL Phantom). This log has one file open at a time,
// plus a directory handle while it prunes old days.
static const uint8_t SD_MAX_FILES = 2;

bool SdLog::begin() {
    if (_ready) return true;
#if defined(TWATCH_S3)
    return false;   // no card slot; GPIO19/20 are the S3's USB pins
#endif
#if defined(CROWPANEL7)
    // A slot, but not brought up in this port: it shares GPIO 4/5/6 with the
    // I2S amplifier behind a switch on the board (K1), and the tested unit
    // sits on the amplifier. So no log and no wipe of it here.
    return false;
#endif
    Serial.printf("[sd] mounting: heap %lu, largest block %lu\n", (unsigned long)ESP.getFreeHeap(), (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
#if defined(FREENOVE_S3)
    // SDMMC on the S3's GPIO matrix. Pins from Freenove's own SDMMC sketch and
    // the schematic: CLK 38, CMD 40, D0 39, D1 41, D2 48, D3 47, each with a
    // 10K pull-up on the board. 4-bit first; if a card will not start that
    // way, 1-bit (CLK/CMD/D0 only) before giving up, and the log says which.
    SD_MMC.setPins(38, 40, 39, 41, 48, 47);
    bool mounted = SD_MMC.begin("/sd", false, false, SDMMC_FREQ_DEFAULT, SD_MAX_FILES);
    if (!mounted) {
        SD_MMC.end();
        SD_MMC.setPins(38, 40, 39);
        mounted = SD_MMC.begin("/sd", true, false, SDMMC_FREQ_DEFAULT, SD_MAX_FILES);
        if (mounted) Serial.println("[sd] 4-bit did not start; mounted 1-bit");
    }
#elif defined(AWOK)
    if (!SD.begin(SD_CS_PIN, SPI, 4000000, "/sd", SD_MAX_FILES)) {
#else
    // Original CYD and 3.5" boards (CYD35 / CYD35C): dedicated SD bus on
    // GPIO 18 (SCK), 19 (MISO), 23 (MOSI) with CS=5.
    SPI.begin(18, 19, 23, SD_CS_PIN);  // SCK, MISO, MOSI, CS
    if (!SD.begin(SD_CS_PIN, SPI, 4000000, "/sd", SD_MAX_FILES)) {
#endif
        // Said out loud either way: a board with no card, or a card on the
        // wrong pins, ran exactly like one that was logging, and the only
        // way to tell was to pull the card and look.
        Serial.println("[sd] no card, or it did not answer: nothing will be logged");
        _ready = false;
        return false;
    }
    Serial.printf("[sd] card mounted: %llu MB\n", (unsigned long long)(CARD.cardSize() >> 20));
    _ready = true;
    openDaily();
    return true;
}

void SdLog::sanitizeFilename(const char* in, char* out, size_t maxLen) {
    if (!in || !in[0]) {
        strncpy(out, "unknown", maxLen - 1);
        out[maxLen - 1] = '\0';
        return;
    }
    size_t j = 0;
    for (size_t i = 0; in[i] != '\0' && j < maxLen - 1; i++) {
        char c = in[i];
        if (isalnum((unsigned char)c) || c == '-' || c == '_') {
            out[j++] = c;
        } else if (c == ' ' || c == '.' || c == '@' || c == '#' || c == '+' || c == '~') {
            out[j++] = '_';
        }
    }
    if (j == 0) {
        strncpy(out, "unknown", maxLen - 1);
        out[maxLen - 1] = '\0';
    } else {
        out[j] = '\0';
    }
}

void SdLog::formatTimestamp(char* out, size_t maxLen) {
    if (Clock::isSet()) {
        struct tm tmv;
        time_t sec = (time_t)Clock::nowEpoch();
        localtime_r(&sec, &tmv);
        snprintf(out, maxLen, "%04d-%02d-%02d %02d:%02d:%02d",
                 tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                 tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
    } else {
        uint32_t ms = millis();
        uint32_t sec = ms / 1000u;
        uint32_t d = sec / 86400u;
        uint32_t h = (sec % 86400u) / 3600u;
        uint32_t m = (sec % 3600u) / 60u;
        uint32_t s = sec % 60u;
        if (d > 0) {
            snprintf(out, maxLen, "%lud %02lu:%02lu:%02lu", (unsigned long)d, (unsigned long)h, (unsigned long)m, (unsigned long)s);
        } else {
            snprintf(out, maxLen, "%02lu:%02lu:%02lu", (unsigned long)h, (unsigned long)m, (unsigned long)s);
        }
    }
}

void SdLog::openDaily() {
    if (!_ready) return;
    if (Clock::isSet()) {
        struct tm tmv;
        time_t sec = (time_t)Clock::nowEpoch();
        localtime_r(&sec, &tmv);
        snprintf(_filename, sizeof(_filename), "/squachwatch-%04d%02d%02d.log",
                 tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday);
    } else {
        uint32_t t = millis();
        uint32_t day = t / (24UL * 60UL * 60UL * 1000UL);
        snprintf(_filename, sizeof(_filename), "/squachwatch-day%lu.log", (unsigned long)day);
    }
}

void SdLog::logEvent(const Detection& d) {
    if (!_ready) return;
    openDaily();
    File f = CARD.open(_filename, FILE_APPEND);
    char mac[18];
    snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
             d.mac[0], d.mac[1], d.mac[2], d.mac[3], d.mac[4], d.mac[5]);

    // Sanitize any commas in vendor / name
    char vendorSafe[24], nameSafe[36];
    strncpy(vendorSafe, vendorText(d), sizeof(vendorSafe) - 1); vendorSafe[sizeof(vendorSafe)-1] = 0;
    strncpy(nameSafe,   d.name,   sizeof(nameSafe)   - 1); nameSafe[sizeof(nameSafe)-1]   = 0;
    for (char* p = vendorSafe; *p; p++) if (*p == ',') *p = '.';
    for (char* p = nameSafe;   *p; p++) if (*p == ',') *p = '.';

    char timeBuf[32];
    formatTimestamp(timeBuf, sizeof(timeBuf));

    if (f) {
        if (f.size() == 0) {
            f.println("# SQUACHWATCH - WIRELESS THREAT & DETECTION LOG");
            f.println("timestamp,type,rssi,mac,channel,vendor,name");
        }
        char line[128];
        snprintf(line, sizeof(line),
                 "%s,%s,%d,%s,%u,%s,%s\n",
                 timeBuf,
                 detectionTypeName(d.type),
                 d.rssi,
                 mac,
                 d.channel,
                 vendorSafe,
                 nameSafe);
        f.print(line);
        f.close();
    }

    // Human-readable evil portal detection capture with SSID in filename
    if (d.type == DetectionType::EVILTWIN) {
        char safeSsid[32];
        sanitizeFilename(d.name, safeSsid, sizeof(safeSsid));
        char evilFile[64];
        snprintf(evilFile, sizeof(evilFile), "/evilportal_%s.log", safeSsid);
        File ef = CARD.open(evilFile, FILE_APPEND);
        if (ef) {
            if (ef.size() == 0) {
                ef.println("==========================================================");
                ef.println("  SQUACHWATCH - ROGUE AP / EVIL PORTAL DETECTION CAPTURE  ");
                ef.println("==========================================================");
                ef.printf("# Impersonated SSID: %s\n", d.name[0] ? d.name : "(hidden)");
                ef.println("# An evil portal / rogue AP was detected cloning this SSID");
                ef.println("# with conflicting encryption or vendor parameters.");
                ef.println("# Format: Timestamp,Type,RSSI,Rogue_BSSID,Channel,Vendor,SSID,Alert");
            }
            ef.printf("%s,EVILTWIN,%d,%s,%u,EvilTwin,%s,EVIL_PORTAL_ALERT\n",
                      timeBuf, d.rssi, mac, d.channel, nameSafe);
            ef.close();
            Serial.printf("[sd] logged evil portal capture to %s\n", evilFile);
        }
    }
}

void SdLog::logTargetScan(const char* ssid, const uint8_t* bssid, int8_t rssi, uint8_t channel, const char* mode) {
    if (!_ready || !ssid || !ssid[0]) return;

    char safeSsid[32];
    sanitizeFilename(ssid, safeSsid, sizeof(safeSsid));
    char filename[64];
    snprintf(filename, sizeof(filename), "/scan_%s.log", safeSsid);
    File f = CARD.open(filename, FILE_APPEND);
    if (!f) return;
    if (f.size() == 0) {
        f.println("# SQUACHWATCH - TARGETED WIRELESS DEFENSE SCAN LOG");
        f.printf("# Target SSID: %s\n", ssid);
        f.println("# Format: Timestamp,Mode,RSSI,BSSID,Channel,SSID");
    }
    char mac[18] = {0};
    if (bssid) {
        snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
                 bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
    } else {
        strncpy(mac, "00:00:00:00:00:00", sizeof(mac));
    }
    char ssidSafe[36];
    strncpy(ssidSafe, ssid, sizeof(ssidSafe) - 1);
    ssidSafe[sizeof(ssidSafe) - 1] = 0;
    for (char* p = ssidSafe; *p; p++) if (*p == ',') *p = '.';

    char timeBuf[32];
    formatTimestamp(timeBuf, sizeof(timeBuf));
    f.printf("%s,%s,%d,%s,%u,%s\n",
             timeBuf, mode ? mode : "WATCH", rssi, mac, channel, ssidSafe);
    f.close();
    Serial.printf("[sd] logged targeted scan sighting to %s\n", filename);
}

void SdLog::wipe() {
    if (!_ready) return;
    // Walk the root and remove every file this firmware writes:
    // /squachwatch-*, /evilportal_*, /scan_*
    bool more = true;
    while (more) {
        File dir = CARD.open("/");
        if (!dir) break;
        char victims[32][64];
        int n = 0;
        for (File f = dir.openNextFile(); f && n < 32; f = dir.openNextFile()) {
            const char* nm = f.name();
            const char* base = nm;
            for (const char* p = nm; *p; p++) if (*p == '/') base = p + 1;
            if (strncmp(base, "squachwatch-", 12) == 0 ||
                strncmp(base, "evilportal_", 11) == 0 ||
                strncmp(base, "scan_", 5) == 0) {
                snprintf(victims[n], sizeof(victims[n]), "/%s", base);
                n++;
            }
            f.close();
        }
        dir.close();
        int removed = 0;
        for (int i = 0; i < n; i++) {
            if (CARD.remove(victims[i])) removed++;
        }
        if (n < 32 || removed == 0) more = false;
    }
    _filename[0] = '\0';       // force a fresh openDaily() on the next event
}

void SdLog::tick() {
    if (!_ready) return;
    uint32_t now = millis();
    if (now - _lastFlush > 5000) {
        _lastFlush = now;
        // Reopen daily file once every 60s or on day change / clock set
        static uint32_t lastDayCheck = 0;
        if (now - lastDayCheck > 60000) {
            lastDayCheck = now;
            openDaily();
        }
    }
}
