// SquachWatch-CYD — SD log implementation
#include "sd_log.h"
#include "frame_push.h"   // busLock(): the card shares the panel's bus
#if defined(WAVESHARE_206)
#include "wardrive.h"
#endif
#include "sd_row.h"
#include "clock.h"
#if defined(CYD_GPS)
#include "gnss.h"
#endif
#include <SD.h>
#if defined(FREENOVE_S3) || defined(FREENOVE_S3_35) || defined(WAVESHARE_206)
// The Freenove S3's slot is wired for SDMMC, not SPI: every card call in this
// file goes through CARD, which is SD_MMC there and SD everywhere else. Both
// are an fs::FS, so open/remove/cardSize read the same either way.
#include <SD_MMC.h>
#define CARD SD_MMC
#else
#define CARD SD
#endif
#include <stdio.h>
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
//
// NM-CYD-C5: CS=10, on the DISPLAY's bus (SCK 6, MISO 2, MOSI 7), from
// RockBase's connections.md. Not 5 -- GPIO5 on this board is the GPS header's
// UART, and the card simply never answers.
#if defined(AWOK)
    #define SD_CS_PIN 14
#elif defined(NM_CYD_C5)
    #define SD_CS_PIN 10
#elif defined(TDECK_PLUS)
    #define SD_CS_PIN 39   // on the display's bus (40/38/41), per LilyGo's utilities.h
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
#if (defined(TWATCH_S3) && !defined(WAVESHARE_206)) || defined(SQW_SMALL)   // the Cardputer has a slot, on pins of its own: not brought up yet
    return false;   // no card slot; GPIO19/20 are the S3's USB pins
#endif
#if defined(CROWPANEL7)
    // A slot, but not brought up in this port: it shares GPIO 4/5/6 with the
    // I2S amplifier behind a switch on the board (K1), and the tested unit
    // sits on the amplifier. So no log and no wipe of it here.
    return false;
