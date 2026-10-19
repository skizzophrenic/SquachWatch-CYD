// SquachWatch-CYD — wardriving capture and storage. See wardrive.h.
#include "wardrive.h"
#include "wardrive_capture.h"
#include "gnss.h"
#include <Arduino.h>
#include <Preferences.h>
#include <string.h>
#include <math.h>

// The CYD GPS builds take the same API from wardrive_sd.cpp.
#if !defined(CYD_GPS)

#if __has_include(<esp_flash.h>) && defined(TWATCH_S3)
#include <esp_flash.h>
#include <esp_partition.h>
#define WD_ON_DEVICE 1
#else
#define WD_ON_DEVICE 0
#endif

#if defined(WAVESHARE_206) && defined(ESP32)
#include <SD_MMC.h>
#include <time.h>
#include "clock.h"
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "unknown"
#endif
#endif

namespace Wardrive {

namespace {

// ---- where ----------------------------------------------------------------
// After the coredump partition (0x830000 + 64 KB) to the end of the 16 MB
// chip on the S3 watches: 1984 sectors, 63 records each. The emulator and
// the tests get 16 sectors of RAM, which is plenty to wrap in a test.
#if WD_ON_DEVICE
const uint32_t BASE    = 0x840000;
const uint32_t SECTORS = (0x1000000 - BASE) / 4096;
#else
const uint32_t BASE    = 0;
const uint32_t SECTORS = 16;
#endif
const uint32_t SECTOR  = 4096;
const uint16_t REC     = sizeof(Record);
const uint16_t PER     = SECTOR / REC - 1;       // slot 0 is the sector's header
const uint32_t MAGIC   = 0x44524157;             // "WARD"
const uint8_t  FORMAT  = 1;

#if WD_ON_DEVICE
bool flashRead(uint32_t off, void* out, uint32_t n)        { return esp_flash_read(esp_flash_default_chip, out, BASE + off, n) == ESP_OK; }
bool flashWrite(uint32_t off, const void* in, uint32_t n)  { return esp_flash_write(esp_flash_default_chip, in, BASE + off, n) == ESP_OK; }
bool flashErase(uint32_t off, uint32_t n)                  { return esp_flash_erase_region(esp_flash_default_chip, BASE + off, n) == ESP_OK; }
bool regionFree() {
    uint32_t chip = 0;
    if (esp_flash_get_size(esp_flash_default_chip, &chip) != ESP_OK || chip < BASE + SECTORS * SECTOR) return false;
    bool clear = true;
    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, nullptr);
    for (; it; it = esp_partition_next(it)) {
        const esp_partition_t* p = esp_partition_get(it);
        if (p->flash_chip == esp_flash_default_chip && p->address < BASE + SECTORS * SECTOR && p->address + p->size > BASE) {
            Serial.printf("[wardrive] off: partition %s covers 0x%06lx\n", p->label, (unsigned long)p->address);
            clear = false;
        }
    }
    esp_partition_iterator_release(it);
    return clear;
}
#else
uint8_t* s_sim = nullptr;
uint8_t* simFlash() { if (!s_sim) { s_sim = new uint8_t[SECTORS * SECTOR]; memset(s_sim, 0xFF, SECTORS * SECTOR); } return s_sim; }
bool flashRead(uint32_t off, void* out, uint32_t n)       { memcpy(out, simFlash() + off, n); return true; }
bool flashWrite(uint32_t off, const void* in, uint32_t n) {
    uint8_t* f = simFlash() + off; const uint8_t* s = (const uint8_t*)in;
    for (uint32_t i = 0; i < n; i++) f[i] &= s[i];
    return true;
}
bool flashErase(uint32_t off, uint32_t n) { memset(simFlash() + off, 0xFF, n); return true; }
bool regionFree() { return true; }
#endif

uint8_t crc8(const uint8_t* p, size_t n) {
    uint8_t c = 0x5A;
    while (n--) { c ^= *p++; for (int i = 0; i < 8; i++) c = (c & 0x80) ? (uint8_t)((c << 1) ^ 0x07) : (uint8_t)(c << 1); }
    return c;
}
bool blank(const uint8_t* p, size_t n) { for (size_t i = 0; i < n; i++) if (p[i] != 0xFF) return false; return true; }

struct __attribute__((packed)) Header {
    uint32_t magic;
    uint8_t  format;
    uint8_t  pad0;
    uint16_t rec;
    uint32_t seq;
    uint8_t  pad[51];
    uint8_t  crc;
};
static_assert(sizeof(Header) == 64, "the header is one slot");

// ---- the ring ---------------------------------------------------------------
// Sectors are used strictly in order and wrap, so the oldest is always the
// one after the newest: only the newest needs finding at boot.
bool     s_ready = false, s_on = false;
int32_t  s_head = -1;          // newest sector, -1 when empty
uint32_t s_headSeq = 0;
uint16_t s_used = 0;           // slots taken in the head sector
uint32_t s_full = 0;           // full sectors behind the head (<= SECTORS-1)
uint32_t s_written = 0, s_skipped = 0, s_dropped = 0;

uint32_t off(uint32_t sec, uint16_t slot) { return sec * SECTOR + (uint32_t)slot * REC; }

bool readHeader(uint32_t sec, uint32_t& seq) {
    uint8_t buf[64];
    if (!flashRead(off(sec, 0), buf, REC)) return false;
    const Header& h = *(const Header*)buf;
    if (h.magic != MAGIC || h.format != FORMAT || h.rec != REC || buf[REC - 1] != crc8(buf, REC - 1) || !h.seq) return false;
    seq = h.seq;
    return true;
}

uint16_t slotsUsed(uint32_t sec) {
    uint8_t buf[64];
    uint16_t last = 0;
    for (uint16_t s = 0; s < PER; s++) {
        if (!flashRead(off(sec, (uint16_t)(1 + s)), buf, REC)) break;
        if (!blank(buf, REC)) last = (uint16_t)(s + 1);
        else break;                         // written strictly in order
    }
    return last;
}

bool openSector(uint32_t sec) {
    if (!flashErase(sec * SECTOR, SECTOR)) return false;
    uint8_t buf[64];
    memset(buf, 0, sizeof buf);
    Header& h = *(Header*)buf;
    h.magic = MAGIC; h.format = FORMAT; h.rec = REC; h.seq = s_headSeq + 1;
    buf[REC - 1] = crc8(buf, REC - 1);
    if (!flashWrite(off(sec, 0), buf, REC)) return false;
    if (s_head >= 0) s_full = s_full + 1 < SECTORS ? s_full + 1 : SECTORS - 1;
    s_head = (int32_t)sec; s_headSeq = h.seq; s_used = 0;
    return true;
}

bool append(Record& r) {
    if (!s_ready) return false;
    r.crc = crc8((const uint8_t*)&r, REC - 1);
    if (s_head < 0 || s_used >= PER) {
        const uint32_t next = s_head < 0 ? 0 : ((uint32_t)s_head + 1) % SECTORS;
        if (!openSector(next)) return false;
    }
    const bool ok = flashWrite(off((uint32_t)s_head, (uint16_t)(1 + s_used)), &r, REC);
    s_used++;
    return ok;
}

// ---- the queue --------------------------------------------------------------
// Pending, Q and QN are in wardrive_capture.h.
// Made by begin(), which only a board that can wardrive calls. As plain
// globals they were 3 KB of RAM on every CYD, none of which has a GPS
// (found in the firmware's symbol table, 2026-10-03). Set before s_ready,
// which is what the radio tasks check first.
Q* s_wifiQ = nullptr;
Q* s_bleQ  = nullptr;

// ---- not the same thing twice from the same place ------------------------------
// Seen, AGAIN_S, AGAIN_M and metres() are in wardrive_capture.h. The watch
// keeps its own 1024-entry table, by hash.
const uint16_t SEEN_N = 1024;
Seen* s_seen = nullptr;

uint16_t slotFor(const uint8_t* mac, uint8_t kind) {
    uint32_t h = 2166136261u ^ kind;
    for (int i = 0; i < 6; i++) { h ^= mac[i]; h *= 16777619u; }
    return (uint16_t)(h % SEEN_N);
}

bool worthWriting(const Pending& p, const Gnss::Fix& f, uint32_t epoch) {
    if (!s_seen) { s_seen = new Seen[SEEN_N]; memset(s_seen, 0, sizeof(Seen) * SEEN_N); }
    Seen& s = s_seen[slotFor(p.mac, p.kind)];
    const bool same = s.used && s.kind == p.kind && !memcmp(s.mac, p.mac, 6);
    if (same && epoch - s.epoch < AGAIN_S && metres(s.lat7, s.lon7, f.lat7, f.lon7) < AGAIN_M) return false;
    memcpy(s.mac, p.mac, 6); s.kind = p.kind; s.used = 1;
    s.lat7 = f.lat7; s.lon7 = f.lon7; s.epoch = epoch;
    return true;
}

#if defined(WAVESHARE_206) && defined(ESP32)
// ---- the Waveshare 2.06's SD writer --------------------------------------------
// No GPS on this watch. Every sighting goes to the card with the watch's UTC
// clock and blank coordinates (0,0), as a WigleWifi-1.6 file; the phone records
// a GPX track at the same time, and tools/wigle_merge.py puts the two
// together by time into the file that uploads. With no place to compare, a
// device is written again after WS_AGAIN_S rather than after 50 m: a few
// rows per pass gives WiGLE what it needs to place a network.
const uint32_t WS_AGAIN_S  = 15;
const uint16_t WS_ROWS_B   = 4096;
const uint16_t WS_ROW_MAX  = 320;
const uint32_t WS_FLUSH_MS = 2000;
// A position from the phone (phone_gps.h) places a row while it is this
// fresh; the phone sends one a second. Older, the row goes out at 0,0 for
// the GPX merge to place.
const uint32_t WS_FIX_MS   = 5000;
uint32_t s_wsPlaced = 0;       // rows written with the phone's position, this boot
char*    s_wsRows = nullptr;
uint16_t s_wsLen = 0, s_wsCount = 0;
uint32_t s_wsSince = 0;
char     s_wsFile[32] = "";
uint8_t  s_wsState = 0;        // 0 off, 1 no card, 2 no clock, 3 logging
bool     s_wsWarned = false;
uint32_t s_wsWifi = 0;         // WiFi rows among those queued for the card, this boot

bool wsCard() { return SD_MMC.cardType() != CARD_NONE; }

void wsFlush() {
    if (!s_wsRows || !s_wsLen) return;
    bool ok = false;
    const bool fresh = !SD_MMC.exists(s_wsFile);
    File f = SD_MMC.open(s_wsFile, FILE_APPEND);
    if (f) {
        ok = true;
        if (fresh) {
            char h[512];
            const size_t n = headerLines(h, sizeof h, FIRMWARE_VERSION, "waveshare-206", "ESP32-S3-Touch-AMOLED-2.06", "Waveshare");
            ok = n && f.write((const uint8_t*)h, n) == n;
        }
        ok = ok && f.write((const uint8_t*)s_wsRows, s_wsLen) == s_wsLen;
        f.close();
    }
    if (ok) s_written += s_wsCount;
    else {
        s_dropped += s_wsCount;
        if (!s_wsWarned) { s_wsWarned = true; Serial.printf("[wardrive] cannot write %s\n", s_wsFile); }
    }
    s_wsLen = 0; s_wsCount = 0;
}

bool wsWorth(const Pending& p, uint32_t epoch) {
    if (!s_seen) { s_seen = new Seen[SEEN_N]; memset(s_seen, 0, sizeof(Seen) * SEEN_N); }
    Seen& s = s_seen[slotFor(p.mac, p.kind)];
    if (s.used && s.kind == p.kind && !memcmp(s.mac, p.mac, 6) && epoch - s.epoch < WS_AGAIN_S) return false;
    memcpy(s.mac, p.mac, 6); s.kind = p.kind; s.used = 1; s.epoch = epoch;
    return true;
}

void wsTick(uint32_t nowMs) {
    const bool card = s_on && wsCard();
    const uint32_t epoch = Clock::nowEpoch();
    s_wsState = !s_on ? 0 : !card ? 1 : !epoch ? 2 : 3;
    if (s_wsState != 3) {
        // Off, no card or no clock: what is buffered goes out if it can, the
        // queues are emptied unwritten, and off ends the session's file.
        if (card && s_wsLen) wsFlush();
        Pending p;
        while (s_wifiQ->pop(p) || s_bleQ->pop(p)) {}
        if (!s_on) { s_wsFile[0] = 0; s_wsWarned = false; }
        return;
    }
    if (!s_wsRows) { s_wsRows = (char*)ps_malloc(WS_ROWS_B); if (!s_wsRows) return; }
    if (!s_wsFile[0]) {
        const time_t t = (time_t)epoch;
        struct tm tm;
        gmtime_r(&t, &tm);
        snprintf(s_wsFile, sizeof s_wsFile, "/wigle-%04d%02d%02d-%02d%02d.csv",
                 tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min);
        Serial.printf("[wardrive] logging to %s\n", s_wsFile);
    }
    const Gnss::Fix& f = Gnss::fix();
    // Signed, as Gnss::fresh() is: a fix that landed after nowMs was taken
    // would otherwise read as 49 days old.
    const bool placed = f.valid && !Gnss::faked() && (int32_t)(nowMs - f.atMs) < (int32_t)WS_FIX_MS;
    Pending p;
    for (int n = 0; n < 2 * QN; n++) {
        if (!(s_wifiQ->pop(p) || s_bleQ->pop(p))) break;
        if (!wsWorth(p, epoch)) { s_skipped++; continue; }
        Record r;
        memset(&r, 0, sizeof r);
        r.kind = p.kind; r.flags = p.flags;
        memcpy(r.mac, p.mac, 6);
        r.rssi = p.rssi; r.channel = p.channel; r.auth = p.auth;
        r.epoch = epoch;
        if (placed) { r.lat7 = f.lat7; r.lon7 = f.lon7; r.altM = f.altM; r.accM = f.accM; s_wsPlaced++; }
        r.nameLen = p.nameLen; memcpy(r.name, p.name, p.nameLen);
        if (WS_ROWS_B - s_wsLen < WS_ROW_MAX) wsFlush();
        const size_t w = csvRow(r, s_wsRows + s_wsLen, WS_ROWS_B - s_wsLen);
        if (!w) { s_dropped++; continue; }
        if (!s_wsCount) s_wsSince = nowMs;
        s_wsLen = (uint16_t)(s_wsLen + w);
        s_wsCount++;
        if (r.kind == KIND_WIFI) s_wsWifi++;
    }
    if (s_wsCount && nowMs - s_wsSince >= WS_FLUSH_MS) wsFlush();
}

void wsWipe() {
    s_wsLen = 0; s_wsCount = 0; s_wsFile[0] = 0;
    if (!wsCard()) return;
    for (int pass = 0; pass < 64; pass++) {
        File dir = SD_MMC.open("/");
        if (!dir) break;
        char victims[16][40];
        int n = 0;
        for (File f = dir.openNextFile(); f && n < 16; f = dir.openNextFile()) {
            const char* base = f.name();
            for (const char* q = base; *q; q++) if (*q == '/') base = q + 1;
            const size_t len = strlen(base);
            if (!strncmp(base, "wigle-", 6) && len > 4 && len < 36 && !strcmp(base + len - 4, ".csv"))
                snprintf(victims[n++], sizeof victims[0], "/%s", base);
            f.close();
        }
        dir.close();
        for (int i = 0; i < n; i++) SD_MMC.remove(victims[i]);
        if (n < 16) break;
    }
}
#endif

Preferences s_prefs;

}  // namespace

