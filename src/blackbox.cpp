// SquachWatch-CYD — the black box. See blackbox.h.
#include "blackbox.h"
#include "clock.h"
#include "gnss.h"
#include <Arduino.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

#if __has_include(<esp_flash.h>)
#include <esp_flash.h>
#include <esp_partition.h>
static_assert(sizeof(BlackBox::BattRecord) == 64, "BattRecord must be one 64-byte record");
#define BB_ON_DEVICE 1
#else
#define BB_ON_DEVICE 0
#endif

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "0.0.0"
#endif

namespace BlackBox {
namespace {

#if (defined(SQW_S3) && !defined(SQW_FLASH_8MB)) || defined(CROWPANEL7)   // an 8 MB S3 takes the CYDs' table and address
const uint32_t BASE    = 0x810000;   // the gap after app1; partitions_twatch.csv (both SQW_S3 boards) and partitions_crowpanel7.csv, the same 16 MB shape
// The two C5 boards have tables of their own, and the CYDs' 0x3D0000 sits
// inside an app slot on both (app0 on the NM-CYD-C5's, app1 on the Marauder
// V8's): regionFree() saw it and kept the black box off, so neither board
// had one (found drawing the partition tables, 2026-10-12).
#elif defined(MARAUDER_V8)
const uint32_t BASE    = 0x710000;   // the gap after app1; see partitions_ota_8mb.csv
#elif defined(NM_CYD_C5) || defined(TAB5)
const uint32_t BASE    = 0xC20000;   // the free space after coredump; see partitions_ota_16mb.csv (the Tab5 takes the same table)
#else
const uint32_t BASE    = 0x3D0000;   // the gap after app1; see partitions_ota.csv
#endif
const uint32_t SECTOR  = 4096;
const uint8_t  SECTORS = 32;
const uint32_t SIZE    = SECTORS * SECTOR;
const uint16_t REC     = 64;
const uint16_t PER     = SECTOR / REC - 1;   // slot 0 is the header: 63 records
const uint32_t MAGIC   = 0x58425153u;        // "SQBX"
const uint16_t CHUNK   = 16;                 // records a read: 1 KB of stack
const uint8_t  FORMAT  = 2;   // 2: a boot record keeps a whole version string

const uint8_t KIND_BOOT  = 1;
const uint8_t KIND_DET   = 2;
const uint8_t KIND_CLEAR = 3;
const uint8_t KIND_BATT  = 4;   // watch only; see BattRecord

// ---- the flash itself ------------------------------------------------------
// On the board, the chip. In the emulator, 128 KB of RAM that starts erased.
#if BB_ON_DEVICE
bool flashRead(uint32_t off, uint8_t* out, uint32_t n) {
    return esp_flash_read(esp_flash_default_chip, out, BASE + off, n) == ESP_OK;
}
bool flashWrite(uint32_t off, const uint8_t* in, uint32_t n) {
    return esp_flash_write(esp_flash_default_chip, in, BASE + off, n) == ESP_OK;
}
bool flashErase(uint32_t off, uint32_t n) {
    return esp_flash_erase_region(esp_flash_default_chip, BASE + off, n) == ESP_OK;
}
// Ours only while nothing in the partition table claims any of it and the
// chip reaches past it. A table that grows the app slots into this space
// turns the black box off rather than letting it write over firmware.
bool regionFree() {
    uint32_t chip = 0;
    if (esp_flash_get_size(esp_flash_default_chip, &chip) != ESP_OK || chip < BASE + SIZE) return false;
    bool clear = true;
    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, nullptr);
    for (; it; it = esp_partition_next(it)) {
        const esp_partition_t* p = esp_partition_get(it);
        if (p->flash_chip != esp_flash_default_chip) continue;
        if (p->address < BASE + SIZE && p->address + p->size > BASE) {
            Serial.printf("[blackbox] off: partition %s covers 0x%06lx\n", p->label, (unsigned long)p->address);
            clear = false;
        }
    }
    esp_partition_iterator_release(it);
    return clear;
}
#else
uint8_t* s_sim = nullptr;
uint8_t* simFlash() {
    if (!s_sim) { s_sim = new uint8_t[SIZE]; memset(s_sim, 0xFF, SIZE); }
    return s_sim;
}
bool flashRead(uint32_t off, uint8_t* out, uint32_t n)        { memcpy(out, simFlash() + off, n); return true; }
bool flashWrite(uint32_t off, const uint8_t* in, uint32_t n)  {
    uint8_t* f = simFlash() + off;
    for (uint32_t i = 0; i < n; i++) f[i] &= in[i];     // NOR: writes only clear bits
    return true;
}
bool flashErase(uint32_t off, uint32_t n) { memset(simFlash() + off, 0xFF, n); return true; }
bool regionFree() { return true; }
#endif

uint8_t crc8(const uint8_t* p, size_t n) {
    uint8_t c = 0x5A;
    while (n--) {
        c ^= *p++;
        for (int i = 0; i < 8; i++) c = (c & 0x80) ? (uint8_t)((c << 1) ^ 0x07) : (uint8_t)(c << 1);
    }
    return c;
}
bool erased(const uint8_t* p, size_t n) {
    for (size_t i = 0; i < n; i++) if (p[i] != 0xFF) return false;
    return true;
}

struct __attribute__((packed)) Header {
    uint32_t magic;
    uint8_t  format;
    uint8_t  kind;
    uint16_t rec;
    uint32_t seq;
    uint8_t  pad[51];
    uint8_t  crc;
};

// One ring: a run of sectors, each stamped with a rising sequence number.
struct Ring {
    uint8_t  kind;
    uint8_t  first, count;
    uint32_t seq[SECTORS];     // 0 = not ours (blank, damaged, or someone else's)
    int8_t   head;             // newest sector, as an index into the ring
    uint16_t used;             // slots taken in it, good or torn

