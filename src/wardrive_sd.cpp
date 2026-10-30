// SquachWatch-CYD: wardriving on the CYD GPS builds, with the same capture as the
// watch, written straight to the SD card as a WiGLE file. See wardrive.h.
//
// Elecrow reserves buffers before the display and radios allocate, then
// reuses them across sessions. Other profiles allocate on demand. Recording
// pauses when scanning needs the remaining contiguous memory.
// Every other board compiles this file empty.
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
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#if defined(ESP32)
#include <SD.h>
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
const uint16_t ROWS_B   = 512;      // the row buffer, about three rows
const uint16_t ROW_MAX  = 320;      // one csvRow() always fits in this
const uint32_t FLUSH_MS = 2000;     // the longest a row waits for the card
const uint32_t RETRY_MS = 5000;     // after a failed allocation
const uint32_t HEAP_MS  = 500;      // how often the heap is read

bool s_on = false;
#if defined(CYD_GPS_ELECROW)
bool s_keepBuffers = false;
#endif

// ---- the buffers, only while on --------------------------------------------
Q*         s_wifiQ = nullptr;
Q*         s_bleQ  = nullptr;
SeenTable  s_seen  = {};
char*      s_rows  = nullptr;
uint16_t   s_rowLen = 0, s_rowCount = 0;
uint32_t   s_rowEpoch = 0;          // UTC of the oldest unflushed row
uint32_t   s_rowSince = 0;          // millis() of the oldest unflushed row

// The radio tasks push while the loop may be freeing. s_live goes true only
// after every pointer is set, and false before any is freed; s_users counts
// the callbacks in flight, including their observation snapshots.
volatile bool s_live = false;
volatile bool s_paused = false;
int           s_users = 0;

bool     s_card = false;            // the last sdTick saw a card
bool     s_allocFailed = false;
uint32_t s_retryAt = 0;
uint32_t s_heapAt = 0;              // 0: read the heap on the next tick
uint32_t s_written = 0, s_skipped = 0, s_dropped = 0;
// A callback can outlast the bounded quiescence wait. Preserve the shutdown
// decision across ticks, even if the user re-enables or remounts meanwhile.
bool s_stopping = false, s_stopDiscard = false, s_stopEndSession = false;

char s_file[32] = "";               // this session's file, "" until its first write attempt
bool s_writeError = false;         // cleared only by a successful batch or OFF
bool s_newFile = false;            // rotate after any potentially partial write
bool s_warned = false;              // "cannot write" printed this session

void addDropped(uint32_t n) { __atomic_add_fetch(&s_dropped, n, __ATOMIC_SEQ_CST); }

// ---- the card ---------------------------------------------------------------
// Both platforms use this checked descriptor path. Arduino File wraps buffered
// stdio and discards fflush/fsync/fclose failures, so a full File::write count
// alone cannot justify counting a row as written.
struct CardLock {
    CardLock() {
#if defined(ESP32)
        FramePush::busLock();
#endif
    }
    ~CardLock() {
#if defined(ESP32)
        FramePush::busUnlock();
#endif
    }
};

#if defined(ESP32)
const size_t FULL_PATH_N = sizeof s_file + sizeof "/sd";
const char* cardRoot() { return "/sd"; } // SdLog::begin mounts this VFS path
bool cardPresent(bool mounted) { return mounted; }
#else
const size_t FULL_PATH_N = 512;
const char* cardRoot() {
    const char* d = getenv("SQUACHSIM_SD");
    return d && *d ? d : nullptr;
}
bool cardPresent(bool) { return cardRoot() != nullptr; }
#endif

bool cardPath(const char* path, char* out, size_t n) {
    const char* root = cardRoot();
    if (!root) { errno = ENODEV; return false; }
    const int len = snprintf(out, n, "%s%s", root, path);
    if (len < 0 || (size_t)len >= n) { errno = ENAMETOOLONG; return false; }
    return true;
}

bool pathExists(const char* path) {
    char full[FULL_PATH_N];
    if (!cardPath(path, full, sizeof full)) return true;
    CardLock lock;
    struct stat st;
    // An unreadable path is not safe to reuse as a new capture.
    return stat(full, &st) == 0 || errno != ENOENT;
}

