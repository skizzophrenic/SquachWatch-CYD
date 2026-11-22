// SquachWatch-CYD: wardriving on the CYD GPS builds, with the same capture as the
// watch, written straight to the SD card as a WiGLE file. See wardrive.h.
//
// The CYD has little heap to spare with a card mounted (26 KB free, a 10.7 KB
// largest block, measured on the user's board), so everything here is made
// only while WARDRIVE is on, in six pieces of 1.5 KB or less, and wardriving
// steps aside when the heap gets tight. Every other board compiles this file
// empty.
//
// In the emulator the card is the host directory named by SQUACHSIM_SD.
#if defined(CYD_GPS)
#include "wardrive.h"
#include "wardrive_capture.h"
#include "gnss.h"
#include <Arduino.h>
#include <Preferences.h>
#include <esp_heap_caps.h>
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined(ESP32)
// The card is SD_MMC on the Freenove S3 boards (sd_log.cpp mounts it there)
// and SPI SD everywhere else.
#if defined(FREENOVE_S3) || defined(FREENOVE_S3_35)
#include <SD_MMC.h>
#define WD_CARD SD_MMC
#else
#include <SD.h>
#define WD_CARD SD
#endif
#include "frame_push.h"
#endif

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "sim"
#endif
#ifndef SQW_ENV
#define SQW_ENV "cyd-gps"
#endif