    Ring(uint8_t k, uint8_t f, uint8_t n) : kind(k), first(f), count(n), seq(), head(-1), used(0) {}

    uint32_t sectorOff(uint8_t i) const { return (uint32_t)(first + i) * SECTOR; }

    void scan() {
        head = -1; used = 0;
        uint32_t top = 0;
        for (uint8_t i = 0; i < count; i++) {
            Header h;
            seq[i] = 0;
            if (!flashRead(sectorOff(i), (uint8_t*)&h, REC)) continue;
            if (h.magic != MAGIC || h.format != FORMAT || h.kind != kind || h.rec != REC ||
                h.crc != crc8((const uint8_t*)&h, REC - 1) || h.seq == 0) continue;
            seq[i] = h.seq;
            if (h.seq > top) { top = h.seq; head = (int8_t)i; }
        }
        if (head >= 0) used = slotsUsed((uint8_t)head);
    }

    // Past the last slot that holds anything. A record torn by a power cut
    // still takes its slot; its checksum keeps it from being read.
    uint16_t slotsUsed(uint8_t i) const {
        uint8_t buf[REC * CHUNK];
        uint16_t last = 0;
        for (uint16_t s = 0; s < PER; s += CHUNK) {
            const uint16_t n = (PER - s) < CHUNK ? (PER - s) : CHUNK;
            if (!flashRead(sectorOff(i) + REC * (1u + s), buf, (uint32_t)REC * n)) break;
            for (uint16_t k = 0; k < n; k++)
                if (!erased(buf + REC * k, REC)) last = (uint16_t)(s + k + 1);
        }
        return last;
    }

    uint32_t topSeq() const { return head >= 0 ? seq[head] : 0; }