bool writeExact(int fd, const char* buf, size_t len) {
    const ssize_t result = write(fd, buf, len);
    if (result == (ssize_t)len) return true;
    // A short write may leave a partial CSV/header. Do not append more to it;
    // flush() drops the batch and rotates the next observation to a new file.
    if (result >= 0 || !errno) errno = EIO;
    return false;
}

bool writeOut(const char* path, const char* buf, size_t len, bool newFile) {
    char full[FULL_PATH_N];
    if (!cardPath(path, full, sizeof full)) return false;
    CardLock lock; // includes sync/close and every error return
    // O_EXCL closes the gap between name selection and creation. Do not use
    // O_APPEND: IDF 4.4 FatFS maps it to FA_OPEN_ALWAYS even without O_CREAT.
    // O_WRONLY + a checked seek keeps a missing session from being recreated.
    const int flags = O_WRONLY | (newFile ? O_CREAT | O_EXCL : 0);
    const int fd = open(full, flags, 0666);
    if (fd < 0) return false;
    int error = 0;
    struct stat st;
    if (fstat(fd, &st) != 0) {
        error = errno ? errno : EIO;
    } else if (lseek(fd, 0, SEEK_END) < 0) {
        error = errno ? errno : EIO;
    } else if (st.st_size == 0) {
        char h[512];
        const size_t n = headerLines(h, sizeof h, FIRMWARE_VERSION, SQW_ENV, "ESP32-2432S028R", "Sunton");
        if (!n) error = EINVAL;
        else if (!writeExact(fd, h, n)) error = errno;
    }
    if (!error && !writeExact(fd, buf, len)) error = errno;
    if (!error && fsync(fd) != 0) error = errno ? errno : EIO;
    // Close exactly once, even after another failure. Retrying a failed close
    // could close a descriptor reused by another task.
    if (close(fd) != 0 && !error) error = errno ? errno : EIO;
    errno = error;
    return error == 0;
}

bool nameFile(uint32_t epoch);

void clearSeen() {
    for (uint8_t b = 0; b < SeenTable::BLOCKS; b++)
        if (s_seen.block[b]) memset(s_seen.block[b], 0, sizeof(Seen) * SeenTable::PER);
}
void flush() {
    if (!s_rows || !s_rowLen) return;
    const bool newFile = s_newFile || !s_file[0];
    if ((!newFile || nameFile(s_rowEpoch)) && writeOut(s_file, s_rows, s_rowLen, newFile)) {
        s_written += s_rowCount;
        s_writeError = false;
        s_warned = false;
    } else {
        addDropped(s_rowCount);
        s_writeError = true;
        s_newFile = true;
        // Entries include rows queued for this batch, not just durable rows.
        // A failed/partial batch must not suppress a later re-observation.
        // Clearing the bounded 256-entry table may repeat an older good row,
        // which is preferable to silently suppressing the failed one until the repeat window expires.
        clearSeen();
        if (!s_warned) { s_warned = true; Serial.printf("[wardrive] cannot write %s (errno %d)\n", s_file, errno); }
    }
    s_rowLen = 0; s_rowCount = 0;
}

bool allocate(bool admit = true) {
    if (!s_wifiQ) s_wifiQ = new (std::nothrow) Q();
    if (!s_bleQ)  s_bleQ  = new (std::nothrow) Q();
    for (uint8_t b = 0; b < SeenTable::BLOCKS; b++)
        if (!s_seen.block[b]) s_seen.block[b] = new (std::nothrow) Seen[SeenTable::PER]();
    if (!s_rows) s_rows = new (std::nothrow) char[ROWS_B];
    bool all = s_wifiQ && s_bleQ && s_rows;
    for (uint8_t b = 0; b < SeenTable::BLOCKS; b++) all = all && s_seen.block[b];
    if (!all) return false;
    // The previous session is quiescent. Retained storage must not carry
    // queued observations or repeat suppression into a new session.
    *s_wifiQ = Q{};
    *s_bleQ = Q{};
    for (uint8_t b = 0; b < SeenTable::BLOCKS; b++)
        memset(s_seen.block[b], 0, sizeof(Seen) * SeenTable::PER);
    s_rowLen = 0; s_rowCount = 0;
    s_heapAt = 0;
    __atomic_store_n(&s_live, admit, __ATOMIC_SEQ_CST);
    return true;
}

