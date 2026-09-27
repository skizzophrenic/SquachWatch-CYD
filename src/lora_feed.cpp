// SquachWatch-CYD — the MeshCore adverts feed. See include/lora_feed.h for why
// this is a poll beside the queue rather than a third source inside it, and for
// the measurements behind every constant.
//
// WHAT IS PURE AND WHAT IS NOT. parse(), prefix(), ingest() and match() are
// arithmetic and string work over buffers -- no socket, no clock, no task -- so
// the host test drives all four, including the ambiguous case that is the whole
// reason this module has three answers instead of two. Only poll() and tick()
// touch the network, and they reach it through Enrich::fetch(), which is the one
// socket path this firmware has and the one a test can stand in for.
#include "lora_feed.h"
#include "lora_json.h"
#include "lora_enrich.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if SQUACH_LORA
#include <Arduino.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#endif

namespace Lora {
namespace Feed {
namespace {

// A held row, with the one piece of bookkeeping the public Row has no business
// carrying: when it arrived, for eviction. 48 bytes.
struct Slot {
    Row      r;
    uint32_t seq;
};

Slot*    s_rows = nullptr;      // PSRAM; see begin()
uint8_t  s_n = 0;
uint32_t s_seq = 0;             // arrival order, monotonic for the boot

// The body, and the staging array parse() writes before the merge. Both PSRAM.
char*    s_body = nullptr;
size_t   s_bodyCap = 0;
Row*     s_stage = nullptr;

bool     s_master = false, s_feed = false;
uint32_t s_polls = 0, s_seen = 0, s_writes = 0;
uint32_t s_nextMs = 0;          // 0 = due now, never polled

// The table is written by the worker task and read by loop() for the screen, so
// it is guarded -- the same shape as Enrich's own lock, and compiled away on a
// desktop where there is one thread.
#if SQUACH_LORA
SemaphoreHandle_t s_lock = nullptr;
inline void lock()   { if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY); }
inline void unlock() { if (s_lock) xSemaphoreGive(s_lock); }
#else
inline void lock()   {}
inline void unlock() {}
#endif

// One hex pair to a byte; -1 on anything that is not hex. A key that is not 64
// hex characters is a row this does not keep, rather than a row with a guessed
// key in it.
int hexByte(const char* p) {
    int v = 0;
    for (int i = 0; i < 2; i++) {
        const char c = p[i];
        int d;
        if (c >= '0' && c <= '9')      d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return -1;
        v = (v << 4) | d;
    }
    return v;
}

void put(char* dst, size_t cap, const char* src) {
    if (!src || !src[0]) { dst[0] = '\0'; return; }
    strncpy(dst, src, cap - 1);
    dst[cap - 1] = '\0';
}

// EVERYTHING BELOW REQUIRES THE LOCK and none of it takes the lock itself: the
// mutex is not recursive, so a public function takes it once and calls only
// these.
int findLocked(const uint8_t* key) {
    for (uint8_t i = 0; i < s_n; i++)
        if (memcmp(s_rows[i].r.key, key, KEY_BYTES) == 0) return i;
    return -1;
}

// One write, whole. Never on the frame path -- this runs on the worker task
// during a WiFi window, when the backlight is already down and nothing is being
// drawn (docs/CROWPANEL7.md on why scattered PSRAM writes twitch the picture).
bool rememberLocked(const Row& r) {
    if (!s_rows) return false;
    const int ix = findLocked(r.key);
    if (ix >= 0) {
        // Same node, seen again. Its name may have changed -- operators rename
        // nodes -- so the row is refreshed, AND it moves to the front of the
        // eviction order. Least-recently-SEEN, not first-arrived: the feed's
        // window rolls, so a node that keeps appearing in it is a node still on
        // the air, and keeping first-arrival order would evict it after four
        // polls and re-add it on the fifth, churning the table for nothing. It
        // costs one slot to hold it, not ninety-six.
        s_rows[ix].r = r;
        s_rows[ix].seq = ++s_seq;
        s_writes++;
        return false;
    }
    Slot* slot = nullptr;
    if (s_n < ROW_MAX) slot = &s_rows[s_n++];
    else {
        // The least recently seen goes. Not least-recently-MATCHED, which would
        // be the cleverer policy and would cost a write to PSRAM from the frame
        // path every time a screen redrew a matched row -- exactly the pattern
        // the panel cannot afford (docs/CROWPANEL7.md).
        slot = &s_rows[0];
        for (uint8_t i = 1; i < ROW_MAX; i++) if (s_rows[i].seq < slot->seq) slot = &s_rows[i];
    }
    slot->r = r;
    slot->seq = ++s_seq;
    s_writes++;
    return true;
}

}  // namespace

const char* matchText(uint8_t m) {
    switch (m) {
        case MATCH_ONE:       return "named";
        // A one-byte hit. Deliberately not "named": the header works out that
        // such a name is wrong between 27 and 77 % of the time depending on how
        // much of the local mesh the feed happens to carry, so the honest word
        // is that the feed holds a candidate, not that we know who this is.
        case MATCH_WEAK:      return "one-byte candidate";
        // Said in words, because it is the interesting answer: the feed knows
        // a node with this prefix and cannot tell us WHICH, and the difference
        // between that and "not in the feed" is the difference between a
        // one-byte row and an eight-byte one.
        case MATCH_AMBIGUOUS: return "ambiguous";
        default:              return "";
    }
}

// ---- the prefix ---------------------------------------------------------------
uint8_t prefix(const Nodes::Node& n, uint8_t* out, size_t cap) {
    if (!out || cap < KEY_BYTES) return 0;
    // MeshCore only. Every other protocol names its sender in a way this feed
    // has never heard of -- a node number, a DevAddr, a callsign, an OGN
    // address -- and there is no prefix of an Ed25519 key to be had from any of
    // them. Not a policy, an arithmetic fact.
    if (n.proto != Proto::MESHCORE) return 0;
    // The relay-only marker: 0x01 in the top byte, bytes 1..6 zero, the path
    // hash in the low byte. See the header for why one byte is still worth
    // trying and why its usual answer is "ambiguous".
    if ((uint8_t)(n.id >> 56) == 0x01 && (n.id & 0x00FFFFFFFFFFFF00ull) == 0) {
        out[0] = (uint8_t)(n.id & 0xFF);
        return 1;
    }
    for (uint8_t i = 0; i < KEY_BYTES; i++) out[i] = (uint8_t)(n.id >> (56 - 8 * i));
    return KEY_BYTES;
}

Match match(const Nodes::Node& n, Row& out, uint8_t& candidates) {
    candidates = 0;
    memset(&out, 0, sizeof out);
    uint8_t pre[KEY_BYTES];
    const uint8_t np = prefix(n, pre, sizeof pre);
    if (!np || !s_rows) return MATCH_NONE;
    lock();
    int first = -1;
    uint16_t hits = 0;
    for (uint8_t i = 0; i < s_n; i++) {
        if (memcmp(s_rows[i].r.key, pre, np) != 0) continue;
        if (first < 0) first = i;
        hits++;
    }
    // Copied out only for an EIGHT-byte single hit. A caller handed a Row will
    // show it whatever the return value was documented to mean - that is not a
    // hypothetical, it is what ui_lora.cpp did with the one-byte case until the
    // two stopped sharing a return value. So the row leaves this function only
    // when it has been earned, and MATCH_WEAK and MATCH_AMBIGUOUS both hand
    // back the zeroed `out` they were given.
    if (hits == 1 && np >= KEY_BYTES) out = s_rows[first].r;
    unlock();
    candidates = (uint8_t)(hits > 255 ? 255 : hits);
    if (hits == 0) return MATCH_NONE;
    if (hits > 1)  return MATCH_AMBIGUOUS;
    // One hit, and now the only question that matters: on how much evidence.
    // Eight bytes is a name. One byte is a coincidence 8.5 to 25 % of the time
    // on its own, and the header works the combined figure out at 27 to 77 %.
    // The two used to share a return value, and the screen printed a name for
    // both - on exactly the population this feed exists to serve.
    return np >= KEY_BYTES ? MATCH_ONE : MATCH_WEAK;
}

// ---- the parser ---------------------------------------------------------------
uint8_t parse(const char* body, size_t len, int code, Row* out, uint8_t cap) {
    if (code != 200 || !body || !len || !out || !cap) return 0;
    const char* b = body;
    const char* e = body + len;
    const char* ab = nullptr;
    const char* ae = nullptr;
    // {"adverts":[ .. ]} -- the envelope, measured. The array is found by name
    // rather than by the first '[' in the document: a name containing a bracket
    // sits earlier in some other body and would start the walk inside a string.
    if (!Json::arr(b, e, "adverts", ab, ae)) {
        // No closing bracket. That is what a TRUNCATED body looks like -- the
        // feed is read into a fixed buffer and the server decides how long the
        // answer is -- and the rows before the cut are still perfectly good. So:
        // require the key, then walk the whole of what arrived. nextObj() steps
        // over the unterminated containers (the document's own brace, and the
        // row the cut fell in) and yields the complete rows between them.
        if (!Json::has(b, e, "\"adverts\"")) return 0;
        ab = b;
        ae = e;
    }
    uint8_t got = 0;
    const char* p = ab;
    const char* ob = nullptr;
    const char* oe = nullptr;
    while (got < cap && Json::nextObj(p, ae, ob, oe)) {
        char hex[72] = {0};
        // THE ALLOW-LIST, all of it. Three keys. The row also carries lat, lon,
        // observers, avg_rssi, avg_snr, hop_count, iata, first_seen, recv_ts and
        // a `hash` -- and not one of them is read, so not one of them can reach
        // a screen. Position especially: see the header.
        if (!Json::str(ob, oe, "advert_pubkey", hex, sizeof hex)) continue;
        // 64 hex characters or nothing. A SHORT key is the dangerous one: its
        // first bytes would match a prefix and name the wrong node, so length is
        // checked before anything is decoded. Only the first sixteen characters
        // are then validated as hex, because only the first eight bytes are kept
        // -- a row with rubbish in the tail of a 64-character key yields a
        // prefix that is either a real node's or nobody's, and the ambiguity
        // machinery handles both.
        if (strlen(hex) != 64) continue;
        Row r;
        memset(&r, 0, sizeof r);
        bool ok = true;
        for (uint8_t i = 0; i < KEY_BYTES && ok; i++) {
            const int v = hexByte(hex + 2 * i);
            if (v < 0) ok = false;
            else r.key[i] = (uint8_t)v;
        }
        if (!ok) continue;
        char v[40];
        if (Json::str(ob, oe, "name", v, sizeof v))      put(r.name, sizeof r.name, v);
        if (Json::str(ob, oe, "role_name", v, sizeof v)) put(r.role, sizeof r.role, v);
        // A row with no name is a row with nothing to give: the node table
        // already knows the role of anything it heard advertise, and for a
        // relay-only row a bare "repeater" is what it says anyway.
        if (!r.name[0]) continue;
        out[got++] = r;
    }
    return got;
}

uint8_t ingest(const Row* rows, uint8_t n) {
    if (!rows || !s_rows) return 0;
    uint8_t added = 0;
    lock();
    for (uint8_t i = 0; i < n; i++) if (rememberLocked(rows[i])) added++;
    s_seen += n;
    unlock();
    return added;
}

// ---- the table ---------------------------------------------------------------
void begin() {
#if SQUACH_LORA
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
#endif
    if (s_rows) return;
    const size_t rowsNeed  = sizeof(Slot) * ROW_MAX;      // 96 x 48 = 4,608
    const size_t stageNeed = sizeof(Row) * LIMIT;         // 24 x 44 = 1,056
    // 12 kB against a body measured at 8,141 bytes for limit=25, which is 47 %
    // of headroom for names longer than the 31 bytes the longest measured one
    // had. A body that still overruns it is truncated, not corrupted: parse()
    // keeps the rows before the cut.
    const size_t bodyCap   = 12288;
#if SQUACH_LORA
    // PSRAM, all of it: 17.9 kB against 7.6 MB out there and about 57 kB of
    // CONTIGUOUS internal RAM that this deliberately does not touch (measured
    // 2026-09-26). The same split Enrich::begin() makes, and for the same
    // reason -- what stays internal is the handful of counters above.
    s_rows  = (Slot*)heap_caps_malloc(rowsNeed,  MALLOC_CAP_SPIRAM);
    s_stage = (Row*) heap_caps_malloc(stageNeed, MALLOC_CAP_SPIRAM);
    s_body  = (char*)heap_caps_malloc(bodyCap,   MALLOC_CAP_SPIRAM);
#else
    s_rows  = (Slot*)malloc(rowsNeed);
    s_stage = (Row*) malloc(stageNeed);
    s_body  = (char*)malloc(bodyCap);
#endif
    if (s_rows)  memset(s_rows, 0, rowsNeed);
    if (s_stage) memset(s_stage, 0, stageNeed);
    if (s_body)  { memset(s_body, 0, bodyCap); s_bodyCap = bodyCap; }
}

void setArmed(bool master, bool feed) {
    // Called from loop() every pass, so it does nothing until something changes.
    if (master == s_master && feed == s_feed) return;
    s_master = master; s_feed = feed;
}
bool armed() { return s_master && s_feed; }

uint8_t  rowCount()    { return s_n; }
uint32_t polls()       { return s_polls; }
uint32_t rowsSeen()    { return s_seen; }
uint32_t tableWrites() { return s_writes; }

bool rowAt(uint8_t i, Row& out) {
    lock();
    bool ok = false;
    // Newest arrival first, by seq -- the table itself is in no order, because
    // eviction fills holes wherever they fall.
    // The i-th newest arrival, found by walking down from the top: the seq
    // values are unique, so "the largest below the last one" needs no sort and
    // no array on the stack. A console listing is n short walks, which is the
    // only caller and is not on the frame path.
    uint32_t below = 0xFFFFFFFFu;
    for (uint8_t k = 0; k <= i && k < s_n; k++) {
        int best = -1;
        for (uint8_t j = 0; j < s_n; j++)
            if (s_rows[j].seq < below && (best < 0 || s_rows[j].seq > s_rows[best].seq)) best = j;
        if (best < 0) break;
        if (k == i) { out = s_rows[best].r; ok = true; break; }
        below = s_rows[best].seq;
    }
    unlock();
    return ok;
}

void clearAll() {
    lock();
    s_n = 0;
    s_seq = 0;
    s_polls = s_seen = s_writes = 0;
    s_nextMs = 0;
    if (s_rows) memset(s_rows, 0, sizeof(Slot) * ROW_MAX);
    unlock();
}

uint32_t dueInMs(uint32_t now) {
    if (!s_nextMs) return 0;
    const int32_t d = (int32_t)(s_nextMs - now);
    return d > 0 ? (uint32_t)d : 0;
}

// ---- the poll -----------------------------------------------------------------
bool poll(uint32_t now) {
    if (!armed() || !s_rows || !s_stage || !s_body) return false;
    if (!Enrich::netUp()) return false;
    if (s_nextMs && (int32_t)(now - s_nextMs) < 0) return false;
    // The next slot is claimed before the request goes out, not after it comes
    // back: two workers must not both find the feed due. Zero is the "never
    // polled" sentinel, so a sum that lands exactly on it is nudged by a
    // millisecond rather than reading as due again.
    s_nextMs = now + PERIOD_MS;
    if (!s_nextMs) s_nextMs = 1;

    // The whole of what leaves this board: a row count, as text, because that
    // is the shape Enrich's one URL builder takes. There is nothing else to
    // pass and no way to pass anything else.
    char lim[8];
    snprintf(lim, sizeof lim, "%u", (unsigned)LIMIT);

    size_t len = 0;
    const int code = Enrich::fetch(Enrich::SRC_MC_FEED, lim, s_body, s_bodyCap, len);
    // Logged before the reply is interpreted, so the log records what left the
    // device and not what the device made of the answer. Same log, same console
    // command, same audit surface as the per-identifier lookups.
    Enrich::logRequest(Enrich::SRC_MC_FEED, lim, code, len, now);
    s_polls++;
    if (code != 200) return true;
    const uint8_t got = parse(s_body, len, code, s_stage, (uint8_t)LIMIT);
    if (got) ingest(s_stage, got);
    return true;
}

// ---- the worker ---------------------------------------------------------------
#if SQUACH_LORA

namespace {
TaskHandle_t s_task = nullptr;
bool wifiUp() { return WiFi.status() == WL_CONNECTED; }

void worker(void*) {
    // One poll and out. Not a long-lived task: it exists only while WiFi is up
    // for another reason, and the moment that stops being true it has no
    // business running.
    poll(millis());
    s_task = nullptr;
    vTaskDelete(nullptr);
}
}  // namespace

bool running() { return s_task != nullptr; }

void tick(uint32_t now) {
    if (s_task || !armed() || !wifiUp()) return;
    if (dueInMs(now)) return;
    // Never alongside the queue's worker. Both would work -- they are separate
    // sockets to separate hosts -- but there is no reason to have two requests
    // in flight from a board that is deaf for the duration, and a feed that is
    // five minutes from urgent can wait for the queue to drain.
    if (Enrich::running()) return;
    // 5 kB, the same as Enrich's worker and for the same measurement: 3.2 kB of
    // ota_wifi.cpp's own plain-HTTP task for this HTTPClient shape. The body,
    // the staging rows and the table are all in PSRAM, not on this stack.
    if (xTaskCreatePinnedToCore(worker, "lorafeed", 5120, nullptr, 1, &s_task, 1) != pdPASS)
        s_task = nullptr;
}

#else
bool running() { return false; }
void tick(uint32_t) {}
#endif

}
}