// ---- the API ------------------------------------------------------------------

bool begin() {
    s_ready = false; s_head = -1; s_headSeq = 0; s_used = 0; s_full = 0;
    if (!regionFree()) return false;
    uint32_t top = 0, valid = 0;
    for (uint32_t i = 0; i < SECTORS; i++) {
        uint32_t seq;
        if (!readHeader(i, seq)) continue;
        valid++;
        if (seq > top) { top = seq; s_head = (int32_t)i; }
    }
    if (s_head >= 0) {
        s_headSeq = top;
        s_used = slotsUsed((uint32_t)s_head);
        s_full = valid ? valid - 1 : 0;
    }
    if (!s_wifiQ) s_wifiQ = new Q();
    if (!s_bleQ)  s_bleQ  = new Q();
    s_ready = true;
    if (s_prefs.begin("wardrive", false)) { s_on = s_prefs.getBool("on", false); s_prefs.end(); }
    Serial.printf("[wardrive] %lu records kept of %lu, %s\n", (unsigned long)count(), (unsigned long)capacity(), s_on ? "ON" : "off");
    return true;
}

void setEnabled(bool on) {
    s_on = on;
    if (s_prefs.begin("wardrive", false)) { s_prefs.putBool("on", on); s_prefs.end(); }
}
bool enabled() { return s_on; }

