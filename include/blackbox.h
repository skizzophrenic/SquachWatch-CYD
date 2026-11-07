// SquachWatch-CYD — the black box
//
// What the board saw and how it last went down, kept across restarts without
// an SD card. It lives in the 128 KB the partition table leaves unassigned
// between app1 and the core dump (0x3D0000-0x3F0000): no table change, so no
// board needs a USB reinstall to have it. See partitions_ota.csv.
//
// Two rings of 4 KB sectors, each sector a header and 63 records of 64 bytes:
//   sectors 0-1   boots: one record a boot, with the crash details when the
//                 boot was a crash -- about a hundred boots back
//   sectors 2-31  detections: one record a sighting, first sight or back
//                 after going quiet -- about 1,800 back
// A full ring erases its oldest sector. Nothing here is trusted until its
// header and checksum say it is ours: before v1.7.0 this space was SPIFFS,
// so an old board arrives with somebody else's bytes in it.
//
// Called from loop() and setup() only, never from the Bluetooth host task: a
// flash write stalls both cores, and a sector erase stalls them for tens of
// milliseconds.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "state.h"

namespace BlackBox {

// One sighting, as the flash keeps it. 64 bytes on flash, checksum included.
struct __attribute__((packed)) DetRecord {
    uint8_t  kind;          // internal: a sighting, or the mark a CLR leaves
    uint8_t  type;          // DetectionType
    uint8_t  conf;          // Confidence
    uint8_t  flags;         // DET_AGAIN, DET_PRINTED
    uint8_t  mac[6];
    int8_t   rssi;
    uint8_t  channel;
    uint16_t hits;
    uint16_t boot;          // which boot saw it -- see bootNumber()
    uint32_t epoch;         // wall-clock seconds; 0 when the clock was not set
    uint32_t upSec;         // seconds since that boot
    char     vendor[16];
    char     name[20];
    uint8_t  pad[3];
    uint8_t  crc;
};
static const uint8_t DET_AGAIN   = 0x01;   // came back after going quiet
// The address is in printed order (v1.25.0 on, issue #22). A Bluetooth record
// without it (channel 0) was kept backwards, and forEachDetection() turns it
// round as it hands it on.
static const uint8_t DET_PRINTED = 0x02;
// Where it was: the GPS had a fresh position when it was noted -- a board's own
// module, or a phone in the squad sending its own (gnss.h). The position takes
// the last eight bytes of `name`, which is cut to eleven characters to make
// room; a record without the flag keeps the whole twenty. detPosition() reads it.
static const uint8_t DET_POS     = 0x04;
bool detPosition(const DetRecord& r, int32_t& lat7, int32_t& lon7);
// The newest position kept for this device, if any record of it has one: what
// the LOG screen's long-press shows as "seen at". Reads the whole ring, so it
// is for a tap, not for a frame.
bool lastPosition(const uint8_t mac[6], uint8_t type, int32_t& lat7, int32_t& lon7);

// One boot. When it followed a crash, the crash is in here too, from the
// breadcrumb and the core dump summary (see crashReportInit() in main.cpp).
struct __attribute__((packed)) BootRecord {
    uint8_t  kind;
    uint8_t  reason;        // esp_reset_reason_t of THIS boot: how the last one ended
    uint8_t  screen;        // AppState the crashed boot was on
    uint8_t  flags;         // BOOT_CRUMB / BOOT_DUMP / BOOT_DUMP_OLDER
    uint16_t boot;
    uint16_t pad0;
    uint32_t epoch;         // when this boot wrote it; 0 when the clock was not set
    uint32_t upSec;         // how long the crashed boot had been up
    uint32_t heapFree;
    uint32_t heapBlock;
    uint32_t pc;            // where it died, from the core dump
    uint32_t cause;
    char     version[16];   // the firmware that wrote this record
    char     task[15];      // 12 was a version cut off mid-word on the bench
    uint8_t  crc;
};
// A battery sample: the watch's power chip, read every ten minutes and at
// the moments the slope changes (boot, USB in or out, screen asleep or
// awake). Voltage as well as the chip's percentage, because the percentage
// is a guess with big steps; the state bits say what the watch was doing,
// which is what turns a slope into a cause. Watch only; the CYDs have no
// gauge and no ring for it.
struct __attribute__((packed)) BattRecord {
    uint8_t  kind;
    uint8_t  pct;           // the chip's gauge, 0..100
    uint8_t  flags;         // BATT_*
    uint8_t  why;           // BATT_WHY_*: what prompted the sample
    uint16_t mv;            // battery voltage, millivolts
    uint16_t boot;
    uint32_t epoch;         // 0 when the clock was not set
    uint32_t upSec;
    uint8_t  cpuMhz10;      // CPU clock / 10
    // What the radios had heard by then, and how warm the chip was: two
    // samples with the same counts are a deaf spell, and these say when it
    // started and what the watch was doing. Zero in older records.
    int8_t   chipC;         // the chip's own sensor, degrees C
    uint8_t  steady;        // 1 when STEADY POWER held DC1 in PWM
    uint32_t adverts;       // Bluetooth adverts heard since boot
    uint32_t frames;        // WiFi frames heard since boot
    uint8_t  pad[36];
    uint8_t  crc;
};
static const uint8_t BATT_USB       = 0x01;   // on the cable
static const uint8_t BATT_CHARGING  = 0x02;
static const uint8_t BATT_SCREEN_ON = 0x04;
static const uint8_t BATT_RADIOS_ON = 0x08;
static const uint8_t BATT_WHY_TIMER  = 0;
static const uint8_t BATT_WHY_BOOT   = 1;
static const uint8_t BATT_WHY_USB    = 2;     // the cable came or went
static const uint8_t BATT_WHY_SCREEN = 3;     // the screen slept or woke
static const uint8_t BATT_WHY_RESET  = 4;     // RADIO RESET tapped: the watch power cycles next
static const uint8_t BATT_WHY_HEAL   = 5;     // the self-heal found Bluetooth deaf: power cycles next
void noteBattery(BattRecord& r);
void forEachBattery(bool (*fn)(const BattRecord& r, void* ctx), void* ctx);   // newest first

static const uint8_t BOOT_CRUMB      = 0x01;
static const uint8_t BOOT_DUMP       = 0x02;
static const uint8_t BOOT_DUMP_OLDER = 0x04;

// Finds the rings and counts what is in them. False when the space is not
// safe to use -- a partition table that has given it to something, or a
// flash chip too small -- and then every call below does nothing.
bool begin();
bool ready();

// The number this boot writes on its records, one past the last boot kept.
uint16_t bootNumber();
// Writes this boot's record; fills in the boot number, version and checksum.
void noteBoot(BootRecord& r);

void noteDetection(const Detection& d, bool again);
// The same, with the time and place given rather than read now: for the phone
// app's emulator, which replays the phone's saved catches into its LOG when it
// starts (sim/main_wasm.cpp, sw_log_catch). Nothing on a board calls it.
void noteDetectionAt(const Detection& d, bool again, uint32_t epoch, bool hasPos, int32_t lat7, int32_t lon7);
// LOG's CLR: a mark, not an erase. What came before it is not shown again,
// and the ring wears it away like anything else.
void markCleared();

// Newest first, stopping at the last CLR. Return false to stop early.
void forEachDetection(bool (*fn)(const DetRecord& r, void* ctx), void* ctx);
// A window of that same list, newest first: skips `from`, fills up to `max`,
// returns how many it filled. What the LOG screen scrolls through once it
// runs off the end of the rows held in RAM.
uint16_t readDetections(uint16_t from, uint16_t max, DetRecord* out);
// The records at these newest-first positions, which must be ascending, in
// one walk. Returns how many it found (fewer if some are gone). The LOG's
// de-duplicated rows are not contiguous in flash, so a page of them is read
// this way.
uint16_t readDetectionsAt(const uint16_t* at, uint16_t n, DetRecord* out);
void forEachBoot(bool (*fn)(const BootRecord& r, void* ctx), void* ctx);

uint16_t detectionsKept();              // since the last CLR
uint8_t  crashesKept();
bool     lastCrash(BootRecord& out);    // the newest crash kept, if any
bool     isCrash(uint8_t reason);
const char* reasonName(uint8_t reason); // "PANIC", "TASK WDT", ...

// The security wipe: every sector, really erased.
void wipe();

// BLACKBOX on the console: both rings, as text.
void dump();
// The same text, a line at a time (each with its '\n'), to anywhere: the
// WiFi log share (log_share.cpp) sends it to a browser as blackbox.csv.
void dumpTo(void (*line)(const char* s, void* ctx), void* ctx);

#ifdef BLACKBOX_TEST
// test/blackbox_test.cpp only: forget what is in RAM and scan again, as a
// restart would; the emulator's flash; where the next sighting will land.
void     testReopen();
uint8_t* testFlash();
bool     testNextDetectionSlot(uint32_t& offset);
#endif

}  // namespace BlackBox