    bool append(uint8_t* rec) {
        rec[REC - 1] = crc8(rec, REC - 1);
        if (head < 0 || used >= PER) {
            // The next sector: one that is not ours yet, else the oldest.
            int8_t pick = -1;
            for (uint8_t i = 0; i < count; i++) {
                if (!seq[i]) { pick = (int8_t)i; break; }
                if (pick < 0 || seq[i] < seq[pick]) pick = (int8_t)i;
            }
            if (pick < 0 || !flashErase(sectorOff((uint8_t)pick), SECTOR)) return false;
            Header h;
            memset(&h, 0, sizeof h);
            h.magic = MAGIC; h.format = FORMAT; h.kind = kind; h.rec = REC;
            h.seq = topSeq() + 1;
            h.crc = crc8((const uint8_t*)&h, REC - 1);
            seq[pick] = 0;
            if (!flashWrite(sectorOff((uint8_t)pick), (const uint8_t*)&h, REC)) return false;
            seq[pick] = h.seq;
            head = pick;
            used = 0;
        }
        const bool ok = flashWrite(sectorOff((uint8_t)head) + REC * (1u + used), rec, REC);
        used++;   // taken either way: a failed write may have cleared bits
        return ok;
    }

    // Newest first. fn gets each record whose checksum holds.
    void walk(bool (*fn)(const uint8_t* rec, void* ctx), void* ctx) const {
        if (head < 0) return;
        bool done[SECTORS] = {};
        uint8_t buf[REC * CHUNK];
        for (;;) {
            int8_t s = -1;
            for (uint8_t i = 0; i < count; i++)
                if (seq[i] && !done[i] && (s < 0 || seq[i] > seq[s])) s = (int8_t)i;
            if (s < 0) return;
            done[s] = true;
            int top = (s == head) ? used : PER;
            while (top > 0) {
                const int n = top < CHUNK ? top : CHUNK;
                const int from = top - n;
                if (!flashRead(sectorOff((uint8_t)s) + REC * (1u + from), buf, (uint32_t)REC * n)) return;
                for (int k = n - 1; k >= 0; k--) {
                    const uint8_t* r = buf + REC * k;
                    if (erased(r, REC) || r[REC - 1] != crc8(r, REC - 1)) continue;
                    if (!fn(r, ctx)) return;
                }
                top = from;
            }
        }
    }
};

Ring s_boots(KIND_BOOT, 0, 2);
#if defined(TWATCH_S3)
// Six sectors of battery samples: 378 of them, two and a half days at one
// every ten minutes. Taken from the detection ring, which the watch's black
// box starts empty anyway.
Ring s_dets (KIND_DET, 2, 24);
Ring s_batt (KIND_BATT, 26, 6);
#else
Ring s_dets (KIND_DET, 2, 30);
#endif
bool     s_ready     = false;
uint16_t s_bootNo    = 1;
uint16_t s_detKept   = 0;
uint8_t  s_crashes   = 0;
bool     s_haveCrash = false;
BootRecord s_lastCrash;

void copyText(char* out, size_t cap, const char* in) {
    memset(out, 0, cap);
    if (in) strncpy(out, in, cap - 1);
}

}  // namespace

bool isCrash(uint8_t r) {
    // PANIC, INT_WDT, TASK_WDT, WDT, BROWNOUT -- see esp_reset_reason_t.
    return r == 4 || r == 5 || r == 6 || r == 7 || r == 9;
}

const char* reasonName(uint8_t r) {
    switch (r) {
        case 1:  return "POWER ON";
        case 2:  return "RESET PIN";
        case 3:  return "RESTART";
        case 4:  return "PANIC";
        case 5:  return "INT WDT";
        case 6:  return "TASK WDT";
        case 7:  return "WDT";
        case 8:  return "SLEEP WAKE";
        case 9:  return "BROWNOUT";
        default: return "UNKNOWN";
    }
}

