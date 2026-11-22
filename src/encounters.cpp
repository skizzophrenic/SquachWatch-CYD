// SquachWatch-CYD -- random encounters: the dice and the FIELD JOURNAL.
// See encounters.h. The playing is in ui_clear.cpp.
#include "encounters.h"
#include "state.h"
#include "clock.h"
#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

namespace Encounters {

// ---- the dice -----------------------------------------------------------------
uint16_t chancePermille(uint8_t level, uint16_t misses) {
    if (level == 0) return 0;
    // RARE halves it, OFTEN doubles it; the sure thing comes after 24 misses
    // at NORMAL, 48 at RARE, 12 at OFTEN.
    const uint16_t base = level == 1 ? 25 : level == 3 ? 100 : 50;
    const uint16_t step = level == 1 ? 10 : level == 3 ? 40 : 20;
    const uint16_t sure = level == 1 ? 48 : level == 3 ? 12 : 24;
    if (misses >= sure) return 1000;
    const uint32_t p = (uint32_t)base + (uint32_t)step * misses;
    return (uint16_t)(p > 1000 ? 1000 : p);
}

Tier tierFor(uint8_t d100, bool woods) {
    if (d100 <= 60) return Tier::COMMON;
    if (d100 <= 85) return Tier::UNCOMMON;
    if (d100 < (woods ? 96 : 97)) return Tier::RARE;
    return Tier::LEGENDARY;
}

Tier tierOf(Kind k) {
    switch (k) {
        case Kind::DRIFTER: case Kind::GOBLIN: case Kind::ICONS: case Kind::ZOOMIES: return Tier::COMMON;
        case Kind::KAIJU: case Kind::LOST:                                          return Tier::UNCOMMON;
        case Kind::TWIN: case Kind::ABDUCT: case Kind::MIMIC:                       return Tier::RARE;
        default:                                                                    return Tier::LEGENDARY;
    }
}

Kind pick(Tier t, const Context& c, uint32_t r) {
    const bool caught = c.lastCatch != 0;
    const bool twinCatch = c.lastCatch == (uint8_t)DetectionType::EVILTWIN || c.lastCatch == (uint8_t)DetectionType::DEAUTH;
    struct W { Kind k; uint8_t w; };
    W ws[4]; uint8_t n = 0;
    switch (t) {
        case Tier::COMMON:
            ws[n++] = { Kind::DRIFTER, (uint8_t)(c.visits ? 3 : 0) };
            ws[n++] = { Kind::GOBLIN,  (uint8_t)(c.visits ? (c.woods ? 6 : 3) : 0) };
            ws[n++] = { Kind::ICONS,   (uint8_t)(caught ? 6 : 0) };
            ws[n++] = { Kind::ZOOMIES, (uint8_t)(c.pet ? 2 : 0) };
            break;
        case Tier::UNCOMMON:
            ws[n++] = { Kind::KAIJU, 3 };
            ws[n++] = { Kind::LOST,  (uint8_t)(c.city ? 4 : 2) };
            break;
        case Tier::RARE:
            ws[n++] = { Kind::TWIN,   (uint8_t)(c.visits ? (twinCatch ? 4 : 1) : 0) };
            ws[n++] = { Kind::ABDUCT, (uint8_t)(c.night ? 2 : 1) };
            break;
        default:
            ws[n++] = { Kind::ELDER, 1 };
            break;
    }
    uint16_t total = 0;
    for (uint8_t i = 0; i < n; i++) total += ws[i].w;
    if (!total) return Kind::NONE;
    uint16_t x = (uint16_t)(r % total);
    for (uint8_t i = 0; i < n; i++) {
        if (x < ws[i].w) return ws[i].k;
        x -= ws[i].w;
    }
    return Kind::NONE;
}

Face faceOf(uint8_t d20) {
    Face f{ false, false, Tier::COMMON };
    if (d20 <= 5)       f.miss  = true;
    else if (d20 <= 12) f.tier  = Tier::COMMON;
    else if (d20 <= 16) f.tier  = Tier::UNCOMMON;
    else if (d20 <= 18) f.tier  = Tier::RARE;
    else if (d20 == 19) f.tier  = Tier::LEGENDARY;
    else                f.shiny = true;
    return f;
}

const char* name(Kind k) {
    switch (k) {
        case Kind::DRIFTER: return "THE DRIFTER";
        case Kind::GOBLIN:  return "WANDERING GOBLIN";
        case Kind::ICONS:   return "LIVING ICONS";
        case Kind::ZOOMIES: return "PET ZOOMIES";
        case Kind::KAIJU:   return "KAIJU MOTHMAN";
        case Kind::LOST:    return "LOST SIGNAL";
        case Kind::TWIN:    return "EVIL TWIN";
        case Kind::ABDUCT:  return "ABDUCTION";
        case Kind::ELDER:   return "THE ELDER";
        case Kind::MIMIC:   return "THE MIMIC";
        default:            return "?";
    }
}
const char* tierName(Tier t) {
    switch (t) {
        case Tier::COMMON:   return "COMMON";
        case Tier::UNCOMMON: return "UNCOMMON";
        case Tier::RARE:     return "RARE";
        default:             return "LEGENDARY";
    }
}

#if !defined(ENCOUNTERS_DICE_ONLY)
// ---- the run ------------------------------------------------------------------
namespace {
const uint32_t ROLL_EVERY_MS = 10UL * 60UL * 1000UL;

struct Entry { uint16_t met; uint16_t shiny; uint32_t first; };
static_assert(sizeof(Entry) == 8, "a journal entry is eight bytes in NVS");

Preferences s_prefs;
bool     s_began = false;
uint8_t  s_level = 2;
Entry    s_book[(uint8_t)Kind::COUNT];
uint16_t s_photos = 0;
uint16_t s_misses = 0;
uint32_t s_eligibleMs = 0, s_lastTick = 0;
Kind     s_active = Kind::NONE;
bool     s_shiny = false;
bool     s_missShow = false;
uint8_t  s_missNum = 3;
uint32_t s_since = 0;
// The free roll: whether one waits, and the running time towards the next.
uint8_t  s_free = 0;
uint32_t s_runMin = 0, s_runSavedMin = 0, s_runMs = 0, s_runLast = 0;

void save() {
    s_prefs.putBytes("book", s_book, sizeof s_book);
    s_prefs.putUInt("miss", s_misses);
}

void record(Kind k, bool shiny) {
    Entry& e = s_book[(uint8_t)k];
    if (e.met < 0xFFFF) e.met++;
    if (shiny && e.shiny < 0xFFFF) e.shiny++;
    if (!e.first && Clock::isSet()) e.first = Clock::nowEpoch();
    save();
}
}  // namespace

void begin() {
    if (s_began) return;
    s_began = true;
    memset(s_book, 0, sizeof s_book);
    if (!s_prefs.begin("enc", false)) return;
    s_level  = s_prefs.getUChar("lvl", 2);
    if (s_level > 3) s_level = 2;
    s_misses = (uint16_t)s_prefs.getUInt("miss", 0);
    s_photos = (uint16_t)s_prefs.getUInt("photo", 0);
    // No key yet is a board that has just been updated: its first one waits.
    s_free   = s_prefs.getUChar("free", 1) ? 1 : 0;
    s_runMin = s_runSavedMin = s_prefs.getUInt("run", 0);
    // A short read means kinds were added since it was written.
    size_t have = s_prefs.getBytesLength("book");
    if (have > sizeof s_book) have = sizeof s_book;
    if (have >= sizeof(Entry)) s_prefs.getBytes("book", s_book, have - have % sizeof(Entry));
}

void runTick(uint32_t nowMs) {
    begin();
    const uint32_t dt = s_runLast ? nowMs - s_runLast : 0;
    s_runLast = nowMs ? nowMs : 1;
    if (s_free) return;                     // one waits at a time: nothing banks behind it
    s_runMs += dt;
    if (s_runMs < 60000UL) return;
    s_runMin += s_runMs / 60000UL;
    s_runMs  %= 60000UL;
    if (s_runMin >= FREE_EVERY_MIN) {
        setFree(true);
        Serial.println("[encounter] a free roll is waiting");
    } else if (s_runMin - s_runSavedMin >= 15) {
        // Kept every quarter of an hour: that much is the most a pulled plug costs.
        s_runSavedMin = s_runMin;
        s_prefs.putUInt("run", s_runMin);
    }
}

void setFree(bool waiting) {
    begin();
    s_free = waiting ? 1 : 0;
    s_runMin = s_runSavedMin = 0; s_runMs = 0;
    s_prefs.putUChar("free", s_free);
    s_prefs.putUInt("run", 0);
}

bool     freeReady() { begin(); return s_free && s_level != 0; }
uint16_t freeInMin() {
    begin();
    if (s_free) return 0;
    return (uint16_t)(s_runMin >= FREE_EVERY_MIN ? 1 : FREE_EVERY_MIN - s_runMin);
}

uint8_t rollFree(const Context& c) {
    begin();
    if (!s_free || s_level == 0 || s_active != Kind::NONE) return 0;
    setFree(false);
    const uint8_t d = (uint8_t)random(1, 21);
    const Face f = faceOf(d);
    Serial.printf("[encounter] free roll: %u\n", (unsigned)d);
    if (f.miss) { forceMiss(d); return d; }
    // A 20's tier is rolled as any hit's is. A tier with nothing in it here
    // steps down to one that has.
    int tr = (int)(f.shiny ? tierFor((uint8_t)random(1, 101), c.woods) : f.tier);
    Kind k = Kind::NONE;
    for (; tr >= 0 && k == Kind::NONE; tr--) k = pick((Tier)tr, c, (uint32_t)random(0, 0x7FFFFFFF));
    if (k == Kind::NONE) k = c.lastCatch ? Kind::ICONS : Kind::LOST;
    force(k, f.shiny);
    return d;
}

bool takeMimic() {
    begin();
    if (!s_free || s_level == 0 || s_active != Kind::NONE) return false;
    if (random(0, MIMIC_ONE_IN) != 0) return false;
    setFree(false);
    force(Kind::MIMIC);
    return true;
}

void tick(uint32_t nowMs, bool eligible, const Context& c) {
    begin();
    runTick(nowMs);
    const uint32_t dt = s_lastTick ? nowMs - s_lastTick : 0;
    s_lastTick = nowMs;
    if (s_active != Kind::NONE || !eligible || s_level == 0) return;
    s_eligibleMs += dt > 2000 ? 2000 : dt;   // a stalled loop is not screen time
    if (s_eligibleMs < ROLL_EVERY_MS) return;
    s_eligibleMs = 0;
    const uint16_t p = chancePermille(s_level, s_misses);
    if ((uint16_t)random(0, 1000) >= p) {
        s_misses++;
        s_prefs.putUInt("miss", s_misses);
        // One miss in four is seen: the die drops for nothing.
        s_missShow = random(0, 4) == 0;
        s_missNum  = (uint8_t)random(1, 6);
        Serial.printf("[encounter] roll missed (%u in 1000), %u misses%s\n", (unsigned)p, (unsigned)s_misses,
                      s_missShow ? ", shown" : "");
        return;
    }
    Kind k = Kind::NONE;
    for (int tries = 0; tries < 6 && k == Kind::NONE; tries++)
        k = pick(tierFor((uint8_t)random(1, 101), c.woods), c, (uint32_t)random(0, 0x7FFFFFFF));
    if (k == Kind::NONE) k = c.lastCatch ? Kind::ICONS : Kind::LOST;
    force(k, random(0, 64) == 0);
}

void force(Kind k, bool shiny, bool count) {
    begin();
    if (k >= Kind::COUNT) return;
    s_active = k; s_shiny = shiny; s_since = millis();
    if (count) {
        s_misses = 0;
        record(k, shiny);
    }
    Serial.printf("[encounter] %s%s (%s)\n", name(k), shiny ? ", SHINY" : "", tierName(tierOf(k)));
}

bool     takeMiss()    { const bool m = s_missShow; s_missShow = false; return m; }
void     forceMiss(uint8_t face) {
    s_missShow = true;
    s_missNum  = (face >= 1 && face <= 5) ? face : (uint8_t)random(1, 6);
    Serial.printf("[encounter] a miss, shown: a %u\n", (unsigned)s_missNum);
}
uint8_t  missRolled()  { return s_missNum; }
Kind     active()      { return s_active; }
bool     activeShiny() { return s_shiny; }
uint32_t activeSince() { return s_since; }
void     finish()      { s_active = Kind::NONE; s_shiny = false; s_eligibleMs = 0; }
uint16_t misses()      { begin(); return s_misses; }

uint8_t level() { begin(); return s_level; }
void cycleLevel() { begin(); s_level = (uint8_t)((s_level + 1) % 4); s_prefs.putUChar("lvl", s_level); }
const char* levelName() {
    static const char* const N[4] = { "OFF", "RARE", "NORMAL", "OFTEN" };
    return N[level() & 3];
}

uint16_t met(Kind k)        { begin(); return k < Kind::COUNT ? s_book[(uint8_t)k].met : 0; }
uint32_t firstEpoch(Kind k) { begin(); return k < Kind::COUNT ? s_book[(uint8_t)k].first : 0; }
uint16_t shinies(Kind k)    { begin(); return k < Kind::COUNT ? s_book[(uint8_t)k].shiny : 0; }
uint8_t  kindsMet() {
    begin();
    uint8_t n = 0;
    for (uint8_t i = 0; i < (uint8_t)Kind::COUNT; i++) if (s_book[i].met) n++;
    return n;
}
uint16_t photos()   { begin(); return s_photos; }
void     notePhoto() { begin(); if (s_photos < 0xFFFF) s_photos++; s_prefs.putUInt("photo", s_photos); }

void wipe() {
    begin();
    memset(s_book, 0, sizeof s_book);
    s_photos = 0; s_misses = 0;
    s_prefs.clear();
    // The free roll is not the journal's: it is as it was.
    s_prefs.putUChar("free", s_free);
    s_prefs.putUInt("run", s_runMin);
    s_runSavedMin = s_runMin;
}
#endif

}  // namespace Encounters
