// SquachWatch-CYD — per-device alert suppression ("IGNORE") implementation
#include "ignore_list.h"
#include <Preferences.h>
#include <string.h>

namespace IgnoreList {

static Preferences s_prefs;
static bool        s_loaded = false;
static uint8_t     s_macs[MAX * 6];
static uint8_t     s_count = 0;

static const char* NS  = "ignore";
static const char* KEY = "macs";

static void save() {
    s_prefs.putBytes(KEY, s_macs, (size_t)s_count * 6u);
}

void begin() {
    if (s_loaded) return;
    s_prefs.begin(NS, false);
    // getBytesLength on a missing key is 0, which is exactly the empty
    // list -- no separate "has it ever been written" flag needed.
    size_t len = s_prefs.getBytesLength(KEY);
    if (len > sizeof(s_macs)) len = sizeof(s_macs);
    if (len >= 6) {
        s_prefs.getBytes(KEY, s_macs, len);
        s_count = (uint8_t)(len / 6u);
    } else {
        s_count = 0;
    }
    s_loaded = true;
}

static int indexOf(const uint8_t* mac) {
    if (!mac) return -1;
    for (uint8_t i = 0; i < s_count; i++)
        if (memcmp(&s_macs[(size_t)i * 6u], mac, 6) == 0) return (int)i;
    return -1;
}

bool contains(const uint8_t* mac) {
    begin();
    return indexOf(mac) >= 0;
}

bool add(const uint8_t* mac) {
    begin();
    if (!mac || s_count >= MAX) return false;
    if (indexOf(mac) >= 0) return false;
    memcpy(&s_macs[(size_t)s_count * 6u], mac, 6);
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
        memcpy(&s_macs[(size_t)idx * 6u], &s_macs[(size_t)last * 6u], 6);
    s_count--;
    save();
    return true;
}

uint8_t count() { begin(); return s_count; }

const uint8_t* macAt(uint8_t idx) {
    begin();
    if (idx >= s_count) return nullptr;
    return &s_macs[(size_t)idx * 6u];
}

void clear() {
    begin();
    s_count = 0;
    s_prefs.remove(KEY);
}

}  // namespace IgnoreList