bool begin() {
    if (s_ready) return true;
    if (!regionFree()) return false;
    s_boots.scan();
    s_dets.scan();
#if defined(TWATCH_S3)
    s_batt.scan();
#endif
    s_ready = true;

    uint16_t newest = 0;
    s_crashes = 0;
    s_haveCrash = false;
    s_boots.walk([](const uint8_t* p, void* ctx) {
        const BootRecord& r = *(const BootRecord*)p;
        if (r.kind != KIND_BOOT) return true;
        uint16_t& top = *(uint16_t*)ctx;
        if (r.boot > top) top = r.boot;
        if (isCrash(r.reason)) {
            if (!s_haveCrash) { s_lastCrash = r; s_haveCrash = true; }
            if (s_crashes < 255) s_crashes++;
        }
        return true;
    }, &newest);
    // Detections carry a boot number too, and outlive the boot ring.
    s_dets.walk([](const uint8_t* p, void* ctx) {
        const DetRecord& r = *(const DetRecord*)p;
        uint16_t& top = *(uint16_t*)ctx;
        if (r.boot > top) top = r.boot;
        return false;   // the newest is enough
    }, &newest);
    s_bootNo = newest == 0xFFFF ? 1 : (uint16_t)(newest + 1);

    s_detKept = 0;
    s_dets.walk([](const uint8_t* p, void*) {
        if (p[0] == KIND_CLEAR) return false;
        if (p[0] == KIND_DET && s_detKept < 0xFFFF) s_detKept++;
        return true;
    }, nullptr);

    Serial.printf("[blackbox] boot %u: %u detections, %u crashes kept\n",
                  (unsigned)s_bootNo, (unsigned)s_detKept, (unsigned)s_crashes);
    return true;
}

bool     ready()          { return s_ready; }
uint16_t bootNumber()     { return s_bootNo; }
uint16_t detectionsKept() { return s_detKept; }

void noteBattery(BattRecord& r) {
#if defined(TWATCH_S3)
    if (!s_ready) return;
    r.kind = KIND_BATT;
    r.boot = s_bootNo;
    s_batt.append((uint8_t*)&r);
#else
    (void)r;
#endif
}

void forEachBattery(bool (*fn)(const BattRecord&, void*), void* ctx) {
#if defined(TWATCH_S3)
    if (!s_ready) return;
    struct W { bool (*fn)(const BattRecord&, void*); void* ctx; } w = { fn, ctx };
    s_batt.walk([](const uint8_t* p, void* c) {
        if (p[0] != KIND_BATT) return true;
        const W& w = *(const W*)c;
        return w.fn(*(const BattRecord*)p, w.ctx);
    }, &w);
#else
    (void)fn; (void)ctx;
#endif
}
uint8_t  crashesKept()    { return s_crashes; }
bool lastCrash(BootRecord& out) {
    if (!s_haveCrash) return false;
    out = s_lastCrash;
    return true;
}

void noteBoot(BootRecord& r) {
    if (!s_ready) return;
    r.kind = KIND_BOOT;
    r.boot = s_bootNo;
    copyText(r.version, sizeof r.version, FIRMWARE_VERSION);
    if (!s_boots.append((uint8_t*)&r)) return;
    if (isCrash(r.reason)) {
        s_lastCrash = r;
        s_haveCrash = true;
        if (s_crashes < 255) s_crashes++;
    }
}

void noteDetection(const Detection& d, bool again) {
    const bool pos = Gnss::fresh(millis()) && !Gnss::faked();
    const Gnss::Fix& f = Gnss::fix();
    noteDetectionAt(d, again, Clock::trusted() ? Clock::nowEpoch() : 0, pos, pos ? f.lat7 : 0, pos ? f.lon7 : 0);
}

void noteDetectionAt(const Detection& d, bool again, uint32_t epoch, bool hasPos, int32_t lat7, int32_t lon7) {
    if (!s_ready) return;
    DetRecord r;
    memset(&r, 0, sizeof r);
    r.kind    = KIND_DET;
    r.type    = (uint8_t)d.type;
    r.conf    = (uint8_t)d.conf;
    r.flags   = (uint8_t)((again ? DET_AGAIN : 0) | DET_PRINTED);
    memcpy(r.mac, d.mac, 6);
    r.rssi    = d.rssi;
    r.channel = d.channel;
    r.hits    = d.hits;
    r.boot    = s_bootNo;
    r.epoch   = epoch;
    r.upSec   = millis() / 1000u;
    copyText(r.vendor, sizeof r.vendor, vendorText(d));
    memcpy(r.name, d.name, sizeof r.name);
    r.name[sizeof r.name - 1] = '\0';
    if (hasPos) {
        r.flags |= DET_POS;
        r.name[11] = '\0';
        memcpy(r.name + 12, &lat7, 4);
        memcpy(r.name + 16, &lon7, 4);
    }
    if (s_dets.append((uint8_t*)&r) && s_detKept < 0xFFFF) s_detKept++;
}