// Stop admission before touching queued work or freeing any buffer. A delayed
// callback keeps its old session alive until it finishes its accepted push.
bool quiesce() {
    __atomic_store_n(&s_live, false, __ATOMIC_SEQ_CST);
    for (int i = 0; i < 1000 && __atomic_load_n(&s_users, __ATOMIC_SEQ_CST); i++) delay(0);
    return !__atomic_load_n(&s_users, __ATOMIC_SEQ_CST);
}

bool release() {
    if (!quiesce()) return false;
#if defined(CYD_GPS_ELECROW)
    if (!s_keepBuffers) {
#endif
    delete s_wifiQ; s_wifiQ = nullptr;
    delete s_bleQ;  s_bleQ  = nullptr;
    for (uint8_t b = 0; b < SeenTable::BLOCKS; b++) { delete[] s_seen.block[b]; s_seen.block[b] = nullptr; }
    delete[] s_rows; s_rows = nullptr;
#if defined(CYD_GPS_ELECROW)
    } else {
        *s_wifiQ = Q{};
        *s_bleQ = Q{};
        for (uint8_t b = 0; b < SeenTable::BLOCKS; b++)
            memset(s_seen.block[b], 0, sizeof(Seen) * SeenTable::PER);
        memset(s_rows, 0, ROWS_B);
    }
#endif
    s_rowLen = 0; s_rowCount = 0;
    __atomic_store_n(&s_paused, false, __ATOMIC_SEQ_CST);
    return true;
}

// The radio tasks' gate: nothing while off or without buffers, and while
// paused only a count.
bool taking() {
    if (!__atomic_load_n(&s_live, __ATOMIC_SEQ_CST)) return false;
    if (__atomic_load_n(&s_paused, __ATOMIC_SEQ_CST)) { addDropped(1); return false; }
    return true;
}

// Count the entire accepted callback, so a stop cannot free/recreate its queue
// while its position snapshot is still being captured. The second live check
// closes the gap between admission and incrementing the in-flight count.
struct Producer {
    bool active = false;
    Producer() {
        if (!__atomic_load_n(&s_live, __ATOMIC_SEQ_CST)) return;
        __atomic_add_fetch(&s_users, 1, __ATOMIC_SEQ_CST);
        active = taking();
        if (!active) __atomic_sub_fetch(&s_users, 1, __ATOMIC_SEQ_CST);
    }
    ~Producer() {
        if (active) __atomic_sub_fetch(&s_users, 1, __ATOMIC_SEQ_CST);
    }
};

void enqueue(bool wifi, const Pending& p) {
    // The caller holds Producer until this push completes. Once accepted, a
    // callback may finish after s_live goes false; shutdown waits for it.
    Q* q = wifi ? s_wifiQ : s_bleQ;
    if (!q->push(p)) addDropped(1);
}

