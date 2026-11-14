// SquachWatch-CYD — per-device alert suppression ("IGNORE") implementation
#include "ignore_list.h"
#include <Preferences.h>
#include <Arduino.h>
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

static const char* KEY_SNZ = "snz";  // persistent ALWAYS snooze MAC records

struct SnoozeRec {
    uint8_t mac[6];
    uint32_t deadline;
    bool always;
};
static SnoozeRec s_snz[SNOOZE_MAX];
static uint8_t s_snzN = 0;

static void removeSnoozeAt(uint8_t at) {
    for (uint8_t j = at + 1; j < s_snzN; j++) s_snz[j - 1] = s_snz[j];
    --s_snzN;
}
static void pruneSnoozes() {
    const uint32_t now = millis();
    for (uint8_t i = 0; i < s_snzN;)
        if (!s_snz[i].always && (int32_t)(now - s_snz[i].deadline) >= 0)
            removeSnoozeAt(i);
        else ++i;
}
static int snoozeIndex(const uint8_t* mac) {
    if (!mac) return -1;
    for (uint8_t i = 0; i < s_snzN; ++i)
        if (memcmp(s_snz[i].mac, mac, 6) == 0) return i;
    return -1;
}
static bool saveSnoozes() {
    uint8_t blob[SNOOZE_MAX * 6];
    size_t len = 0;
    for (uint8_t i = 0; i < s_snzN; ++i) {
        if (!s_snz[i].always) continue;
        memcpy(blob + len, s_snz[i].mac, 6);
        len += 6;
    }
    if (!len) return !s_prefs.isKey(KEY_SNZ) || s_prefs.remove(KEY_SNZ);
    return s_prefs.putBytes(KEY_SNZ, blob, len) == len;
}
static void loadSnoozes() {
    s_snzN = 0;
    const size_t len = s_prefs.getBytesLength(KEY_SNZ);
    if (!len || len > SNOOZE_MAX * 6 || len % 6) return;
    uint8_t blob[SNOOZE_MAX * 6];
    if (s_prefs.getBytes(KEY_SNZ, blob, len) != len) return;
    for (size_t i = 0; i < len; i += 6) {
        if (snoozeIndex(blob + i) >= 0) continue;
        memcpy(s_snz[s_snzN].mac, blob + i, 6);
        s_snz[s_snzN].deadline = 0;
        s_snz[s_snzN].always = true;
        ++s_snzN;
    }
}


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
    loadSnoozes();
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
    // Before v1.25.0 a Bluetooth address was kept backwards (NimBLE's byte
    // order, issue #22), so a device ignored then is on the list reversed.
    // It still matches, and it still comes off with UN-IGNORE. A WiFi address
    // whose reverse is somebody else's ignored address is not a real risk.
    for (uint8_t i = 0; i < s_count; i++) {
        const uint8_t* r = &s_rec[(size_t)i * REC];
        bool same = true;
        for (int b = 0; b < 6 && same; b++) same = (r[b] == mac[5 - b]);
        if (same) return (int)i;
    }
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
    s_prefs.remove(KEY_SNZ);
    s_snzN = 0;
}

// ---- SNOOZE: 10/30 minutes, 1/4 hours, or persistent ALWAYS ----
bool snoozed(const uint8_t* mac) {
    begin();
    pruneSnoozes();
    return snoozeIndex(mac) >= 0;
}
uint8_t snoozeCount() {
    begin();
    pruneSnoozes();
    return s_snzN;
}
bool snoozeInfo(uint8_t idx, uint8_t mac[6], uint32_t& remainingMs, bool& always) {
    begin();
    pruneSnoozes();
    if (idx >= s_snzN || !mac) return false;
    const SnoozeRec& r = s_snz[idx];
    memcpy(mac, r.mac, 6);
    always = r.always;
    remainingMs = always ? 0 : (uint32_t)(r.deadline - millis());
    return true;
}
bool snooze(const uint8_t* mac, SnoozeDuration duration) {
    begin();
    if (!mac) return false;
    pruneSnoozes();
    const int old = snoozeIndex(mac);
    if (old < 0 && s_snzN >= SNOOZE_MAX) return false;
    const bool permanent = duration == SnoozeDuration::ALWAYS;
    const uint32_t durationMs =
        duration == SnoozeDuration::MIN_10 ? 600000UL :
        duration == SnoozeDuration::MIN_30 ? 1800000UL :
        duration == SnoozeDuration::HOUR_1 ? 3600000UL :
        duration == SnoozeDuration::HOUR_4 ? 14400000UL : 0;
    const SnoozeRec previous = old < 0 ? SnoozeRec{} : s_snz[old];
    SnoozeRec& entry = old >= 0 ? s_snz[old] : s_snz[s_snzN++];
    memcpy(entry.mac, mac, 6);
    entry.always = permanent;
    entry.deadline = permanent ? 0 : millis() + durationMs;
    if ((permanent || (old >= 0 && previous.always)) && !saveSnoozes()) {
        if (old >= 0) s_snz[old] = previous;
        else --s_snzN;
        return false;
    }
    return true;
}
void snooze(const uint8_t* mac) {
    (void)snooze(mac, SnoozeDuration::HOUR_1);
}

bool unsnooze(const uint8_t* mac) {
    begin();
    pruneSnoozes();
    const int old = snoozeIndex(mac);
    if (old < 0) return false;
    const SnoozeRec previous = s_snz[old];
    removeSnoozeAt((uint8_t)old);
    if (previous.always && !saveSnoozes()) {
        for (uint8_t i = s_snzN; i > (uint8_t)old; --i) s_snz[i] = s_snz[i - 1];
        s_snz[old] = previous;
        ++s_snzN;
        return false;
    }
    return true;
}

bool silenced(const uint8_t* mac) { return contains(mac) || snoozed(mac); }

}  // namespace IgnoreList
