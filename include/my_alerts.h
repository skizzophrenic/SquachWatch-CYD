// SquachWatch-CYD — MY ALERTS: WiFi names the owner asked to be told about.
//
// Asked for by a tester whose local police run in-car routers named "veh"
// plus the unit number, and whose county does the same: no signature table
// can know a naming habit that local, so the board lets its owner type one.
// A beacon whose network name matches raises an ordinary alert of type
// MINE, with the owner's label on the plate and the network's name under it.
//
// How a pattern reads:
//   plain text      a name that STARTS with it ("veh9" catches "veh912"),
//                   the same rule the built-in SSID table uses
//   *               any run of characters, none included ("*-pd-*")
//   ?               exactly one character ("veh9??")
// A pattern with a wildcard in it must match the WHOLE name, so "veh*" and
// "veh" mean the same thing and "*veh" means "ends in veh". Upper and lower
// case are the same.
//
// Eight slots, each a pattern and an optional label. The slots never move: a
// row in the LOG points at its slot's label (Detection::vendor is a pointer,
// never a copy), so removing one entry must not hand its rows the next
// entry's name. A removed slot just goes empty.
//
// Stored in NVS namespace "myalerts", which a duress wipe clears: a list of
// what someone is watching for says a good deal about them.
//
// Read from loop() (processWiFiQ) and edited from loop() (the MY ALERTS
// screen), so it needs no lock.
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace MyAlerts {

static const uint8_t MAX       = 8;
static const uint8_t PAT_MAX   = 32;   // an SSID is at most 32 bytes
static const uint8_t LABEL_MAX = 15;   // the alert plate's vendor line holds 15

// ---- the matcher, pure, for the host test --------------------------------

inline char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }

inline bool hasWildcard(const char* p) {
    for (; p && *p; p++) if (*p == '*' || *p == '?') return true;
    return false;
}

// Characters a pattern pins down, wildcards not counted. "veh9*" has four.
inline uint8_t literals(const char* p) {
    uint8_t n = 0;
    for (; p && *p; p++) if (*p != '*' && *p != '?') n++;
    return n;
}

// The whole of `text` against `pat`, with * and ?. Iterative, remembering
// only the last star: that is enough for a glob (a later star always
// supersedes an earlier one), and it cannot recurse or blow up on
// "*a*a*a*a*b" the way the textbook recursive form can.
inline bool globMatch(const char* pat, const char* text) {
    if (!pat || !text) return false;
    const char* star = nullptr;   // the last * seen in the pattern
    const char* mark = nullptr;   // where in the text that star is up to
    while (*text) {
        if (*pat == '*') { star = pat++; mark = text; continue; }
        if (*pat && (*pat == '?' || lower(*pat) == lower(*text))) { pat++; text++; continue; }
        if (star) { pat = star + 1; text = ++mark; continue; }
        return false;
    }
    while (*pat == '*') pat++;
    return *pat == '\0';
}

// The rule above in full: plain text is a prefix, a wildcard makes it whole.
inline bool nameMatches(const char* pat, const char* ssid) {
    if (!pat || !pat[0] || !ssid || !ssid[0]) return false;
    if (hasWildcard(pat)) return globMatch(pat, ssid);
    for (; *pat; pat++, ssid++) {
        if (!*ssid || lower(*pat) != lower(*ssid)) return false;
    }
    return true;
}

// What the screen says about a pattern before it is saved.
//   OK        fine
//   WIDE      saved, with a warning: under three fixed characters will catch
//             other people's networks too ("veh" is three, so it is fine)
//   TOO_WIDE  refused: under two fixed characters ("*", "a*") is every
//             network in the street
enum class Check : uint8_t { OK, WIDE, TOO_WIDE };
inline Check check(const char* pat) {
    const uint8_t n = literals(pat);
    if (n < 2) return Check::TOO_WIDE;
    if (n < 3) return Check::WIDE;
    return Check::OK;
}

// ---- the list --------------------------------------------------------------

void begin();                         // loads from NVS; safe to call again

uint8_t     count();                  // slots in use
bool        used(uint8_t slot);
const char* pattern(uint8_t slot);    // "" for an empty or out-of-range slot
const char* label(uint8_t slot);      // "" when none was given
// What the alert plate and the LOG show for a slot: its label, or its
// pattern when it has none. Points into the slot, so it stays valid (and
// reads "" once the slot is removed) for as long as the board is up.
const char* tag(uint8_t slot);
// The n-th slot in use, in slot order, or -1. The screen lists by this.
int8_t      nth(uint8_t n);

// The first empty slot it went into, or -1 when all eight are taken or the
// pattern is refused by check(). Saved at once.
int8_t add(const char* pat, const char* label);
bool   setLabel(uint8_t slot, const char* label);
void   remove(uint8_t slot);
void   clear();

// The slot whose pattern matches this network name, or -1. Cheap enough for
// every beacon: eight short compares, and none at all while the list is
// empty.
int8_t match(const char* ssid);

}  // namespace MyAlerts