void noteWifi(const uint8_t* bssid, const char* ssid, uint16_t auth, uint8_t channel, int8_t rssi) {
    if (!s_on || !s_ready || !bssid) return;
    Pending p;
    memset(&p, 0, sizeof p);
    p.kind = KIND_WIFI;
    memcpy(p.mac, bssid, 6);
    p.rssi = rssi; p.channel = channel; p.auth = auth;
    if (ssid) { size_t n = strnlen(ssid, 32); memcpy(p.name, ssid, n); p.nameLen = (uint8_t)n; }
    if (!s_wifiQ->push(p)) s_dropped++;
}

void noteBle(const uint8_t* mac, const char* name, int8_t rssi, bool haveMfgr, uint16_t mfgr) {
    if (!s_on || !s_ready || !mac) return;
    Pending p;
    memset(&p, 0, sizeof p);
    p.kind = KIND_BLE;
    memcpy(p.mac, mac, 6);
    p.rssi = rssi;
    if (haveMfgr) { p.flags |= F_HAS_MFGR; p.auth = mfgr; }
    if (name) { size_t n = strnlen(name, 32); memcpy(p.name, name, n); p.nameLen = (uint8_t)n; }
    if (!s_bleQ->push(p)) s_dropped++;
}

void tick(uint32_t nowMs) {
    if (!s_wifiQ || !s_bleQ) return;        // begin() never ran: nothing can have been queued
#if defined(WAVESHARE_206) && defined(ESP32)
    wsTick(nowMs);
    return;
#endif
    Pending p;
    // Emptied either way, so a queue filled while there was no fix does not
    // turn into rows stamped with the first fix that arrives later.
    const bool fix = s_on && s_ready && Gnss::fresh(nowMs);
    const Gnss::Fix& f = Gnss::fix();
    const uint32_t epoch = f.epoch ? f.epoch + (nowMs - f.atMs) / 1000u : 0;
    for (int n = 0; n < 2 * QN; n++) {
        const bool got = s_wifiQ->pop(p) || s_bleQ->pop(p);
        if (!got) break;
        if (!fix || !epoch) continue;
        if (!worthWriting(p, f, epoch)) { s_skipped++; continue; }
        Record r;
        memset(&r, 0, sizeof r);
        r.kind = p.kind; r.flags = (uint8_t)(p.flags | (Gnss::faked() ? F_FAKE : 0));
        memcpy(r.mac, p.mac, 6);
        r.rssi = p.rssi; r.channel = p.channel; r.auth = p.auth;
        r.lat7 = f.lat7; r.lon7 = f.lon7; r.altM = f.altM; r.accM = f.accM;
        r.epoch = epoch;
        r.nameLen = p.nameLen; memcpy(r.name, p.name, p.nameLen);
        if (append(r)) s_written++;
    }
}

