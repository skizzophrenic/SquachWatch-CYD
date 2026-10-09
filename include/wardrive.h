// SquachWatch-CYD — wardriving: every network and Bluetooth device heard, with
// where, kept in flash and handed out as a WiGLE file.
//
// Separate from detection on purpose. Detection is about the handful of
// things worth an alert, and its log is a ring of 64 on a screen. This is the
// opposite: everything the radios hear while a GPS has a fix, deduplicated
// only enough to spare the flash, and never shown one row at a time -- it
// leaves the watch as a WigleWifi-1.6 CSV that uploads to wigle.net as it is.
//
// Where it lives: the T-Watch keeps sightings in raw flash. Its 16 MB chip
// has about 7.5 MB that nothing uses, after the coredump partition. It is
// written raw, like the black box, so taking it needs no partition-table
// change -- which an update over the air could never make. The CYD GPS
// builds have 4 MB and two app slots, so they write sightings straight to
// the SD card as /wigle-YYYYMMDD-HHMM.csv (wardrive_sd.cpp).
//
// The record and its CSV line are plain C++ (wardrive_fmt.cpp) so the host
// tests check the file WiGLE will read, byte for byte.
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace Wardrive {

enum : uint8_t { KIND_WIFI = 1, KIND_BLE = 2 };
enum : uint8_t {
    F_FAKE     = 1u << 0,   // written against Gnss::fake(): a bench row, never uploaded
    F_HAS_MFGR = 1u << 1,   // BLE: `auth` holds the manufacturer's company ID
};

// One sighting, 64 bytes on flash, checksum last.
struct __attribute__((packed)) Record {
    uint8_t  kind;          // KIND_WIFI or KIND_BLE
    uint8_t  flags;
    uint8_t  mac[6];        // printed order
    int8_t   rssi;
    uint8_t  channel;       // WiFi channel; 0 for Bluetooth
    uint16_t auth;          // WiFi: WifiAuth bits. BLE with F_HAS_MFGR: company ID
    int32_t  lat7, lon7;    // degrees x 10^7
    int16_t  altM;
    uint8_t  accM;
    uint8_t  nameLen;       // bytes of `name` in use, 0..32
    uint32_t epoch;         // UTC seconds
    char     name[32];      // SSID, or the device's advertised name; not terminated
    uint8_t  pad[3];
    uint8_t  crc;
};
static_assert(sizeof(Record) == 64, "a wardrive record is 64 bytes on flash");

// ---- the file (wardrive_fmt.cpp) ------------------------------------------
// The two header lines. `version` is the firmware's, `board` its build name,
// `model` and `brand` the hardware's.
size_t headerLines(char* out, size_t n, const char* version, const char* board, const char* model, const char* brand);
// One CSV line with its "\n". 0 if it did not fit.
size_t csvRow(const Record& r, char* out, size_t n);
// "YYYY-MM-DD hh:mm:ss", UTC.
void utcStamp(uint32_t epoch, char* out, size_t n);
// 2.4 GHz channel to MHz (2412 for 1 ... 2484 for 14); 0 if unknown.
uint16_t channelMhz(uint8_t ch);

// ---- capture and storage (wardrive.cpp; wardrive_sd.cpp on CYD_GPS) -----------
// On, it takes sightings whenever the GPS has a fresh fix. Off, nothing is
// queued and nothing written. A setting; off by default.
void setEnabled(bool on);
bool enabled();

// From the radio callbacks: cheap, no flash, no allocation. Dropped when the
// queue is full or there is no fresh fix.
void noteWifi(const uint8_t* bssid, const char* ssid, uint16_t auth, uint8_t channel, int8_t rssi);
void noteBle(const uint8_t* mac, const char* name, int8_t rssi, bool haveMfgr, uint16_t mfgr);

uint32_t written();        // this boot
uint32_t skipped();        // this boot: heard again too soon, same place
uint32_t dropped();        // this boot: the queue was full, or (CYD) memory ran low

// The watch's flash store. The CYD GPS builds do not have these.
// From loop(): drains the queue, skips what was written recently from about
// the same place, and appends the rest to flash.
void tick(uint32_t nowMs);

bool     ready();          // the flash region is ours and scanned
uint32_t count();          // records kept
uint32_t capacity();

// Oldest first. fn returns false to stop.
void forEach(bool (*fn)(const Record& r, void* ctx), void* ctx);
// Everything, gone. The duress wipe calls this too.
void clear();

// Mounts the region and finds the newest sector. Call once at boot.
bool begin();

#if defined(CYD_GPS)
// ---- the CYD GPS builds' SD writer (wardrive_sd.cpp) ------------------------
// Reads the switch. Call once at boot.
void sdBegin();
// From loop(): makes the buffers while on with a card in and frees them when
// off, drains the queues, and appends rows to the session's file.
void sdTick(uint32_t nowMs, bool cardMounted);
enum class SdState : uint8_t { OFF, NO_CARD, LOW_MEMORY, WAITING_FOR_FIX, LOGGING };
SdState sdState(uint32_t nowMs);
// "/wigle-YYYYMMDD-HHMM.csv", or "" until the session's first row.
const char* sdFileName();
// Deletes every /wigle-*.csv on the card. The duress wipe calls this.
void sdWipe();
#endif

}  // namespace Wardrive