#endif
    Serial.printf("[sd] mounting: heap %lu, largest block %lu\n", (unsigned long)ESP.getFreeHeap(), (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
#if defined(WAVESHARE_206)
    // The Waveshare 2.06"'s slot: SDMMC, one bit wide (CLK 2, CMD 1, D0 3 in
    // the board's BSP; D1-D3 are not wired).
    SD_MMC.setPins(2, 1, 3);
    bool mounted = SD_MMC.begin("/sd", true, false, SDMMC_FREQ_DEFAULT, SD_MAX_FILES);
    if (!mounted) {
#elif defined(FREENOVE_S3) || defined(FREENOVE_S3_35)
    // SDMMC on the S3's GPIO matrix. Pins from Freenove's own SDMMC sketch and
    // the schematic: CLK 38, CMD 40, D0 39, D1 41, D2 48, D3 47 on the 2.8",
    // CLK 5, CMD 4, D0 6, D1 7, D2 2, D3 3 on the 3.5", each with a 10K
    // pull-up on the board. 4-bit first; if a card will not start that way,
    // 1-bit (CLK/CMD/D0 only) before giving up, and the log says which.
#if defined(FREENOVE_S3_35)
    SD_MMC.setPins(5, 4, 6, 7, 2, 3);
#else
    SD_MMC.setPins(38, 40, 39, 41, 48, 47);
#endif
    bool mounted = SD_MMC.begin("/sd", false, false, SDMMC_FREQ_DEFAULT, SD_MAX_FILES);
    if (!mounted) {
        SD_MMC.end();
#if defined(FREENOVE_S3_35)
        SD_MMC.setPins(5, 4, 6);
#else
        SD_MMC.setPins(38, 40, 39);
#endif
        mounted = SD_MMC.begin("/sd", true, false, SDMMC_FREQ_DEFAULT, SD_MAX_FILES);
        if (mounted) Serial.println("[sd] 4-bit did not start; mounted 1-bit");
    }
    if (!mounted) {
#elif defined(CYD35)
    // (The RL Phantom used to land here too, and its SD card never worked as
    // a result: the card is on 18/19/23, and the display's SPI engine never
    // clocked those pins. Its display now runs on HSPI -- see its user setup
    // -- so it takes the original board's branch below with a bus of its own.)
    // The RL Phantom landed here for the same reason cyd35 does, and it cost a
    // tester an evening: its resistive touch chip sits on the DISPLAY's bus, so
    // the extra pins this attaches corrupt MISO for every touch read afterwards.
    // The symptom is precise and was reported exactly as described below --
    // the 4-corner calibration works (it runs BEFORE engine.init() brings SD up)
    // and touch is dead on the very next screen. Not a bad calibration blob: a
    // corrupted bus underneath a perfectly good one.
    //
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
    if (!SD.begin(SD_CS_PIN, tft.getSPIinstance(), 4000000, "/sd", SD_MAX_FILES)) {
#elif defined(NM_CYD_C5)
    // Same shape as cyd35 above, and for the same reason: the card is on the
    // DISPLAY's SPI bus here (SCK 6, MISO 2, MOSI 7), so it gets TFT_eSPI's
    // already-begun instance and nothing extra is ever attached to the bus.
    //
    // It matters more on this board than the comment above suggests, because
    // -DRLPHANTOM_R sends it to the #else branch otherwise, and that branch
    // runs SPI.begin(18, 19, 23, 5) -- pins chosen for the original CYD's
    // dedicated SD bus. Two separate faults on a C5: those are not this
    // board's SD pins, so the card never answers (observed:
    // "Card Failed! cmd: 0x00" and then "no card, or it did not answer"
    // with a working card inserted); and GPIO23 is this board's TFT_CS, so
    // the additive GPIO-matrix attach the comment above warns about lands on
    // the display's own chip select.
    if (!SD.begin(SD_CS_PIN, tft.getSPIinstance(), 4000000, "/sd", SD_MAX_FILES)) {
#elif defined(TDECK_PLUS)
    // The T-Deck's slot is on the DISPLAY's bus too (SCK 40, MISO 38, MOSI 41,
    // CS 39), with the SX1262 beside it on CS 9: the same shared-bus treatment
    // as the C5 above, TFT_eSPI's own instance and nothing extra attached.
    if (!SD.begin(SD_CS_PIN, tft.getSPIinstance(), 4000000, "/sd", SD_MAX_FILES)) {
#elif defined(AWOK)
    if (!SD.begin(SD_CS_PIN, SPI, 4000000, "/sd", SD_MAX_FILES)) {
#else
    // Original board only: a genuinely separate, dedicated SD bus (not
    // shared with the display), so it does need its own explicit begin()
    // -- SD.begin()'s internal default-pin fallback happens to match
    // this board's real wiring too, but stay explicit for clarity.
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

#if defined(WAVESHARE_206)
#include <ff.h>
#include <driver/sdmmc_host.h>
#include <sdmmc_cmd.h>
#include <diskio_impl.h>
#include <diskio_sdmmc.h>
// Formatted by hand rather than by the driver's format-on-mount: that one
// uses one-sector clusters, and on a card of tens of GB the allocation table
// alone is hundreds of MB to write -- the watch sat in it for minutes. Here:
// the card brought up on its own, handed to FatFs, FAT32 with 32 KB clusters.
bool SdLog::format() {
    if (_ready) { SD_MMC.end(); _ready = false; }
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.flags = SDMMC_HOST_FLAG_1BIT;
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.clk = GPIO_NUM_2; slot.cmd = GPIO_NUM_1; slot.d0 = GPIO_NUM_3;
    slot.d1 = slot.d2 = slot.d3 = GPIO_NUM_NC;
    slot.width = 1;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    if (sdmmc_host_init() != ESP_OK) { Serial.println("[sd] format: host would not start"); return false; }
    bool ok = false;
    sdmmc_card_t* card = (sdmmc_card_t*)calloc(1, sizeof(sdmmc_card_t));
    BYTE pdrv = 0xFF;
    void* work = malloc(4096);
    if (card && work && sdmmc_host_init_slot(SDMMC_HOST_SLOT_1, &slot) == ESP_OK &&
        sdmmc_card_init(&host, card) == ESP_OK && ff_diskio_get_drive(&pdrv) == ESP_OK) {
        const uint64_t mb = (uint64_t)card->csd.capacity * card->csd.sector_size >> 20;
        Serial.printf("[sd] format: %llu MB card, FAT32 with 32 KB clusters -- erasing it\n", (unsigned long long)mb);
        ff_diskio_register_sdmmc(pdrv, card);
        char drv[3] = { (char)('0' + pdrv), ':', 0 };
        const uint32_t t0 = millis();
        FRESULT r = f_mkfs(drv, FM_FAT32, 32768, work, 4096);
        if (r != FR_OK) { Serial.printf("[sd] format: FAT32 refused (%d), trying any FAT\n", (int)r); r = f_mkfs(drv, FM_ANY, 0, work, 4096); }
        ff_diskio_register(pdrv, NULL);
        ok = (r == FR_OK);
        Serial.printf("[sd] format %s in %lu ms (FatFs %d)\n", ok ? "done" : "FAILED", (unsigned long)(millis() - t0), (int)r);
    } else {
        Serial.println("[sd] format: no card answered");
    }
    free(work);
    free(card);
    sdmmc_host_deinit();
    if (!ok) return false;
    return begin();
}

void SdLog::list() {
    if (!_ready) { Serial.println("[sd] ls: no card mounted"); return; }
    File dir = CARD.open("/");
    unsigned n = 0;
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
        Serial.printf("[sd] ls: %-32s %8lu%s\n", f.name(), (unsigned long)f.size(), f.isDirectory() ? " <dir>" : "");
        f.close(); n++;
    }
    dir.close();
    Serial.printf("[sd] ls: %u entries; logging to %s\n", n, _filename);
    File f = CARD.open(_filename, FILE_READ);
    if (!f) return;
    const size_t sz = f.size(), from = sz > 600 ? sz - 600 : 0;
    f.seek(from);
    String tail = f.readString();
    f.close();
    Serial.printf("[sd] tail of %s:\n%s\n", _filename, tail.c_str());
    // And the wardrive file being written, if there is one.
    const char* wd = Wardrive::wsFileName();
    if (!*wd) return;
    File w = CARD.open(wd, FILE_READ);
    if (!w) return;
    const size_t wsz = w.size();
    w.seek(wsz > 900 ? wsz - 900 : 0);
    String wt = w.readString();
    w.close();
    Serial.printf("[sd] tail of %s (%lu bytes):\n%s\n", wd, (unsigned long)wsz, wt.c_str());
}

void SdLog::selfTest() {
    if (!_ready) { Serial.println("[sd] test: no card mounted"); return; }
    static const size_t CHUNK = 4096, TOTAL = 256 * 1024;
    uint8_t* buf = (uint8_t*)malloc(CHUNK);
    if (!buf) { Serial.println("[sd] test: no memory"); return; }
    const char* path = "/sqw-test.bin";
    bool ok = true;
    uint32_t t0 = millis();
    File f = CARD.open(path, FILE_WRITE);
    if (!f) { Serial.println("[sd] test: could not create the file"); free(buf); return; }
    for (size_t at = 0; at < TOTAL && ok; at += CHUNK) {
        for (size_t i = 0; i < CHUNK; i++) buf[i] = (uint8_t)((at + i) * 31 + 7);
        ok = f.write(buf, CHUNK) == CHUNK;
    }
    f.close();
    const uint32_t wMs = millis() - t0;
    t0 = millis();
    f = CARD.open(path, FILE_READ);
    size_t bad = 0;
    for (size_t at = 0; f && at < TOTAL && ok; at += CHUNK) {
        if (f.read(buf, CHUNK) != CHUNK) { ok = false; break; }
        for (size_t i = 0; i < CHUNK; i++) if (buf[i] != (uint8_t)((at + i) * 31 + 7)) bad++;
    }
    if (f) f.close();
    const uint32_t rMs = millis() - t0;
    CARD.remove(path);
    free(buf);
    const char* type = SD_MMC.cardType() == CARD_SDHC ? "SDHC/SDXC" : SD_MMC.cardType() == CARD_SD ? "SD" : SD_MMC.cardType() == CARD_MMC ? "MMC" : "unknown";
    Serial.printf("[sd] test %s: %s card, %llu MB, %llu MB used of %llu MB; 256 KB written in %lu ms (%lu KB/s), read in %lu ms (%lu KB/s), %u bad bytes\n",
                  (ok && !bad) ? "PASSED" : "FAILED", type,
                  (unsigned long long)(CARD.cardSize() >> 20), (unsigned long long)(SD_MMC.usedBytes() >> 20),
                  (unsigned long long)(SD_MMC.totalBytes() >> 20),
                  (unsigned long)wMs, (unsigned long)(wMs ? 256000 / wMs : 0),
                  (unsigned long)rMs, (unsigned long)(rMs ? 256000 / rMs : 0), (unsigned)bad);
}
#endif

void SdLog::openDaily() {
    if (!_ready) return;
    // Named for the local date once the clock is trusted. A guessed clock
    // gets the nodate file, because its date can be days behind.
    _day = Clock::trusted() ? Clock::localDay() : 0;
    SdRow::fileName(_filename, sizeof(_filename), _day);
#if defined(NM_CYD_C5)
    // Say what is already on the card for today. Mounting proves the card
    // answers; it does not prove a single row ever reached it, and until this
    // line existed the only way to tell the two apart was to pull the card.
    // Board-gated for the same reason as the branch in logEvent() below.
    FramePush::busLock();
    File f = CARD.open(_filename, FILE_READ);
    if (f) { Serial.printf("[sd] %s: %lu bytes already\n", _filename, (unsigned long)f.size()); f.close(); }
    else   { Serial.printf("[sd] %s: new file\n", _filename); }
    FramePush::busUnlock();
#endif
}

void SdLog::logEvent(const Detection& d) {
    if (!_ready) return;
    // The card shares the panel's SPI bus on the boards that have it there;
    // on the C5 the push runs in a task, so the write waits for a frame in
    // flight (a no-op everywhere else).
    FramePush::busLock();
    File f = CARD.open(_filename, FILE_APPEND);
    if (!f) {
        FramePush::busUnlock();
#if defined(NM_CYD_C5)
        // Once, not once per detection: a card that mounts but cannot be
        // opened for append reads exactly like a card that was logging, and
        // begin()'s own comment two screens up says that is the thing to
        // avoid. A write-protected card, a full one, or a filesystem the
        // driver mounted but cannot write all land here.
        //
        // Board-gated ON PURPOSE. It is worth having on every board and costs
        // ~288 bytes of flash, but this is a board-port PR: no existing board
        // should change by a byte on account of it. Lift the guard if you want
        // it everywhere -- see the note in the PR description.
        static bool said = false;
        if (!said) {
            said = true;
            Serial.printf("[sd] cannot open %s for append: mounted, but nothing is being logged\n",
                          _filename);
        }
#endif
        return;
    }
    char line[SdRow::ROW_MAX];
#if defined(CYD_GPS)
    // Where the board is now. The queue drains on the loop task within a
    // frame or two of the detection, so this is where it was seen.
    SdRow::line(line, sizeof(line), d,
                Clock::trusted() ? Clock::nowEpoch() : 0, millis(),
                &Gnss::fix(), Gnss::faked(), millis());
#else
    SdRow::line(line, sizeof(line), d,
                Clock::trusted() ? Clock::nowEpoch() : 0, millis(),
                nullptr, false, 0);
#endif
    f.print(line);
    f.close();
    FramePush::busUnlock();
}

void SdLog::wipe() {
    if (!_ready) return;
    FramePush::busLock();
    // Walk the root and remove every file this firmware writes. Names are
    // /squachwatch-YYYYMMDD.log and /squachwatch-nodate.log; matching on the
    // prefix takes every day's file, which is the whole point of a wipe.
    File dir = CARD.open("/");
    if (!dir) { FramePush::busUnlock(); return; }
    // Collect first, then remove: deleting while iterating openNextFile() is
    // not something the FAT driver promises to survive.
    char victims[16][32];
    int  n = 0;
    for (File f = dir.openNextFile(); f && n < 16; f = dir.openNextFile()) {
        const char* nm = f.name();
        // name() is with or without a leading slash depending on core version;
        // match the basename either way.
        const char* base = nm;
        for (const char* p = nm; *p; p++) if (*p == '/') base = p + 1;
        if (strncmp(base, "squachwatch-", 12) == 0) {
            snprintf(victims[n], sizeof victims[n], "/%s", base);
            n++;
        }
        f.close();
    }
    dir.close();
    for (int i = 0; i < n; i++) CARD.remove(victims[i]);
    FramePush::busUnlock();
    _filename[0] = '\0';       // force a fresh openDaily() on the next event
}

void SdLog::tick() {
    if (!_ready) return;
    uint32_t now = millis();
    if (now - _lastFlush > 5000) {
        _lastFlush = now;
        // A new file at local midnight, and the moment the clock is first
        // trusted (nodate to the real date).
        uint32_t day = Clock::trusted() ? Clock::localDay() : 0;
        if (day != _day || _filename[0] == '\0') openDaily();
    }
}
