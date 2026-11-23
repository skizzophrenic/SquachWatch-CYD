// SquachWatch-CYD — places. See places.h.
#include "places.h"
#include <string.h>
#include <math.h>

namespace Places {

namespace {

struct Heard { uint8_t bssid[6]; int8_t rssi; uint32_t lastMs; };
// Networks heard lately: the strongest ANCHOR_N of these are the fingerprint.
static const uint8_t HEARD_CAP = 16;
Heard    s_heard[HEARD_CAP];
uint8_t  s_heardN = 0;

struct Place {
    uint8_t  id;
    uint32_t startMs;
    uint8_t  anchor[ANCHOR_N][6];
    uint8_t  anchorN;
    bool     home;
    bool     hasPos;
    int32_t  lat7, lon7;
};
Place    s_places[CAP];
uint8_t  s_placesN = 0;     // filled slots, <= CAP
uint8_t  s_newest  = 0;     // index of the current place
uint8_t  s_nextId  = 1;
uint32_t s_lastCheck = 0;
uint8_t  s_awayChecks = 0;  // checks in a row that found the fingerprint gone
bool     s_homeHeard = false;
bool   (*s_homeFn)(const char*) = nullptr;

Place* cur() { return s_placesN ? &s_places[s_newest] : nullptr; }

Place* find(uint8_t id) {
    for (uint8_t i = 0; i < s_placesN; i++) if (s_places[i].id == id) return &s_places[i];
    return nullptr;
}

// The strongest networks heard in the last HEARD_MS, into out; returns how many.
uint8_t strongest(uint32_t nowMs, uint8_t out[ANCHOR_N][6]) {
    int8_t  best[ANCHOR_N];
    uint8_t n = 0;
    for (uint8_t i = 0; i < s_heardN; i++) {
        if ((uint32_t)(nowMs - s_heard[i].lastMs) > HEARD_MS) continue;
        // Insert by RSSI, strongest first.
        uint8_t k = n < ANCHOR_N ? n : ANCHOR_N;
        if (n == ANCHOR_N && s_heard[i].rssi <= best[ANCHOR_N - 1]) continue;
        while (k > 0 && best[k - 1] < s_heard[i].rssi) {
            if (k < ANCHOR_N) { best[k] = best[k - 1]; memcpy(out[k], out[k - 1], 6); }
            k--;
        }
        if (k < ANCHOR_N) { best[k] = s_heard[i].rssi; memcpy(out[k], s_heard[i].bssid, 6); }
        if (n < ANCHOR_N) n++;
    }
    return n;
}

void startPlace(uint32_t nowMs) {
    Place* p;
    if (s_placesN < CAP) { p = &s_places[s_placesN]; s_newest = s_placesN++; }
    else {
        // The oldest slot (the one after the newest in a ring) is reused.
        s_newest = (uint8_t)((s_newest + 1) % CAP);
        p = &s_places[s_newest];
    }
    memset(p, 0, sizeof *p);
    p->id      = s_nextId++;
    if (s_nextId == 0) s_nextId = 1;
    p->startMs = nowMs;
    p->anchorN = strongest(nowMs, p->anchor);
    p->home    = s_homeHeard;
    s_awayChecks = 0;
}

uint8_t anchorsHeard(const Place& p, uint32_t nowMs) {
    uint8_t k = 0;
    for (uint8_t a = 0; a < p.anchorN; a++)
        for (uint8_t i = 0; i < s_heardN; i++)
            if (memcmp(s_heard[i].bssid, p.anchor[a], 6) == 0) {
                if ((uint32_t)(nowMs - s_heard[i].lastMs) <= HEARD_MS) k++;
                break;
            }
    return k;
}

}  // namespace

void begin() { reset(); }

void reset() {
    s_heardN = 0; s_placesN = 0; s_newest = 0; s_nextId = 1;
    s_lastCheck = 0; s_awayChecks = 0; s_homeHeard = false;
    // s_homeFn stays: it is wiring, not state.
}

void setHomeCheck(bool (*isHome)(const char* ssid)) { s_homeFn = isHome; }

void noteBssid(const uint8_t* bssid, int8_t rssi, const char* ssid, uint32_t nowMs) {
    if (!bssid) return;
    if (ssid && ssid[0] && s_homeFn && s_homeFn(ssid)) { s_homeHeard = true; if (Place* p = cur()) p->home = true; }
    int idx = -1, oldest = 0;
    for (uint8_t i = 0; i < s_heardN; i++) {
        if (memcmp(s_heard[i].bssid, bssid, 6) == 0) { idx = i; break; }
        if ((int32_t)(s_heard[i].lastMs - s_heard[oldest].lastMs) < 0) oldest = i;
    }
    if (idx < 0) {
        if (s_heardN < HEARD_CAP) idx = s_heardN++;
        else idx = oldest;
        memcpy(s_heard[idx].bssid, bssid, 6);
    }
    s_heard[idx].rssi   = rssi;
    s_heard[idx].lastMs = nowMs;
    // The first network heard starts the first place, so the trail has
    // somewhere to begin without waiting for a check.
    if (!s_placesN) startPlace(nowMs);
    else if (cur()->anchorN < MIN_ANCHORS) {
        // A place begun with too few networks to fingerprint takes the ones
        // heard in its first half minute, so a board that boots indoors still
        // gets a usable anchor set.
        Place* p = cur();
        if ((uint32_t)(nowMs - p->startMs) < HEARD_MS) p->anchorN = strongest(nowMs, p->anchor);
    }
}

static uint16_t metres(int32_t la1_7, int32_t lo1_7, int32_t la2_7, int32_t lo2_7) {
    const double R = 6371000.0, k = 3.14159265358979 / 180.0 / 1e7;
    const double la1 = la1_7 * k, la2 = la2_7 * k, dl = (lo2_7 - lo1_7) * k, dp = la2 - la1;
    const double h = sin(dp / 2) * sin(dp / 2) + cos(la1) * cos(la2) * sin(dl / 2) * sin(dl / 2);
    double m = 2.0 * R * asin(sqrt(h));
    if (m > 65000.0) m = 65000.0;
    return (uint16_t)m;
}

void noteFix(int32_t lat7, int32_t lon7, uint32_t nowMs) {
    if (!s_placesN) startPlace(nowMs);
    Place* p = cur();
    if (!p->hasPos) { p->hasPos = true; p->lat7 = lat7; p->lon7 = lon7; return; }
    // Moved NEW_PLACE_M from where this place began: a new place, here.
    if (metres(p->lat7, p->lon7, lat7, lon7) >= NEW_PLACE_M) {
        s_homeHeard = false;
        startPlace(nowMs);
        Place* q = cur(); q->hasPos = true; q->lat7 = lat7; q->lon7 = lon7;
    }
}

void tick(uint32_t nowMs) {
    Place* p = cur();
    if (!p) return;
    if ((uint32_t)(nowMs - s_lastCheck) < CHECK_MS) return;
    s_lastCheck = nowMs;
    // A place that never got a fingerprint cannot be left by WiFi; only a
    // GPS fix (noteFix) moves on from it.
    if (p->anchorN < MIN_ANCHORS) return;
    const uint8_t heard = anchorsHeard(*p, nowMs);
    const bool gone = heard * 4 <= p->anchorN;    // a quarter or less still here
    if (!gone) { s_awayChecks = 0; return; }
    if (++s_awayChecks < 2) return;
    // Nothing new to fingerprint yet (a lift, a car with no networks): wait,
    // or the next place would be empty and this one would never end.
    uint8_t probe[ANCHOR_N][6];
    if (strongest(nowMs, probe) < MIN_ANCHORS) return;
    s_homeHeard = false;
    startPlace(nowMs);
}

uint8_t current() { const Place* p = cur(); return p ? p->id : 0; }
uint32_t startMs(uint8_t id) { const Place* p = find(id); return p ? p->startMs : 0; }
bool isHome(uint8_t id) { const Place* p = find(id); return p && p->home; }
bool hasPos(uint8_t id) { const Place* p = find(id); return p && p->hasPos; }
int32_t lat7(uint8_t id) { const Place* p = find(id); return p ? p->lat7 : 0; }
int32_t lon7(uint8_t id) { const Place* p = find(id); return p ? p->lon7 : 0; }

uint16_t distanceM(uint8_t a, uint8_t b) {
    const Place* pa = find(a); const Place* pb = find(b);
    if (!pa || !pb || !pa->hasPos || !pb->hasPos) return 0;
    return metres(pa->lat7, pa->lon7, pb->lat7, pb->lon7);
}

uint8_t countBetweenMin(uint16_t fromMin, uint16_t toMin) {
    // Places begun inside the window, plus the one the window started in.
    uint8_t inside = 0; bool before = false;
    for (uint8_t i = 0; i < s_placesN; i++) {
        const uint16_t m = (uint16_t)(s_places[i].startMs / 60000u);
        if ((int16_t)(m - fromMin) > 0 && (int16_t)(toMin - m) >= 0) inside++;
        else if ((int16_t)(m - fromMin) <= 0) before = true;
    }
    return (uint8_t)(inside + (before ? 1 : 0));
}

void forceNew(uint32_t nowMs, bool home) { s_homeHeard = home; startPlace(nowMs); }
void forceNewAt(uint32_t nowMs, int32_t la, int32_t lo, bool home) {
    forceNew(nowMs, home);
    Place* p = cur(); p->hasPos = true; p->lat7 = la; p->lon7 = lo;
}

}  // namespace Places