bool     ready()    { return s_ready; }
uint32_t capacity() { return SECTORS * PER; }
uint32_t count()    { return s_head < 0 ? 0 : s_full * PER + s_used; }
uint32_t written()  { return s_written; }
uint32_t skipped()  { return s_skipped; }
uint32_t dropped()  { return s_dropped; }

void forEach(bool (*fn)(const Record& r, void* ctx), void* ctx) {
    if (!s_ready || s_head < 0) return;
    const uint32_t n = s_full + 1;
    uint32_t sec = ((uint32_t)s_head + SECTORS - s_full) % SECTORS;
    uint8_t buf[64];
    for (uint32_t k = 0; k < n; k++, sec = (sec + 1) % SECTORS) {
        uint32_t seq;
        if (!readHeader(sec, seq)) continue;
        const uint16_t top = (sec == (uint32_t)s_head) ? s_used : PER;
        for (uint16_t s = 0; s < top; s++) {
            if (!flashRead(off(sec, (uint16_t)(1 + s)), buf, REC)) return;
            if (blank(buf, REC) || buf[REC - 1] != crc8(buf, REC - 1)) continue;
            if (!fn(*(const Record*)buf, ctx)) return;
        }
    }
}

void clear() {
    if (!s_ready) return;
    // Only what was written: a run of sectors ending at the head.
    if (s_head >= 0) {
        uint32_t sec = ((uint32_t)s_head + SECTORS - s_full) % SECTORS;
        for (uint32_t k = 0; k <= s_full; k++, sec = (sec + 1) % SECTORS) flashErase(sec * SECTOR, SECTOR);
    }
    s_head = -1; s_headSeq = 0; s_used = 0; s_full = 0;
    if (s_seen) memset(s_seen, 0, sizeof(Seen) * SEEN_N);
#if defined(WAVESHARE_206) && defined(ESP32)
    wsWipe();
#endif
}

#if defined(WAVESHARE_206) && defined(ESP32)
uint8_t     wsState()    { return s_wsState; }
const char* wsFileName() { return s_wsFile; }
uint32_t    wsWifiRows() { return s_wsWifi; }
uint32_t    wsPlacedRows() { return s_wsPlaced; }
#endif

}  // namespace Wardrive

#endif  // !CYD_GPS
