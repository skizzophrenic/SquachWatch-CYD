// SquachWatch-CYD -- what the HUMAN SENSING screen draws from. See include/sense_store.h.
#if defined(SENSE_CYD_APP)
#include <Arduino.h>
#include <string.h>
#include "sense_store.h"

namespace SenseStore {

static bool        s_heard = false;
static SenseReport s_last = {};
static uint32_t    s_lastAt = 0, s_motionAt = 0, s_count_total = 0;
static bool        s_motionSeen = false;

// The history and the movements (~1.9 KB) live on the heap between begin()
// and end(), not in fixed RAM: every board paid for them all the time.
struct Hist {
    uint8_t score[HISTORY];
    uint8_t state[HISTORY];
    uint8_t pres[(HISTORY + 7) / 8];
    Event   ev[EVENTS];
};
static Hist*       s_h = nullptr;
static uint16_t    s_head = 0, s_count = 0;

static Router      s_router = {};
static uint32_t    s_routerAt = 0;

static uint8_t     s_evHead = 0, s_evCount = 0;
static bool        s_inMotion = false;

void begin() {
    if (!s_h) s_h = (Hist*)calloc(1, sizeof(Hist));
    s_heard = false; s_motionSeen = false; s_inMotion = false;
    s_head = s_count = 0; s_evHead = s_evCount = 0; s_count_total = 0;
    memset(&s_last, 0, sizeof s_last);
    memset(&s_router, 0, sizeof s_router);
    s_routerAt = 0;
}
void end() { s_heard = false; s_head = s_count = 0; s_evHead = s_evCount = 0; free(s_h); s_h = nullptr; }

void tick(uint32_t now) {
    // Signed: the engine stamps with a millis() read after the loop read `now`.
    if (s_router.live && (int32_t)(now - s_routerAt) > 5000) s_router.live = false;
}

void status(const SenseReport& r, uint32_t now) {
    if (!s_h) return;
    s_last = r; s_lastAt = now; s_heard = true; s_count_total++;
    if (r.state == SENSE_STATE_MOTION) { s_motionAt = now; s_motionSeen = true; }
    s_h->score[s_head] = r.score;
    s_h->state[s_head] = r.state;
    const uint8_t bit = (uint8_t)(1u << (s_head & 7));
    if (r.presence) s_h->pres[s_head >> 3] |= bit; else s_h->pres[s_head >> 3] &= (uint8_t)~bit;
    s_head = (uint16_t)((s_head + 1) % HISTORY);
    if (s_count < HISTORY) s_count++;
    // A MOTION stretch opens an event, and closing it records how long it ran.
    const bool m = r.state == SENSE_STATE_MOTION;
    if (m && !s_inMotion) {
        s_h->ev[s_evHead] = { now, 0, true };
        s_evHead = (uint8_t)((s_evHead + 1) % EVENTS);
        if (s_evCount < EVENTS) s_evCount++;
    }
    if (s_evCount) {
        Event& e = s_h->ev[(s_evHead + EVENTS - 1) % EVENTS];
        if (e.open) { e.ms = now - e.at; if (!m) e.open = false; }
    }
    s_inMotion = m;
}

void router(uint16_t tag, int8_t rssi, uint16_t rate, uint8_t score, uint8_t cal, uint8_t rel, uint32_t now) {
    s_router.live = true; s_router.tag = tag; s_router.rssi = rssi; s_router.rate = rate;
    s_router.score = score; s_router.cal = cal; s_router.rel = rel;
    s_routerAt = now;
}

// Signed, as in tick(): a status can be a few ms "newer" than the caller's now,
// and the unsigned difference wrapped to ~49 days (bench, 2026-11-08).
bool fresh(uint32_t now) { return s_heard && (int32_t)(now - s_lastAt) < 2000; }
bool heard() { return s_heard; }
const SenseReport& last() { return s_last; }
uint32_t lastAt() { return s_lastAt; }
uint32_t sinceMotion(uint32_t now) { return s_motionSeen ? ((int32_t)(now - s_motionAt) < 0 ? 0 : now - s_motionAt) : UINT32_MAX; }

uint16_t count() { return s_count; }
static uint16_t slot(uint16_t i) { return (uint16_t)((s_head + HISTORY - s_count + i) % HISTORY); }
uint8_t scoreAt(uint16_t i) { return s_h ? s_h->score[slot(i)] : 0; }
uint8_t stateAt(uint16_t i) { return s_h ? s_h->state[slot(i)] : 0; }
bool presenceAt(uint16_t i) { if (!s_h) return false; const uint16_t k = slot(i); return (s_h->pres[k >> 3] >> (k & 7)) & 1; }

const Router& router() { return s_router; }

uint8_t events(Event* out, uint8_t cap) {
    uint8_t n = 0;
    if (!s_h) return 0;
    for (uint8_t k = 0; k < s_evCount && n < cap; k++) out[n++] = s_h->ev[(s_evHead + EVENTS - 1 - k) % EVENTS];
    return n;
}

uint32_t statuses() { return s_count_total; }

}  // namespace SenseStore
#endif  // SENSE_CYD_APP