bool detPosition(const DetRecord& r, int32_t& lat7, int32_t& lon7) {
    if (!(r.flags & DET_POS)) return false;
    memcpy(&lat7, r.name + 12, 4);
    memcpy(&lon7, r.name + 16, 4);
    return true;
}

bool lastPosition(const uint8_t mac[6], uint8_t type, int32_t& lat7, int32_t& lon7) {
    struct W { const uint8_t* mac; uint8_t type; bool got; int32_t la, lo; } w = { mac, type, false, 0, 0 };
    forEachDetection([](const DetRecord& r, void* c) {
        W& w = *(W*)c;
        int32_t la, lo;
        if (r.type == w.type && memcmp(r.mac, w.mac, 6) == 0 && detPosition(r, la, lo)) {
            // The walk is newest first, so the first one with a position is the one.
            w.got = true; w.la = la; w.lo = lo;
            return false;
        }
        return true;
    }, &w);
    if (w.got) { lat7 = w.la; lon7 = w.lo; }
    return w.got;
}

void markCleared() {
    if (!s_ready || !s_detKept) return;
    DetRecord r;
    memset(&r, 0, sizeof r);
    r.kind  = KIND_CLEAR;
    r.boot  = s_bootNo;
    r.epoch = Clock::trusted() ? Clock::nowEpoch() : 0;
    if (s_dets.append((uint8_t*)&r)) s_detKept = 0;
}

void forEachDetection(bool (*fn)(const DetRecord&, void*), void* ctx) {
    if (!s_ready) return;
    struct W { bool (*fn)(const DetRecord&, void*); void* ctx; } w = { fn, ctx };
    s_dets.walk([](const uint8_t* p, void* c) {
        if (p[0] == KIND_CLEAR) return false;
        if (p[0] != KIND_DET) return true;
        const W& w = *(const W*)c;
        const DetRecord& r = *(const DetRecord*)p;
        if (r.channel != 0 || (r.flags & DET_PRINTED)) return w.fn(r, w.ctx);
        // A Bluetooth sighting kept before addresses were in printed order:
        // handed on the right way round, so the log, a WATCH set from it and
        // everything else see one order.
        DetRecord f = r;
        for (int b = 0; b < 6; b++) f.mac[b] = r.mac[5 - b];
        return w.fn(f, w.ctx);
    }, &w);
}

uint16_t readDetections(uint16_t from, uint16_t max, DetRecord* out) {
    struct W { uint16_t from, max, seen, got; DetRecord* out; } w = { from, max, 0, 0, out };
    forEachDetection([](const DetRecord& r, void* c) {
        W& w = *(W*)c;
        if (w.seen++ < w.from) return true;
        w.out[w.got++] = r;
        return w.got < w.max;
    }, &w);
    return w.got;
}

uint16_t readDetectionsAt(const uint16_t* at, uint16_t n, DetRecord* out) {
    if (!n) return 0;
    struct W { const uint16_t* at; uint16_t n, seen, got; DetRecord* out; } w = { at, n, 0, 0, out };
    forEachDetection([](const DetRecord& r, void* c) {
        W& w = *(W*)c;
        if (w.seen++ == w.at[w.got]) w.out[w.got++] = r;
        return w.got < w.n;
    }, &w);
    return w.got;
}

void forEachBoot(bool (*fn)(const BootRecord&, void*), void* ctx) {
    if (!s_ready) return;
    struct W { bool (*fn)(const BootRecord&, void*); void* ctx; } w = { fn, ctx };
    s_boots.walk([](const uint8_t* p, void* c) {
        if (p[0] != KIND_BOOT) return true;
        const W& w = *(const W*)c;
        return w.fn(*(const BootRecord*)p, w.ctx);
    }, &w);
}

void wipe() {
    if (!regionFree()) return;
    flashErase(0, SIZE);
    if (!s_ready) return;
    s_boots.scan();
    s_dets.scan();
#if defined(TWATCH_S3)
    s_batt.scan();
#endif
    s_detKept = 0;
    s_crashes = 0;
    s_haveCrash = false;
}