bool nameFile(uint32_t epoch) {
    const time_t t = (time_t)epoch;
    struct tm tm;
    gmtime_r(&t, &tm);
    char stem[24];
    snprintf(stem, sizeof stem, "/wigle-%04d%02d%02d-%02d%02d%02d",
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    // Bounded work and no allocation. Rapid restarts (or a clock reset) must
    // not append to an earlier session. A failed candidate never replaces the
    // previous filename, so status can still identify the damaged capture.
    for (unsigned sequence = 0; sequence < 1000; sequence++) {
        char candidate[sizeof s_file];
        if (sequence) snprintf(candidate, sizeof candidate, "%s-%03u.csv", stem, sequence);
        else snprintf(candidate, sizeof candidate, "%s.csv", stem);
        if (pathExists(candidate)) continue;
        memcpy(s_file, candidate, strlen(candidate) + 1);
        s_newFile = false;
        return true;
    }
    return false;
}

// Both callback and status use the same published source + position. Unsigned
// age rejects future stamps and stays correct when millis() wraps. UI's signed
// Gnss::fresh() intentionally tolerates future frame stamps; capture must not.
bool eligible(const Gnss::Snapshot& s, uint32_t observedMs) {
    if (!s.position.valid || !s.position.epoch || s.source == Gnss::Source::BENCH) return false;
#if defined(CYD_GPS_ELECROW)
    if (s.source != Gnss::Source::RECEIVER) return false;
#endif
    const uint32_t limit = s.source == Gnss::Source::PHONE ? Gnss::PHONE_FRESH_MS : Gnss::FRESH_MS;
    return observedMs - s.position.atMs < limit;
}

bool realFix(uint32_t nowMs) { return eligible(Gnss::snapshot(), nowMs); }

bool capturePosition(Pending& p) {
    p.observedMs = millis();
    const Gnss::Snapshot snapshot = Gnss::snapshot();
    if (!eligible(snapshot, p.observedMs)) return false;
    const Gnss::Fix& f = snapshot.position;
    p.lat7 = f.lat7; p.lon7 = f.lon7; p.altM = f.altM; p.accM = f.accM;
    p.epoch = f.epoch + (p.observedMs - f.atMs) / 1000u;
    return true;
}

void drain(uint32_t nowMs, bool dropPaused = true) {
    // Eligibility was decided at observation. Delayed writes retain that
    // position and UTC even after loss, movement, source change or staleness.
    Pending p;
    for (int n = 0; n < 2 * QN; n++) {
        const bool got = s_wifiQ->pop(p) || s_bleQ->pop(p);
        if (!got) break;
        if (dropPaused && __atomic_load_n(&s_paused, __ATOMIC_SEQ_CST)) { addDropped(1); continue; }
        if (!p.epoch) continue;
        if (!seenWorthWriting(s_seen, p.kind, p.mac, p.lat7, p.lon7, p.epoch)) { s_skipped++; continue; }
        Record r;
        memset(&r, 0, sizeof r);
        r.kind = p.kind; r.flags = p.flags;
        memcpy(r.mac, p.mac, 6);
        r.rssi = p.rssi; r.channel = p.channel; r.auth = p.auth;
        r.lat7 = p.lat7; r.lon7 = p.lon7; r.altM = p.altM; r.accM = p.accM;
        r.epoch = p.epoch;
        r.nameLen = p.nameLen; memcpy(r.name, p.name, p.nameLen);
        size_t w = csvRow(r, s_rows + s_rowLen, ROWS_B - s_rowLen);
        if (!w) { flush(); w = csvRow(r, s_rows + s_rowLen, ROWS_B - s_rowLen); }
        if (!w) { addDropped(1); continue; }
        if (!s_rowCount) { s_rowSince = nowMs; s_rowEpoch = p.epoch; }
        s_rowLen = (uint16_t)(s_rowLen + w);
        s_rowCount++;
        if (ROWS_B - s_rowLen < ROW_MAX) flush();
    }
}

}  // namespace

// ---- the API ------------------------------------------------------------------

void sdBegin() {
    Preferences prefs;
    if (prefs.begin("wardrive", false)) { __atomic_store_n(&s_on, prefs.getBool("on", false), __ATOMIC_SEQ_CST); prefs.end(); }
#if defined(CYD_GPS_ELECROW)
    // Called before the display buffer and engine.init(). This reserves RAM
    // without admitting radio callbacks or performing card I/O. Failed boot
    // allocation retains the ordinary on-demand retry when recording starts.
    if (!s_keepBuffers && !__atomic_load_n(&s_live, __ATOMIC_SEQ_CST) && !s_stopping &&
        !__atomic_load_n(&s_users, __ATOMIC_SEQ_CST)) {
        if (allocate(false)) s_keepBuffers = true;
        else release();
    }
#endif
    Serial.printf("[wardrive] %s, rows go to the SD card\n", enabled() ? "ON" : "off");
}

void setEnabled(bool on) {
    __atomic_store_n(&s_on, on, __ATOMIC_SEQ_CST);
    Preferences prefs;
    if (prefs.begin("wardrive", false)) { prefs.putBool("on", on); prefs.end(); }
}
bool enabled() { return __atomic_load_n(&s_on, __ATOMIC_SEQ_CST); }

