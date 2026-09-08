// SquachWatch-CYD — per-device alert suppression ("IGNORE") implementation
#include "ignore_list.h"
#include <Preferences.h>
#include <string.h>

namespace IgnoreList {

static const uint8_t REC = 7;      // mac[6] + DetectionType

static Preferences s_prefs;
static bool        s_loaded = false;
static uint8_t     s_rec[MAX * REC];
static uint8_t     s_count = 0;

static const char* NS      = "ignore";
static const char* KEY     = "dev";    // 7-byte records
static const char* KEY_OLD = "macs";   // pre-type format, 6-byte MACs

static void save() {
    if (s_count == 0) {
        // Preferences::putBytes returns early on a zero-length value without
        // touching NVS, so saving an empty list is a silent no-op and the old
        // blob survives. Removing the last device therefore left it on disk
        // and it came back on the next boot. Emptying the list has to delete
        // the key instead.
        s_prefs.remove(KEY);
        return;
    }
    s_prefs.putBytes(KEY, s_rec, (size_t)s_count * REC);
}

void begin() {
    if (s_loaded) return;
    s_prefs.begin(NS, false);
    // getBytesLength on a missing key is 0, which is exactly the empty
    // list -- no separate "has it ever been written" flag needed.
    size_t len = s_prefs.getBytesLength(KEY);
    if (len > sizeof(s_rec)) len = sizeof(s_rec);
    if (len >= REC) {
        s_prefs.getBytes(KEY, s_rec, len);
        s_count = (uint8_t)(len / REC);
        s_loaded = true;
        return;
    }

    // Nothing in the new key: migrate anything left in the old one, giving
    // those entries UNKNOWN. A separate key rather than sniffing the blob
    // length, because 6- and 7-byte records share lengths at 42, 84 and 126 --
    // length alone cannot tell the two formats apart, and guessing wrong would
    // shred somebody's list.
    size_t old = s_prefs.getBytesLength(KEY_OLD);
    s_count = 0;
    if (old >= 6) {
        uint8_t tmp[MAX * 6];
        if (old > sizeof(tmp)) old = sizeof(tmp);
        s_prefs.getBytes(KEY_OLD, tmp, old);
        s_count = (uint8_t)(old / 6u);
        for (uint8_t i = 0; i < s_count; i++) {
            memcpy(&s_rec[(size_t)i * REC], &tmp[(size_t)i * 6u], 6);
            s_rec[(size_t)i * REC + 6] = (uint8_t)DetectionType::UNKNOWN;
        }
        save();
        s_prefs.remove(KEY_OLD);
    }
    s_loaded = true;
}

static int indexOf(const uint8_t* mac) {
    if (!mac) return -1;
    for (uint8_t i = 0; i < s_count; i++)
        if (memcmp(&s_rec[(size_t)i * REC], mac, 6) == 0) return (int)i;
    return -1;
}

bool contains(const uint8_t* mac) {
    begin();
    return indexOf(mac) >= 0;
}

bool add(const uint8_t* mac, DetectionType type) {
    begin();
    if (!mac || s_count >= MAX) return false;
    if (indexOf(mac) >= 0) return false;
    memcpy(&s_rec[(size_t)s_count * REC], mac, 6);
    s_rec[(size_t)s_count * REC + 6] = (uint8_t)type;
    s_count++;
    save();
    return true;
}

bool remove(const uint8_t* mac) {
    begin();
    const int idx = indexOf(mac);
    if (idx < 0) return false;
    // Order carries no meaning here, so the last entry backfills the hole
    // rather than shifting the tail down.
    const uint8_t last = (uint8_t)(s_count - 1);
    if ((uint8_t)idx != last)
        memcpy(&s_rec[(size_t)idx * REC], &s_rec[(size_t)last * REC], REC);
    s_count--;
    save();
    return true;
}

uint8_t count() { begin(); return s_count; }

const uint8_t* macAt(uint8_t idx) {
    begin();
    if (idx >= s_count) return nullptr;
    return &s_rec[(size_t)idx * REC];
}

DetectionType typeAt(uint8_t idx) {
    begin();
    if (idx >= s_count) return DetectionType::UNKNOWN;
    const uint8_t v = s_rec[(size_t)idx * REC + 6];
    return (v < (uint8_t)DetectionType::COUNT) ? (DetectionType)v
                                               : DetectionType::UNKNOWN;
}

void clear() {
    begin();
    s_count = 0;
    s_prefs.remove(KEY);
}

}  // namespace IgnoreList