namespace Wardrive {

namespace {

// The scanner's SCAN_FLUSH_BLOCK_B in detection.cpp: wardriving gives way at
// the point where detection starts fighting for heap.
const uint32_t PAUSE_BLOCK_B = 8192;
// And out of the same pool it was measured in -- INTERNAL SRAM. Asking with
// MALLOC_CAP_8BIT alone also matches PSRAM on every board that builds with
// -DBOARD_HAS_PSRAM (sticks3, twatch-s3, tdeck-plus, crowpanel7, the Freenove
// S3 envs), and the call hands back the larger of the two blocks, so there it
// would answer with PSRAM's megabytes and the pause would never arm however
// little internal SRAM was left. MALLOC_CAP_8BIT stays in the mask so a board
// without PSRAM reads exactly what it read before; see the long note at the
// top of src/detection.cpp for why not MALLOC_CAP_INTERNAL on its own.
const uint32_t PAUSE_CAPS = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
const uint16_t ROWS_B   = 512;      // the row buffer, about three rows
const uint16_t ROW_MAX  = 320;      // one csvRow() always fits in this
const uint32_t FLUSH_MS = 2000;     // the longest a row waits for the card
const uint32_t RETRY_MS = 5000;     // after a failed allocation
const uint32_t HEAP_MS  = 500;      // how often the heap is read

bool s_on = false;

// ---- the buffers, only while on --------------------------------------------
Q*         s_wifiQ = nullptr;
Q*         s_bleQ  = nullptr;
SeenTable  s_seen  = {};
char*      s_rows  = nullptr;
uint16_t   s_rowLen = 0, s_rowCount = 0;
uint32_t   s_rowSince = 0;          // millis() of the oldest unflushed row

// The radio tasks push while the loop may be freeing. s_live goes true only
// after every pointer is set, and false before any is freed; s_users counts
// the pushes in flight.
volatile bool s_live = false;
volatile bool s_paused = false;
volatile bool s_fixNow = false;     // a real fix on the last tick: nothing is queued without one
int           s_users = 0;

bool     s_card = false;            // the last sdTick saw a card
bool     s_allocFailed = false;
uint32_t s_retryAt = 0;
uint32_t s_heapAt = 0;              // 0: read the heap on the next tick
uint32_t s_written = 0, s_skipped = 0, s_dropped = 0;
uint32_t s_wWifi = 0, s_wBle = 0;
uint16_t s_rowWifi = 0, s_rowBle = 0;  // of the rows buffered, how many of each
uint16_t s_waiting = 0;                // files waiting to go to WiGLE, as last counted
uint32_t s_waitingAt = 0;

char s_file[32] = "";               // this session's file, "" until its first row
bool s_warned = false;              // "cannot write" printed this session

void addDropped(uint32_t n) { __atomic_add_fetch(&s_dropped, n, __ATOMIC_SEQ_CST); }

// ---- the card ---------------------------------------------------------------
#if defined(ESP32)
bool cardPresent(bool mounted) { return mounted; }

bool writeOut(const char* path, const char* buf, size_t len) {
    FramePush::busLock();
    // Asked before the open: on core 2.0.14 a file that FILE_APPEND creates
    // reports a size() left over from the failed stat(), so the first file on
    // the user's board went out with no header and WiGLE would refuse it.
    const bool fresh = !WD_CARD.exists(path);
    File f = WD_CARD.open(path, FILE_APPEND);
    if (!f) { FramePush::busUnlock(); return false; }
    bool ok = true;
    if (fresh) {
        char h[512];
        const size_t n = headerLines(h, sizeof h, FIRMWARE_VERSION, SQW_ENV, "ESP32-2432S028R", "Sunton");
        ok = n && f.write((const uint8_t*)h, n) == n;
    }
    ok = ok && f.write((const uint8_t*)buf, len) == len;
    f.close();
    FramePush::busUnlock();
    return ok;
}
#else
const char* simDir() {
    const char* d = getenv("SQUACHSIM_SD");
    return d && *d ? d : nullptr;
}
bool cardPresent(bool) { return simDir() != nullptr; }

bool writeOut(const char* path, const char* buf, size_t len) {
    const char* dir = simDir();
    if (!dir) return false;
    char full[512];
    snprintf(full, sizeof full, "%s%s", dir, path);
    FILE* f = fopen(full, "ab");
    if (!f) return false;
    bool ok = true;
    fseek(f, 0, SEEK_END);
    if (ftell(f) == 0) {
        char h[512];
        const size_t n = headerLines(h, sizeof h, FIRMWARE_VERSION, SQW_ENV, "ESP32-2432S028R", "Sunton");
        ok = n && fwrite(h, 1, n, f) == n;
    }
    ok = ok && fwrite(buf, 1, len, f) == len;
    fclose(f);
    return ok;
}
#endif

void flush() {
    if (!s_rows || !s_rowLen) return;
    if (writeOut(s_file, s_rows, s_rowLen)) {
        s_written += s_rowCount;
        s_wWifi += s_rowWifi; s_wBle += s_rowBle;
    } else {
        addDropped(s_rowCount);
        if (!s_warned) { s_warned = true; Serial.printf("[wardrive] cannot write %s\n", s_file); }
    }
    s_rowLen = 0; s_rowCount = 0; s_rowWifi = 0; s_rowBle = 0;
}

bool allocate() {
    if (!s_wifiQ) s_wifiQ = new (std::nothrow) Q();
    if (!s_bleQ)  s_bleQ  = new (std::nothrow) Q();
    for (uint8_t b = 0; b < SeenTable::BLOCKS; b++)
        if (!s_seen.block[b]) s_seen.block[b] = new (std::nothrow) Seen[SeenTable::PER]();
    if (!s_rows) s_rows = new (std::nothrow) char[ROWS_B];
    bool all = s_wifiQ && s_bleQ && s_rows;
    for (uint8_t b = 0; b < SeenTable::BLOCKS; b++) all = all && s_seen.block[b];
    if (!all) return false;
    s_rowLen = 0; s_rowCount = 0;
    s_heapAt = 0;
    __atomic_store_n(&s_live, true, __ATOMIC_SEQ_CST);
    return true;
}

bool held() {
    bool any = s_live || s_wifiQ || s_bleQ || s_rows;
    for (uint8_t b = 0; b < SeenTable::BLOCKS; b++) any = any || s_seen.block[b];
    return any;
}

// False if a radio task was still pushing; the buffers stay, unreachable, and
// the next tick tries again.
bool release() {
    __atomic_store_n(&s_live, false, __ATOMIC_SEQ_CST);
    for (int i = 0; i < 1000 && __atomic_load_n(&s_users, __ATOMIC_SEQ_CST); i++) delay(0);
    if (__atomic_load_n(&s_users, __ATOMIC_SEQ_CST)) return false;
    delete s_wifiQ; s_wifiQ = nullptr;
    delete s_bleQ;  s_bleQ  = nullptr;
    for (uint8_t b = 0; b < SeenTable::BLOCKS; b++) { delete[] s_seen.block[b]; s_seen.block[b] = nullptr; }
    delete[] s_rows; s_rows = nullptr;
    s_rowLen = 0; s_rowCount = 0;
    s_paused = false;
    return true;
}

void enqueue(bool wifi, const Pending& p) {
    __atomic_add_fetch(&s_users, 1, __ATOMIC_SEQ_CST);
    // Again, after counting in: the loop may have stored s_live false between
    // the first look and the count, and then it frees as soon as it reads
    // s_users as 0. Seen true here, the loop waits for this push to finish.
    if (__atomic_load_n(&s_live, __ATOMIC_SEQ_CST)) {
        Q* q = wifi ? s_wifiQ : s_bleQ;
        if (!q->push(p)) addDropped(1);
    }
    __atomic_sub_fetch(&s_users, 1, __ATOMIC_SEQ_CST);
}

// The radio tasks' gate: nothing while off or without buffers, and while
// paused only a count.
bool taking() {
    if (!__atomic_load_n(&s_live, __ATOMIC_SEQ_CST)) return false;
#if defined(CARDPUTER_ADV)
    // Without a fix nothing could be written: not queued, and not a loss.
    if (!s_fixNow) return false;
#endif
    if (s_paused) { addDropped(1); return false; }
    return true;
}

void nameFile(uint32_t epoch) {
    const time_t t = (time_t)epoch;
    struct tm tm;
    gmtime_r(&t, &tm);
    snprintf(s_file, sizeof s_file, "/wigle-%04d%02d%02d-%02d%02d.csv",
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min);
}

bool realFix(uint32_t nowMs) { return Gnss::fresh(nowMs) && !Gnss::faked() && Gnss::fix().epoch; }

void drain(uint32_t nowMs) {
    const Gnss::Fix& f = Gnss::fix();
    // A bench fix never writes a row. Items popped without a real fix are
    // let go uncounted, as on the watch, so a queue filled before the fix is
    // never stamped with it.
    const bool fix = realFix(nowMs);
    const uint32_t epoch = f.epoch ? f.epoch + (nowMs - f.atMs) / 1000u : 0;
    Pending p;
    for (int n = 0; n < 2 * QN; n++) {
        const bool got = s_wifiQ->pop(p) || s_bleQ->pop(p);
        if (!got) break;
        if (s_paused) { addDropped(1); continue; }
        if (!fix || !epoch) continue;
        if (!seenWorthWriting(s_seen, p.kind, p.mac, f.lat7, f.lon7, epoch)) { s_skipped++; continue; }
        Record r;
        memset(&r, 0, sizeof r);
        r.kind = p.kind; r.flags = p.flags;
        memcpy(r.mac, p.mac, 6);
        r.rssi = p.rssi; r.channel = p.channel; r.auth = p.auth;
        r.lat7 = f.lat7; r.lon7 = f.lon7; r.altM = f.altM; r.accM = f.accM;
        r.epoch = epoch;
        r.nameLen = p.nameLen; memcpy(r.name, p.name, p.nameLen);
        if (!s_file[0]) { nameFile(epoch); s_warned = false; }
        size_t w = csvRow(r, s_rows + s_rowLen, ROWS_B - s_rowLen);
        if (!w) { flush(); w = csvRow(r, s_rows + s_rowLen, ROWS_B - s_rowLen); }
        if (!w) { addDropped(1); continue; }
        if (!s_rowCount) s_rowSince = nowMs;
        s_rowLen = (uint16_t)(s_rowLen + w);
        s_rowCount++;
        if (r.kind == KIND_WIFI) s_rowWifi++; else s_rowBle++;
        if (ROWS_B - s_rowLen < ROW_MAX) flush();
    }
}

}  // namespace

// ---- the API ------------------------------------------------------------------

void sdBegin() {
    Preferences prefs;
    if (prefs.begin("wardrive", false)) { s_on = prefs.getBool("on", false); prefs.end(); }
    Serial.printf("[wardrive] %s, rows go to the SD card\n", s_on ? "ON" : "off");
}

void setEnabled(bool on) {
    s_on = on;
    Preferences prefs;
    if (prefs.begin("wardrive", false)) { prefs.putBool("on", on); prefs.end(); }
}
bool enabled() { return s_on; }

static int8_t s_sound = -1;   // -1 not read yet
bool soundOn() {
    if (s_sound < 0) {
        Preferences prefs;
        s_sound = 1;
        if (prefs.begin("wardrive", true)) { s_sound = prefs.getBool("snd", true) ? 1 : 0; prefs.end(); }
    }
    return s_sound == 1;
}
void toggleSound() {
    s_sound = soundOn() ? 0 : 1;
    Preferences prefs;
    if (prefs.begin("wardrive", false)) { prefs.putBool("snd", s_sound == 1); prefs.end(); }
}

void noteWifi(const uint8_t* bssid, const char* ssid, uint16_t auth, uint8_t channel, int8_t rssi) {
    if (!bssid || !taking()) return;
    Pending p;
    memset(&p, 0, sizeof p);
    p.kind = KIND_WIFI;
    memcpy(p.mac, bssid, 6);
    p.rssi = rssi; p.channel = channel; p.auth = auth;
    if (ssid) { size_t n = strnlen(ssid, 32); memcpy(p.name, ssid, n); p.nameLen = (uint8_t)n; }
    enqueue(true, p);
}

void noteBle(const uint8_t* mac, const char* name, int8_t rssi, bool haveMfgr, uint16_t mfgr) {
    if (!mac || !taking()) return;
    Pending p;
    memset(&p, 0, sizeof p);
    p.kind = KIND_BLE;
    memcpy(p.mac, mac, 6);
    p.rssi = rssi;
    if (haveMfgr) { p.flags |= F_HAS_MFGR; p.auth = mfgr; }
    if (name) { size_t n = strnlen(name, 32); memcpy(p.name, name, n); p.nameLen = (uint8_t)n; }
    enqueue(false, p);
}

static uint16_t countWaiting();

void sdTick(uint32_t nowMs, bool cardMounted) {
    s_card = cardPresent(cardMounted);
    // What is waiting to go to WiGLE, for the WARDRIVE page: a directory
    // read every fifteen seconds, not every frame.
    if (s_card && (!s_waitingAt || nowMs - s_waitingAt >= 15000)) {
        s_waitingAt = nowMs ? nowMs : 1;
        s_waiting = countWaiting();
    }
    if (!s_on || !s_card) {
        // Off, or the card is gone: what is buffered goes out first, then
        // every buffer is freed. Off also ends the session.
        if (s_rows) flush();
        if (held()) release();
        s_allocFailed = false;
        if (!s_on) { s_file[0] = 0; s_warned = false; }
        return;
    }
    if (!s_live) {
        if (s_allocFailed && (int32_t)(nowMs - s_retryAt) < 0) return;
        if (!allocate()) {
            release();
            s_allocFailed = true;
            s_retryAt = nowMs + RETRY_MS;
            return;
        }
        s_allocFailed = false;
    }
    if (!s_heapAt || nowMs - s_heapAt >= HEAP_MS) {
        s_heapAt = nowMs ? nowMs : 1;
        s_paused = heap_caps_get_largest_free_block(PAUSE_CAPS) < PAUSE_BLOCK_B;
    }
    s_fixNow = realFix(nowMs);
    drain(nowMs);
    if (s_rowCount && nowMs - s_rowSince >= FLUSH_MS) flush();
}

SdState sdState(uint32_t nowMs) {
    if (!s_on) return SdState::OFF;
    if (!s_card) return SdState::NO_CARD;
    if (s_paused || s_allocFailed) return SdState::LOW_MEMORY;
    if (!realFix(nowMs)) return SdState::WAITING_FOR_FIX;
    return SdState::LOGGING;
}

const char* sdFileName() { return s_file; }

uint32_t written() { return s_written; }
uint32_t writtenWifi() { return s_wWifi; }
uint32_t writtenBle()  { return s_wBle; }
uint32_t skipped() { return s_skipped; }
uint32_t dropped() { return __atomic_load_n(&s_dropped, __ATOMIC_SEQ_CST); }

void sdWipe() {
    // The buffered rows go without being written.
    s_rowLen = 0; s_rowCount = 0;
    s_file[0] = 0;
#if defined(ESP32)
    if (!s_card) return;
    FramePush::busLock();
    // Collect first, then remove, as SdLog::wipe() does. Sixteen a pass,
    // and passes until one comes back short: a card driven with for months
    // holds more than sixteen.
    for (int pass = 0; pass < 64; pass++) {
        File dir = WD_CARD.open("/");
        if (!dir) break;
        char victims[16][32];
        int  n = 0;
        for (File f = dir.openNextFile(); f && n < 16; f = dir.openNextFile()) {
            const char* nm = f.name();
            const char* base = nm;
            for (const char* p = nm; *p; p++) if (*p == '/') base = p + 1;
            const size_t len = strlen(base);
            if (strncmp(base, "wigle-", 6) == 0 && len > 4 && strcmp(base + len - 4, ".csv") == 0 && len + 2 <= sizeof victims[n]) {
                snprintf(victims[n], sizeof victims[n], "/%s", base);
                n++;
            }
            f.close();
        }
        dir.close();
        for (int i = 0; i < n; i++) WD_CARD.remove(victims[i]);
        if (n < 16) break;
    }
    // And the ones already uploaded: they are just as much a record of where
    // the board has been.
    for (int pass = 0; pass < 64; pass++) {
        File dir = WD_CARD.open("/wigle-sent");
        if (!dir) break;
        char victims[16][48];
        int  n = 0;
        for (File f = dir.openNextFile(); f && n < 16; f = dir.openNextFile()) {
            const char* nm = f.name(); const char* base = nm;
            for (const char* q = nm; *q; q++) if (*q == '/') base = q + 1;
            snprintf(victims[n], sizeof victims[n], "/wigle-sent/%s", base);
            n++;
            f.close();
        }
        dir.close();
        for (int i = 0; i < n; i++) WD_CARD.remove(victims[i]);
        if (n < 16) { WD_CARD.rmdir("/wigle-sent"); break; }
    }
    FramePush::busUnlock();
#endif
    s_waitingAt = 0;
}

// ---- for the WiGLE upload ------------------------------------------------------
namespace {
// "/wigle-20261121-1405.csv" or the bare name, and only those.
const char* baseOf(const char* nm) {
    const char* b = nm;
    for (const char* p = nm; *p; p++) if (*p == '/') b = p + 1;
    return b;
}
bool isSessionFile(const char* base) {
    const size_t len = strlen(base);
    return strncmp(base, "wigle-", 6) == 0 && len > 10 && len < 31 && strcmp(base + len - 4, ".csv") == 0;
}
// The file this session still appends to is not offered while WARDRIVE is on.
bool stillWriting(const char* base) { return s_on && s_file[0] && strcmp(s_file + 1, base) == 0; }
void sortNames(char (*names)[32], uint32_t* sizes, uint8_t n) {
    for (uint8_t i = 1; i < n; i++)
        for (uint8_t j = i; j > 0 && strcmp(names[j - 1], names[j]) > 0; j--) {
            char t[32]; memcpy(t, names[j], 32); memcpy(names[j], names[j - 1], 32); memcpy(names[j - 1], t, 32);
            const uint32_t z = sizes[j]; sizes[j] = sizes[j - 1]; sizes[j - 1] = z;
        }
}
}  // namespace

#if defined(ESP32)
static File s_rd;

uint8_t sdWaitingFiles(char (*names)[32], uint32_t* sizes, uint8_t cap) {
    if (!s_card) return 0;
    uint8_t n = 0;
    FramePush::busLock();
    File dir = WD_CARD.open("/");
    if (dir) {
        for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
            const char* base = baseOf(f.name());
            if (!f.isDirectory() && isSessionFile(base) && !stillWriting(base) && n < cap) {
                snprintf(names[n], 32, "/%s", base);
                sizes[n] = (uint32_t)f.size();
                n++;
            }
            f.close();
        }
        dir.close();
    }
    FramePush::busUnlock();
    sortNames(names, sizes, n);
    return n;
}