void noteWifi(const uint8_t* bssid, const char* ssid, uint16_t auth, uint8_t channel, int8_t rssi) {
    if (!bssid) return;
    Producer producer;
    if (!producer.active) return;
    Pending p;
    memset(&p, 0, sizeof p);
    if (!capturePosition(p)) return;
    p.kind = KIND_WIFI;
    memcpy(p.mac, bssid, 6);
    p.rssi = rssi; p.channel = channel; p.auth = auth;
    if (ssid) { size_t n = strnlen(ssid, 32); memcpy(p.name, ssid, n); p.nameLen = (uint8_t)n; }
    enqueue(true, p);
}

void noteBle(const uint8_t* mac, const char* name, int8_t rssi, bool haveMfgr, uint16_t mfgr) {
    if (!mac) return;
    Producer producer;
    if (!producer.active) return;
    Pending p;
    memset(&p, 0, sizeof p);
    if (!capturePosition(p)) return;
    p.kind = KIND_BLE;
    memcpy(p.mac, mac, 6);
    p.rssi = rssi;
    if (haveMfgr) { p.flags |= F_HAS_MFGR; p.auth = mfgr; }
    if (name) { size_t n = strnlen(name, 32); memcpy(p.name, name, n); p.nameLen = (uint8_t)n; }
    enqueue(false, p);
}

void sdTick(uint32_t nowMs, bool cardMounted) {
    s_card = cardPresent(cardMounted);
    const bool on = enabled();
    if (!on || !s_card || s_stopping) {
        s_stopping = true;
        s_stopDiscard = s_stopDiscard || !s_card;
        s_stopEndSession = s_stopEndSession || !on;
        if (!quiesce()) return;
        if (s_stopDiscard) {
            // Missing media must never be touched. Account each accepted row
            // exactly once, including rows not yet transferred from the radios.
            addDropped(s_rowCount);
            s_rowLen = 0; s_rowCount = 0;
            Pending p;
            while (s_wifiQ && s_wifiQ->pop(p)) addDropped(1);
            while (s_bleQ && s_bleQ->pop(p)) addDropped(1);
        } else if (s_rows && s_wifiQ && s_bleQ) {
            // OFF finishes accepted observations using their saved snapshots,
            // including ones accepted before the low-memory producer pause.
            drain(nowMs, false);
            flush();
        }
        if (!release()) return; // retain shutdown intent if a callback is still retiring
        s_allocFailed = false;
        if (s_stopEndSession) { s_file[0] = 0; s_warned = false; s_writeError = false; s_newFile = false; }
        s_stopping = s_stopDiscard = s_stopEndSession = false;
        return;
    }
    if (!__atomic_load_n(&s_live, __ATOMIC_SEQ_CST)) {
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
        __atomic_store_n(&s_paused, heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) < PAUSE_BLOCK_B, __ATOMIC_SEQ_CST);
    }
    drain(nowMs);
    if (s_rowCount && nowMs - s_rowSince >= FLUSH_MS) flush();
}

SdState sdState(uint32_t nowMs) {
    if (!enabled()) return SdState::OFF;
    if (!s_card) return SdState::NO_CARD;
    if (s_writeError) return SdState::WRITE_ERROR;
    if (__atomic_load_n(&s_paused, __ATOMIC_SEQ_CST) || s_allocFailed) return SdState::LOW_MEMORY;
    if (!realFix(nowMs)) return SdState::WAITING_FOR_FIX;
    return SdState::LOGGING;
}

const char* sdFileName() { return s_file; }

uint32_t written() { return s_written; }
uint32_t skipped() { return s_skipped; }
uint32_t dropped() { return __atomic_load_n(&s_dropped, __ATOMIC_SEQ_CST); }

void sdWipe() {
    // The buffered rows go without being written.
    s_rowLen = 0; s_rowCount = 0;
    s_file[0] = 0; s_newFile = false;
#if defined(ESP32)
    if (!s_card) return;
    FramePush::busLock();
    // Collect first, then remove, as SdLog::wipe() does. Sixteen a pass,
    // and passes until one comes back short: a card driven with for months
    // holds more than sixteen.
    for (int pass = 0; pass < 64; pass++) {
        File dir = SD.open("/");
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
        for (int i = 0; i < n; i++) SD.remove(victims[i]);
        if (n < 16) break;
    }
    FramePush::busUnlock();
#endif
}

}  // namespace Wardrive
#endif
