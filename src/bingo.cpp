// SquachWatch-CYD — detection bingo. See bingo.h.
#include "bingo.h"
#include "detection.h"
#include "clock.h"
#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

namespace Bingo {
namespace {

Preferences s_prefs;
bool s_open = false;

uint8_t  s_card[CELLS];        // DetectionType per square
uint8_t  s_day[CELLS];         // 0 unmarked, else the weekday it marked, 1..7
uint16_t s_lines   = 0;        // which of the ten lines have been called
uint32_t s_week    = 0;
uint16_t s_filled  = 0;        // cards finished, ever
uint16_t s_linesEv = 0;
uint8_t  s_streak  = 0;
uint8_t  s_best    = 0;        // best streak
uint8_t  s_bestFil = 0;        // most squares on one card

// Sightings the radio task has handed over, one bit a type. Never written to
// flash from there -- tick() does that from loop().
volatile uint32_t s_pending = 0;
bool     s_dirty   = false;
uint32_t s_saveAt  = 0;

Event         s_event     = Event::NONE;
DetectionType s_eventType = DetectionType::UNKNOWN;

// The ten lines, as the squares they are made of.
const uint8_t LINE_CELLS[LINES][4] = {
    { 0,  1,  2,  3}, { 4,  5,  6,  7}, { 8,  9, 10, 11}, {12, 13, 14, 15},
    { 0,  4,  8, 12}, { 1,  5,  9, 13}, { 2,  6, 10, 14}, { 3,  7, 11, 15},
    { 0,  5, 10, 15}, { 3,  6,  9, 12},
};

void open() {
    if (!s_open) s_open = s_prefs.begin("bingo", false);
}

void save() {
    open();
    if (!s_open) return;
    s_prefs.putBytes("card", s_card, sizeof s_card);
    s_prefs.putBytes("days", s_day, sizeof s_day);
    s_prefs.putUInt("lines", s_lines);
    s_prefs.putUInt("week", s_week);
    s_prefs.putUInt("filled", s_filled);
    s_prefs.putUInt("linesev", s_linesEv);
    s_prefs.putUChar("streak", s_streak);
    s_prefs.putUChar("best", s_best);
    s_prefs.putUChar("bestfil", s_bestFil);
}

uint32_t weekOfDay(uint32_t day) { return day / 7u; }

// A card: sixteen of the nineteen types, so three sit out. The ones that sit
// out are picked from the types this board has never seen, which is
// what keeps a card winnable somewhere quiet -- a card that keeps every rare
// type is a card nobody finishes.
void deal(const DetectionEngine* eng) {
    uint8_t pool[(uint8_t)DetectionType::COUNT];
    uint8_t n = 0;
    for (uint8_t i = 0; i < CATALOGUE_N; i++) pool[n++] = (uint8_t)catalogueAt(i);   // MY ALERTS is nobody else's card

    if (n > CELLS) {
        // Drop (n - CELLS) of them, preferring ones never seen here.
        for (uint8_t drop = (uint8_t)(n - CELLS); drop > 0; drop--) {
            uint8_t unseen[(uint8_t)DetectionType::COUNT];
            uint8_t un = 0;
            if (eng) {
                for (uint8_t i = 0; i < n; i++)
                    if (eng->lifetimeTypeCount((DetectionType)pool[i]) == 0) unseen[un++] = i;
            }
            const uint8_t victim = un ? unseen[random(0, un)] : (uint8_t)random(0, n);
            for (uint8_t i = victim; i + 1 < n; i++) pool[i] = pool[i + 1];
            n--;
        }
    }
    // Shuffle what is left, so the same types do not sit in the same corner
    // week after week.
    for (uint8_t i = n; i > 1; i--) {
        const uint8_t j = (uint8_t)random(0, i);
        const uint8_t tmp = pool[i - 1]; pool[i - 1] = pool[j]; pool[j] = tmp;
    }
    for (uint8_t i = 0; i < CELLS; i++) s_card[i] = i < n ? pool[i] : 0;
    memset(s_day, 0, sizeof s_day);
    s_lines = 0;
}

// Marks that came in from the radio task, and the lines they finish.
void applyPending() {
    const uint32_t bits = s_pending;
    if (!bits) return;
    s_pending = 0;
    const uint8_t day = Clock::trusted() ? (uint8_t)(Clock::weekday() + 1) : 1;
    for (uint8_t i = 0; i < CELLS; i++) {
        if (s_day[i] || !s_card[i]) continue;
        if (!(bits & (1u << s_card[i]))) continue;
        s_day[i]    = day;
        s_dirty     = true;
        s_event     = Event::MARKED;
        s_eventType = (DetectionType)s_card[i];
    }
    if (!s_dirty) return;

    for (uint8_t l = 0; l < LINES; l++) {
        if (s_lines & (1u << l)) continue;
        bool all = true;
        for (uint8_t k = 0; k < 4; k++) if (!s_day[LINE_CELLS[l][k]]) { all = false; break; }
        if (!all) continue;
        s_lines |= (uint16_t)(1u << l);
        if (s_linesEv < 0xFFFF) s_linesEv++;
        s_event = Event::LINE;
    }
    const uint8_t got = markedCount();
    if (got > s_bestFil) s_bestFil = got;
    if (got == CELLS && s_event != Event::NONE) s_event = Event::FULL;
}

}  // namespace

void begin(const DetectionEngine& eng) {
    open();
    randomSeed((uint32_t)(millis() ^ (micros() << 3)));
    size_t have = s_open ? s_prefs.getBytesLength("card") : 0;
    if (have == sizeof s_card) {
        s_prefs.getBytes("card", s_card, sizeof s_card);
        s_prefs.getBytes("days", s_day, sizeof s_day);
        s_lines   = (uint16_t)s_prefs.getUInt("lines", 0);
        s_week    = s_prefs.getUInt("week", 0);
        s_filled  = (uint16_t)s_prefs.getUInt("filled", 0);
        s_linesEv = (uint16_t)s_prefs.getUInt("linesev", 0);
        s_streak  = s_prefs.getUChar("streak", 0);
        s_best    = s_prefs.getUChar("best", 0);
        s_bestFil = s_prefs.getUChar("bestfil", 0);
    } else {
        deal(&eng);
        s_week = Clock::trusted() ? weekOfDay(Clock::localDay()) : 0;
        save();
    }
    // A board that was off over the weekend comes back to the right week.
    tick(millis());
}

void tick(uint32_t now) {
    applyPending();

    // The week turns over. Without a clock there are no weeks, so the card
    // stands until somebody asks for a new one.
    if (Clock::trusted()) {
        const uint32_t wk = weekOfDay(Clock::localDay());
        if (!s_week) {                       // the card predates the clock
            s_week  = wk;
            s_dirty = true;
        } else if (wk != s_week) {
            const bool full = markedCount() == CELLS;
            if (full) {
                if (s_filled < 0xFFFF) s_filled++;
                if (s_streak < 255) s_streak++;
                if (s_streak > s_best) s_best = s_streak;
            } else {
                s_streak = 0;
            }
            deal(nullptr);
            s_week  = wk;
            s_event = Event::NEW_CARD;
            s_dirty = true;
        }
    }

    // Written a second after the last change, not on the spot: a walk past a
    // row of cameras marks several squares in a few seconds, and that is one
    // write, not four.
    if (s_dirty && !s_saveAt) s_saveAt = now + 1000;
    if (s_saveAt && now >= s_saveAt) {
        s_saveAt = 0;
        s_dirty  = false;
        save();
    }
}

void note(DetectionType t) {
    const uint8_t i = (uint8_t)t;
    if (i && i < 32) s_pending |= (1u << i);
}

DetectionType typeAt(uint8_t i) {
    return i < CELLS ? (DetectionType)s_card[i] : DetectionType::UNKNOWN;
}
bool    marked(uint8_t i)  { return i < CELLS && s_day[i] != 0; }
uint8_t markDay(uint8_t i) { return i < CELLS ? s_day[i] : 0; }

uint8_t markedCount() {
    uint8_t n = 0;
    for (uint8_t i = 0; i < CELLS; i++) if (s_day[i]) n++;
    return n;
}

bool inCalledLine(uint8_t i) {
    for (uint8_t l = 0; l < LINES; l++) {
        if (!(s_lines & (1u << l))) continue;
        for (uint8_t k = 0; k < 4; k++) if (LINE_CELLS[l][k] == i) return true;
    }
    return false;
}

uint8_t linesCalled() {
    uint8_t n = 0;
    for (uint8_t l = 0; l < LINES; l++) if (s_lines & (1u << l)) n++;
    return n;
}

uint32_t weekNumber() { return s_week; }
uint16_t cardsFilled(){ return s_filled; }
uint16_t linesEver()  { return s_linesEv; }
uint8_t  streak()     { return s_streak; }
uint8_t  bestStreak() { return s_best; }
uint8_t  bestFilled() { return s_bestFil; }

void newCard() {
    const bool full = markedCount() == CELLS;
    if (full) {
        if (s_filled < 0xFFFF) s_filled++;
        if (s_streak < 255) s_streak++;
        if (s_streak > s_best) s_best = s_streak;
    } else {
        s_streak = 0;              // asking for a fresh card mid-week costs the streak
    }
    deal(nullptr);
    if (Clock::trusted()) s_week = weekOfDay(Clock::localDay());
    s_dirty = true;
    save();
    s_saveAt = 0;
    s_dirty  = false;
}

Event takeEvent(DetectionType& type) {
    const Event e = s_event;
    type    = s_eventType;
    s_event = Event::NONE;
    return e;
}

}  // namespace Bingo