bool sdOpen(const char* path, uint32_t& size) {
    FramePush::busLock();
    if (s_rd) s_rd.close();
    s_rd = WD_CARD.open(path, FILE_READ);
    size = s_rd ? (uint32_t)s_rd.size() : 0;
    const bool ok = (bool)s_rd;
    FramePush::busUnlock();
    return ok;
}

int sdRead(uint8_t* buf, size_t n) {
    if (!s_rd) return -1;
    FramePush::busLock();
    const int got = s_rd.read(buf, n);
    FramePush::busUnlock();
    return got;
}

void sdClose() {
    FramePush::busLock();
    if (s_rd) s_rd.close();
    FramePush::busUnlock();
}

bool sdMarkSent(const char* path) {
    char to[48];
    snprintf(to, sizeof to, "/wigle-sent/%s", baseOf(path));
    FramePush::busLock();
    if (!WD_CARD.exists("/wigle-sent")) WD_CARD.mkdir("/wigle-sent");
    if (WD_CARD.exists(to)) WD_CARD.remove(to);
    const bool ok = WD_CARD.rename(path, to);
    FramePush::busUnlock();
    s_waitingAt = 0;
    return ok;
}

static uint16_t countWaiting() {
    uint16_t n = 0;
    FramePush::busLock();
    File dir = WD_CARD.open("/");
    if (dir) {
        for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
            const char* base = baseOf(f.name());
            if (!f.isDirectory() && isSessionFile(base) && !stillWriting(base)) n++;
            f.close();
        }
        dir.close();
    }
    FramePush::busUnlock();
    return n;
}
#else
// The emulator: the card is the directory SQUACHSIM_SD names.
}  // namespace Wardrive
#include <dirent.h>
#include <sys/stat.h>
namespace Wardrive {
static FILE* s_rd = nullptr;

uint8_t sdWaitingFiles(char (*names)[32], uint32_t* sizes, uint8_t cap) {
    const char* d = simDir();
    if (!d) return 0;
    DIR* dir = opendir(d);
    if (!dir) return 0;
    uint8_t n = 0;
    for (dirent* e = readdir(dir); e && n < cap; e = readdir(dir)) {
        if (!isSessionFile(e->d_name) || stillWriting(e->d_name)) continue;
        char full[512]; snprintf(full, sizeof full, "%s/%s", d, e->d_name);
        struct stat st; if (stat(full, &st) != 0) continue;
        snprintf(names[n], 32, "/%s", e->d_name);
        sizes[n] = (uint32_t)st.st_size;
        n++;
    }
    closedir(dir);
    sortNames(names, sizes, n);
    return n;
}
bool sdOpen(const char* path, uint32_t& size) {
    const char* d = simDir(); if (!d) return false;
    char full[512]; snprintf(full, sizeof full, "%s%s", d, path);
    if (s_rd) fclose(s_rd);
    s_rd = fopen(full, "rb");
    if (!s_rd) return false;
    fseek(s_rd, 0, SEEK_END); size = (uint32_t)ftell(s_rd); fseek(s_rd, 0, SEEK_SET);
    return true;
}
int sdRead(uint8_t* buf, size_t n) { return s_rd ? (int)fread(buf, 1, n, s_rd) : -1; }
void sdClose() { if (s_rd) { fclose(s_rd); s_rd = nullptr; } }
bool sdMarkSent(const char* path) {
    const char* d = simDir(); if (!d) return false;
    char from[512], to[512], dir2[512];
    snprintf(from, sizeof from, "%s%s", d, path);
    snprintf(dir2, sizeof dir2, "%s/wigle-sent", d);
    mkdir(dir2, 0755);
    snprintf(to, sizeof to, "%s/%s", dir2, baseOf(path));
    s_waitingAt = 0;
    return rename(from, to) == 0;
}
static uint16_t countWaiting() {
    char names[64][32]; uint32_t sizes[64];
    return sdWaitingFiles(names, sizes, 64);
}
#endif

uint16_t sdWaitingCount() { return s_waiting; }

}  // namespace Wardrive
#endif