#ifdef BLACKBOX_TEST
void testReopen() { s_ready = false; begin(); }
uint8_t* testFlash() { return simFlash(); }
bool testNextDetectionSlot(uint32_t& offset) {
    if (s_dets.head < 0 || s_dets.used >= PER) return false;
    offset = s_dets.sectorOff((uint8_t)s_dets.head) + REC * (1u + s_dets.used);
    return true;
}
#endif

namespace {
struct DumpSink {
    void (*line)(const char* s, void* ctx);
    void* ctx;
    void printf(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
        char buf[192];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(buf, sizeof buf, fmt, ap);
        va_end(ap);
        line(buf, ctx);
    }
};
}  // namespace

void dump() {
    dumpTo([](const char* s, void*) { Serial.print(s); }, nullptr);
}

void dumpTo(void (*line)(const char* s, void* ctx), void* ctx) {
    DumpSink out = { line, ctx };
    if (!s_ready) { out.printf("[blackbox] off\n"); return; }
    out.printf("[blackbox] boot %u; %u detections, %u crashes kept\n",
               (unsigned)s_bootNo, (unsigned)s_detKept, (unsigned)s_crashes);
    out.printf("boot,reason,epoch,version,up_s,heap_free,heap_block,screen,task,pc,cause\n");
    forEachBoot([](const BootRecord& r, void* c) {
        ((DumpSink*)c)->printf("%u,%s,%lu,%s,%lu,%lu,%lu,%u,%s,%08lx,%lu\n", (unsigned)r.boot, reasonName(r.reason),
                      (unsigned long)r.epoch, r.version, (unsigned long)r.upSec,
                      (unsigned long)r.heapFree, (unsigned long)r.heapBlock, (unsigned)r.screen,
                      (r.flags & BOOT_DUMP) ? r.task : "", (unsigned long)r.pc, (unsigned long)r.cause);
        return true;
    }, &out);
    out.printf("boot,epoch,up_s,type,mac,rssi,channel,hits,again,vendor,name,lat,lon\n");
    // Everything the ring holds, CLR or not. The LOG screen stops at the mark
    // a CLR leaves (a restart must not bring back what was cleared), but this
    // is the record: it prints the mark as a line and carries on, so a
    // morning cleared at lunch is still here to read (2026-09-26).
    s_dets.walk([](const uint8_t* p, void* c) {
        DumpSink& o = *(DumpSink*)c;
        if (p[0] == KIND_CLEAR) {
            const DetRecord& cl = *(const DetRecord*)p;
            o.printf("# LOG CLEARED here (boot %u, epoch %lu): the rows below were hidden from the LOG screen\n",
                     (unsigned)cl.boot, (unsigned long)cl.epoch);
            return true;
        }
        if (p[0] != KIND_DET) return true;
        const DetRecord& r = *(const DetRecord*)p;
        // Where it was, as two decimal-degree columns, empty when it wasn't known.
        char pos[32] = ",";
        int32_t la, lo;
        if (detPosition(r, la, lo))
            snprintf(pos, sizeof pos, "%s%ld.%07ld,%s%ld.%07ld",
                     la < 0 ? "-" : "", labs((long)la) / 10000000L, labs((long)la) % 10000000L,
                     lo < 0 ? "-" : "", labs((long)lo) / 10000000L, labs((long)lo) % 10000000L);
        o.printf("%u,%lu,%lu,%s,%02x:%02x:%02x:%02x:%02x:%02x,%d,%u,%u,%u,%s,%s,%s\n",
                 (unsigned)r.boot, (unsigned long)r.epoch, (unsigned long)r.upSec,
                 detectionTypeName((DetectionType)r.type),
                 r.mac[0], r.mac[1], r.mac[2], r.mac[3], r.mac[4], r.mac[5],
                 (int)r.rssi, (unsigned)r.channel, (unsigned)r.hits,
                 (r.flags & DET_AGAIN) ? 1u : 0u, r.vendor, r.name, pos);
        return true;
    }, &out);
    out.printf("[blackbox] end\n");
}

}  // namespace BlackBox
