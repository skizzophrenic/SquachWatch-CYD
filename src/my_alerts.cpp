// SquachWatch-CYD — MY ALERTS storage. See include/my_alerts.h.
#include "my_alerts.h"
#include <Preferences.h>
#include <string.h>
#include <stdio.h>

namespace MyAlerts {

namespace {

// One NVS blob of fixed records, the ignore list's way: eight records of 49
// bytes is 392, one key, one read at boot.
struct Rec {
    char pat[PAT_MAX + 1];
    char label[LABEL_MAX + 1];
};

Rec         s_rec[MAX];
bool        s_began = false;
uint8_t     s_count = 0;
Preferences s_prefs;

const char* NS  = "myalerts";
const char* KEY = "list";

void recount() {
    s_count = 0;
    for (uint8_t i = 0; i < MAX; i++) if (s_rec[i].pat[0]) s_count++;
}

void save() {
    if (!s_began) return;
    if (!s_count) { s_prefs.remove(KEY); return; }   // putBytes refuses a zero-length value anyway
    s_prefs.putBytes(KEY, s_rec, sizeof s_rec);
}

void copyText(char* dst, size_t cap, const char* src) {
    snprintf(dst, cap, "%s", src ? src : "");
}

}  // namespace

void begin() {
    if (s_began) return;
    memset(s_rec, 0, sizeof s_rec);
    s_prefs.begin(NS, false);
    s_began = true;
    const size_t have = s_prefs.getBytesLength(KEY);
    if (have == sizeof s_rec) s_prefs.getBytes(KEY, s_rec, have);
    // Whatever came off the flash is terminated before anything prints it.
    for (uint8_t i = 0; i < MAX; i++) {
        s_rec[i].pat[PAT_MAX] = '\0';
        s_rec[i].label[LABEL_MAX] = '\0';
    }
    recount();
}

uint8_t count() { return s_count; }

bool used(uint8_t slot) { return slot < MAX && s_rec[slot].pat[0]; }

const char* pattern(uint8_t slot) { return slot < MAX ? s_rec[slot].pat : ""; }
const char* label(uint8_t slot)   { return slot < MAX ? s_rec[slot].label : ""; }

const char* tag(uint8_t slot) {
    if (slot >= MAX) return "";
    return s_rec[slot].label[0] ? s_rec[slot].label : s_rec[slot].pat;
}

int8_t nth(uint8_t n) {
    for (uint8_t i = 0; i < MAX; i++) {
        if (!s_rec[i].pat[0]) continue;
        if (n-- == 0) return (int8_t)i;
    }
    return -1;
}

int8_t add(const char* pat, const char* lab) {
    if (!pat || !pat[0] || check(pat) == Check::TOO_WIDE) return -1;
    for (uint8_t i = 0; i < MAX; i++) {
        if (s_rec[i].pat[0]) continue;
        copyText(s_rec[i].pat, sizeof s_rec[i].pat, pat);
        copyText(s_rec[i].label, sizeof s_rec[i].label, lab);
        recount();
        save();
        return (int8_t)i;
    }
    return -1;
}

bool setLabel(uint8_t slot, const char* lab) {
    if (!used(slot)) return false;
    copyText(s_rec[slot].label, sizeof s_rec[slot].label, lab);
    save();
    return true;
}

void remove(uint8_t slot) {
    if (!used(slot)) return;
    memset(&s_rec[slot], 0, sizeof s_rec[slot]);
    recount();
    save();
}

void clear() {
    memset(s_rec, 0, sizeof s_rec);
    s_count = 0;
    save();
}

int8_t match(const char* ssid) {
    if (!s_count || !ssid || !ssid[0]) return -1;
    for (uint8_t i = 0; i < MAX; i++)
        if (s_rec[i].pat[0] && nameMatches(s_rec[i].pat, ssid)) return (int8_t)i;
    return -1;
}

}  // namespace MyAlerts
