// SquachWatch-CYD — the antenna survey. See include/lora_survey.h for what it
// measures and for the trap the whole design is built around.
//
// Standalone: <stdint.h> and string work only, no Arduino core and no radio, so
// the arithmetic that decides "this antenna is better" is checked on a desktop
// (test/lora_survey_test.cpp). That matters more here than anywhere else in the
// LoRa code -- a median or a paired delta that is subtly wrong does not crash,
// it prints a confident number and sends somebody up a mast.
#include "lora_survey.h"
#include <string.h>
#include <stdio.h>

namespace Lora {
namespace Survey {

namespace {

// One node's readings inside one run. The two histograms are the whole reason
// a run costs what it does, and they buy an EXACT median at the resolution the
// packet record carries, for any number of frames, in fixed memory: a ring of
// the last N samples would bias the median toward the end of the run, and a
// running mean cannot produce a median at all.
struct NodeAcc {
    uint64_t id;
    Proto    proto;
    bool     used;
    char     tag[12];
    uint16_t frames;
    int16_t  rssiMin, rssiMax;
    int8_t   snrMin4, snrMax4;
    uint32_t firstMs, lastMs;
    uint16_t outOfRange;
    uint16_t rssiBin[RSSI_BINS];
    uint16_t snrBin[SNR_BINS];
};

// The live line, per node: a ring of the last TREND_LEN readings.
struct TrendRow {
    uint64_t id;
    Proto    proto;
    bool     used;
    char     tag[12];
    uint32_t lastMs;
    uint16_t count;     // readings ever, so a view can say "64 of 300"
    uint8_t  head;      // where the next sample goes
    Sample   s[TREND_LEN];
};

struct Store {
    Run      runs[RUNS];
    NodeAcc  acc[RUNS][RUN_NODES];
    TrendRow trend[TREND_NODES];
};

Store* s_st = nullptr;
int8_t s_rec = -1;      // the run being recorded, or -1

// Floor division by two, which is what averaging two dBm readings wants: C++
// truncates toward zero, so (-91 + -90) / 2 is -90 and quietly reports the
// stronger of the two. -91 is the honest answer.
int16_t floorHalf(int32_t sum) {
    return (int16_t)(sum >= 0 ? sum / 2 : -((-sum + 1) / 2));
}

// The order statistic at index k (0-based) of a histogram, in the histogram's
// own units. `n` must be the histogram's total count.
int32_t nth(const uint16_t* bin, uint16_t bins, int32_t base, uint32_t k) {
    uint32_t seen = 0;
    for (uint16_t i = 0; i < bins; i++) {
        seen += bin[i];
        if (seen > k) return base + (int32_t)i;
    }
    return base + (int32_t)bins - 1;
}

// The median of a histogram. Odd counts give the middle reading; even counts
// average the two middle readings and round DOWN, toward the weaker signal, so
// that an even number of frames never reports a better figure than an odd
// number would have.
int16_t histMedian(const uint16_t* bin, uint16_t bins, int32_t base, uint32_t n) {
    if (!n) return 0;
    const int32_t lo = nth(bin, bins, base, (n - 1) / 2);
    const int32_t hi = nth(bin, bins, base, n / 2);
    return floorHalf(lo + hi);
}

// The median of the paired deltas. NOT the same rounding as histMedian, and the
// difference is the whole correctness of the verdict.
//
// histMedian rounds a LEVEL, where down means weaker, so flooring a half is the
// conservative choice. This medians a signed DIFFERENCE, where down is a
// direction and not a magnitude - and flooring a difference is conservative in
// one direction and anti-conservative in the other. A true median of +1.5 dB
// floors to +1 and understates the gain; a true median of -1.5 dB floors to -2
// and overstates the loss.
//
// The consequence is that the answer was not mirror-symmetric, and which run an
// owner calls A is an arbitrary choice that must only negate the result.
// Measured over every delta set of length 3 to 6 drawn from -2..+2, 19,500
// sets: 316 of them (1.6 %) changed the verdict WORD when A and B were swapped,
// always "B WORSE" one way and "NO CHANGE" the other, and 7,380 (38 %) changed
// the magnitude - [-2 -2 -1 -1] read "B WORSE -2 dB" and negated read "B BETTER
// +1 dB", the same physics a decibel apart.
//
// Rounding a half toward zero fixes it: median(-x) == -median(x) for every
// input, and a half still never exaggerates, in either direction.
//
// Sorts in place; the caller's array is scratch.
int16_t arrayMedian(int16_t* v, uint16_t n) {
    if (!n) return 0;
    for (uint16_t i = 1; i < n; i++) {
        const int16_t x = v[i];
        int j = (int)i - 1;
        while (j >= 0 && v[j] > x) { v[j + 1] = v[j]; j--; }
        v[j + 1] = x;
    }
    const int32_t sum = (int32_t)v[(n - 1) / 2] + (int32_t)v[n / 2];
    // Truncating division in C rounds toward zero, which is exactly the rule.
    return (int16_t)(sum / 2);
}

// The exact two-sided sign test, in per-mille. See Compare::signPermille.
// The binomial recurrence C(n,k+1) = C(n,k)*(n-k)/(k+1) is exact in integers --
// C(n,k)*(n-k) is always divisible by k+1 -- and with n bounded by RUN_NODES
// (32) the sum stays inside a uint64 with room for the 2000 multiplier.
uint16_t signTestPermille(uint16_t better, uint16_t worse) {
    const uint32_t n = (uint32_t)better + worse;
    if (!n) return 1000;
    const uint32_t m = better < worse ? better : worse;
    uint64_t sum = 0, c = 1;
    for (uint32_t k = 0; k <= m; k++) {
        sum += c;
        c = c * (n - k) / (k + 1);
    }
    const uint64_t denom = 1ull << n;
    const uint64_t pm = (2ull * sum * 1000ull) / denom;
    return pm > 1000 ? (uint16_t)1000 : (uint16_t)pm;
}

NodeAcc* findAcc(uint8_t run, Proto p, uint64_t id) {
    NodeAcc* a = s_st->acc[run];
    for (uint8_t i = 0; i < RUN_NODES; i++) if (a[i].used && a[i].proto == p && a[i].id == id) return &a[i];
    return nullptr;
}

void fillStats(const NodeAcc& a, NodeStats& out) {
    memset(&out, 0, sizeof out);
    out.proto = a.proto; out.id = a.id;
    memcpy(out.tag, a.tag, sizeof out.tag);
    out.frames = a.frames;
    out.rssiMin = a.rssiMin; out.rssiMax = a.rssiMax;
    out.snrMin4 = a.snrMin4; out.snrMax4 = a.snrMax4;
    out.firstMs = a.firstMs; out.lastMs = a.lastMs;
    out.outOfRange = a.outOfRange;
    out.enough = a.frames >= EVIDENCE_MIN;
    // The count the histograms actually hold: a reading outside the RSSI
    // bounds was counted in `frames` and in nothing else, so the RSSI median
    // is over the readings that landed in range. outOfRange says how many did
    // not, and it is zero on working hardware.
    const uint32_t nR = a.frames > a.outOfRange ? (uint32_t)(a.frames - a.outOfRange) : 0;
    // With nothing in range there is no median to take, and 0 dBm would be a
    // lie rather than a gap. The extreme we do hold is the honest stand-in, and
    // outOfRange being non-zero is how a reader knows not to trust it. Only
    // reachable when the radio driver reports a value no receiver can produce.
    out.rssiMed = nR ? (int16_t)histMedian(a.rssiBin, RSSI_BINS, RSSI_LO, nR) : a.rssiMin;
    out.snrMed4 = (int16_t)histMedian(a.snrBin, SNR_BINS, -128, a.frames);
}

// Rows of a run, ordered loudest median first with the under-evidence rows
// last. Only the order is built here -- an array of 32 NodeStats is 1.8 kB and
// this runs on loop()'s stack.
uint8_t orderRun(uint8_t run, uint8_t* ord) {
    int16_t key[RUN_NODES];
    uint8_t n = 0;
    const NodeAcc* a = s_st->acc[run];
    for (uint8_t i = 0; i < RUN_NODES; i++) {
        if (!a[i].used) continue;
        const uint32_t nR = a[i].frames > a[i].outOfRange ? (uint32_t)(a[i].frames - a[i].outOfRange) : 0;
        // Under the evidence floor the row sorts below everything: its median
        // is not a figure to rank by. -32768 is the sort's sentinel, never a
        // reading -- RSSI_LO is -160.
        // nR == 0 with frames >= EVIDENCE_MIN means every reading was outside
        // the RSSI bounds, and histMedian over an empty histogram returns 0 --
        // which as a sort key is louder than any real signal and would put a
        // driver fault at the top of the list. fillStats falls back to the
        // extreme actually held for exactly this row; the order has to use the
        // same figure the row reports, or the two disagree about one median.
        key[n] = a[i].frames >= EVIDENCE_MIN
                     ? (nR ? (int16_t)histMedian(a[i].rssiBin, RSSI_BINS, RSSI_LO, nR) : a[i].rssiMin)
                     : (int16_t)-32768;
        ord[n] = i;
        n++;
    }
    for (uint8_t i = 1; i < n; i++) {
        const uint8_t v = ord[i]; const int16_t k = key[i];
        int j = (int)i - 1;
        while (j >= 0 && key[j] < k) { key[j + 1] = key[j]; ord[j + 1] = ord[j]; j--; }
        key[j + 1] = k; ord[j + 1] = v;
    }
    return n;
}

// One node present in both runs, in the smallest form the sort needs.
struct PairCore {
    uint8_t ai, bi;
    int16_t dRssi, dSnr4;
    bool    enough;
};

// Every node A and B share, with A's and B's medians differenced. Returns how
// many, and fills gained/lost/thin for the ones they do not share.
uint8_t buildPairs(uint8_t a, uint8_t b, PairCore* out, uint16_t& gained, uint16_t& lost, uint16_t& thin) {
    gained = lost = thin = 0;
    uint8_t n = 0;
    const NodeAcc* ra = s_st->acc[a];
    const NodeAcc* rb = s_st->acc[b];
    for (uint8_t i = 0; i < RUN_NODES; i++) {
        if (!ra[i].used) continue;
        NodeStats sa; fillStats(ra[i], sa);
        const NodeAcc* m = nullptr; uint8_t mi = 0;
        for (uint8_t j = 0; j < RUN_NODES; j++)
            if (rb[j].used && rb[j].proto == ra[i].proto && rb[j].id == ra[i].id) { m = &rb[j]; mi = j; break; }
        if (!m) { if (sa.enough) lost++; continue; }
        NodeStats sb; fillStats(*m, sb);
        if (n < RUN_NODES) {
            out[n].ai = i; out[n].bi = mi;
            out[n].dRssi = (int16_t)(sb.rssiMed - sa.rssiMed);
            out[n].dSnr4 = (int16_t)(sb.snrMed4 - sa.snrMed4);
            out[n].enough = sa.enough && sb.enough;
            if (!out[n].enough) thin++;
            n++;
        }
    }
    for (uint8_t j = 0; j < RUN_NODES; j++) {
        if (!rb[j].used) continue;
        bool inA = false;
        for (uint8_t i = 0; i < RUN_NODES; i++)
            if (ra[i].used && ra[i].proto == rb[j].proto && ra[i].id == rb[j].id) { inA = true; break; }
        if (inA) continue;
        if (rb[j].frames >= EVIDENCE_MIN) gained++;
    }
    return n;
}

}  // namespace

size_t bytesNeeded() { return sizeof(Store); }

bool begin(void* mem, size_t bytes) {
    if (!mem || bytes < sizeof(Store)) return false;
    s_st = (Store*)mem;
    memset(s_st, 0, sizeof(Store));
    s_rec = -1;
    return true;
}

bool ready() { return s_st != nullptr; }

void clear() {
    if (!s_st) return;
    memset(s_st, 0, sizeof(Store));
    s_rec = -1;
}

bool clearRun(uint8_t i) {
    if (!s_st || i >= RUNS || !s_st->runs[i].used) return false;
    // The recording run is allowed, and it is the common case: a mis-started
    // run is not a finished one. Recording stops with it rather than rolling on
    // into a slot that has been zeroed under the radio task.
    if (s_rec == (int8_t)i) s_rec = -1;
    memset(&s_st->runs[i], 0, sizeof s_st->runs[i]);
    memset(s_st->acc[i], 0, sizeof s_st->acc[i]);
    // Nothing is shifted down: see clearRun's comment in the header for why a
    // run's number has to survive its neighbour going away.
    return true;
}

// ---- feeding ---------------------------------------------------------------

void noteDirect(Proto p, uint64_t id, const char* tag, int16_t rssi, int8_t snr4, uint32_t now) {
    if (!s_st) return;

    // The live line first: it is fed whether or not anything is recording,
    // because it is what the owner watches while turning the antenna.
    TrendRow* t = nullptr;
    for (uint8_t i = 0; i < TREND_NODES; i++)
        if (s_st->trend[i].used && s_st->trend[i].proto == p && s_st->trend[i].id == id) { t = &s_st->trend[i]; break; }
    if (!t) {
        for (uint8_t i = 0; i < TREND_NODES; i++) if (!s_st->trend[i].used) { t = &s_st->trend[i]; break; }
        if (!t) {
            // Full: the station unheard for longest gives way, the same rule
            // the node table uses. A trend is about now.
            t = &s_st->trend[0];
            for (uint8_t i = 1; i < TREND_NODES; i++) if (s_st->trend[i].lastMs < t->lastMs) t = &s_st->trend[i];
        }
        memset(t, 0, sizeof *t);
        t->used = true; t->proto = p; t->id = id;
    }
    if (tag && tag[0]) { strncpy(t->tag, tag, sizeof t->tag - 1); t->tag[sizeof t->tag - 1] = '\0'; }
    t->lastMs = now;
    Sample& sm = t->s[t->head];
    sm.ms = now; sm.rssi = rssi; sm.snr4 = snr4; sm.reserved = 0;
    t->head = (uint8_t)((t->head + 1) % TREND_LEN);
    if (t->count < 0xFFFF) t->count++;

    if (s_rec < 0) return;
    Run& r = s_st->runs[s_rec];
    NodeAcc* a = findAcc((uint8_t)s_rec, p, id);
    if (!a) {
        for (uint8_t i = 0; i < RUN_NODES; i++) if (!s_st->acc[s_rec][i].used) { a = &s_st->acc[s_rec][i]; break; }
        if (!a) {
            // Deliberately no eviction: a row replaced halfway through a run
            // would report a median over half a window, and a wrong figure is
            // worse than a missing one. The run counts what it turned away.
            if (r.framesDropped < 0xFFFF) r.framesDropped++;
            return;
        }
        memset(a, 0, sizeof *a);
        a->used = true; a->proto = p; a->id = id;
        a->firstMs = now;
        a->rssiMin = rssi; a->rssiMax = rssi;
        a->snrMin4 = snr4; a->snrMax4 = snr4;
        r.nodes++;
    }
    if (tag && tag[0]) { strncpy(a->tag, tag, sizeof a->tag - 1); a->tag[sizeof a->tag - 1] = '\0'; }
    a->lastMs = now;
    if (a->frames < 0xFFFF) a->frames++;
    if (rssi < a->rssiMin) a->rssiMin = rssi;
    if (rssi > a->rssiMax) a->rssiMax = rssi;
    if (snr4 < a->snrMin4) a->snrMin4 = snr4;
    if (snr4 > a->snrMax4) a->snrMax4 = snr4;
    if (rssi >= RSSI_LO && rssi <= RSSI_HI) {
        uint16_t& bin = a->rssiBin[rssi - RSSI_LO];
        if (bin < 0xFFFF) bin++;
    } else if (a->outOfRange < 0xFFFF) {
        a->outOfRange++;
    }
    // snr4 is an int8_t and the histogram covers all 256 of its values, so
    // this index cannot be out of range.
    {
        uint16_t& bin = a->snrBin[(uint8_t)((int16_t)snr4 + 128)];
        if (bin < 0xFFFF) bin++;
    }
    if (r.frames < 0xFFFFFFFFu) r.frames++;
}

// ---- runs ------------------------------------------------------------------

int8_t start(const char* labelText, uint8_t profile, bool hopping, uint32_t now) {
    if (!s_st) return -1;
    if (s_rec >= 0) stop(now);
    int8_t slot = -1;
    for (uint8_t i = 0; i < RUNS; i++) if (!s_st->runs[i].used) { slot = (int8_t)i; break; }
    if (slot < 0) {
        // Every slot used: the oldest finished run goes. Oldest by startMs, so
        // a third run never eats one of the two a comparison is about before it
        // eats the one before them.
        for (uint8_t i = 0; i < RUNS; i++) {
            if (s_rec == (int8_t)i) continue;
            if (slot < 0 || s_st->runs[i].startMs < s_st->runs[slot].startMs) slot = (int8_t)i;
        }
    }
    if (slot < 0) return -1;
    memset(&s_st->runs[slot], 0, sizeof s_st->runs[slot]);
    memset(s_st->acc[slot], 0, sizeof s_st->acc[slot]);
    Run& r = s_st->runs[slot];
    r.used = true;
    r.startMs = now;
    r.profile = profile;
    r.hopping = hopping;
    if (labelText && labelText[0]) { strncpy(r.label, labelText, sizeof r.label - 1); r.label[sizeof r.label - 1] = '\0'; }
    else snprintf(r.label, sizeof r.label, "run %u", (unsigned)(slot + 1));
    s_rec = slot;
    return slot;
}

bool stop(uint32_t now) {
    if (!s_st || s_rec < 0) return false;
    s_st->runs[s_rec].stopMs = now ? now : 1;   // 0 means "still recording"
    s_rec = -1;
    return true;
}

int8_t recording() { return s_st ? s_rec : (int8_t)-1; }

bool label(const char* labelText) {
    if (!s_st || s_rec < 0 || !labelText || !labelText[0]) return false;
    strncpy(s_st->runs[s_rec].label, labelText, sizeof s_st->runs[s_rec].label - 1);
    s_st->runs[s_rec].label[sizeof s_st->runs[s_rec].label - 1] = '\0';
    return true;
}

uint8_t runCount() {
    if (!s_st) return 0;
    uint8_t n = 0;
    for (uint8_t i = 0; i < RUNS; i++) if (s_st->runs[i].used) n++;
    return n;
}

bool run(uint8_t i, Run& out) {
    if (!s_st || i >= RUNS || !s_st->runs[i].used) { memset(&out, 0, sizeof out); return false; }
    out = s_st->runs[i];
    return true;
}

uint8_t runNodes(uint8_t runIdx, NodeStats* out, uint8_t cap, uint8_t from) {
    if (!s_st || runIdx >= RUNS || !s_st->runs[runIdx].used || !out || !cap) return 0;
    uint8_t ord[RUN_NODES];
    const uint8_t n = orderRun(runIdx, ord);
    uint8_t w = 0;
    for (uint8_t i = from; i < n && w < cap; i++) fillStats(s_st->acc[runIdx][ord[i]], out[w++]);
    return w;
}

bool runNode(uint8_t runIdx, Proto p, uint64_t id, NodeStats& out) {
    if (!s_st || runIdx >= RUNS || !s_st->runs[runIdx].used) { memset(&out, 0, sizeof out); return false; }
    const NodeAcc* a = findAcc(runIdx, p, id);
    if (!a) { memset(&out, 0, sizeof out); return false; }
    fillStats(*a, out);
    return true;
}

bool compare(uint8_t a, uint8_t b, Compare& out) {
    memset(&out, 0, sizeof out);
    out.a = a; out.b = b;
    out.verdict = V_NO_DATA;
    if (!s_st || a >= RUNS || b >= RUNS || a == b) return false;
    if (!s_st->runs[a].used || !s_st->runs[b].used) return false;
    const Run& ra = s_st->runs[a];
    const Run& rb = s_st->runs[b];

    PairCore pc[RUN_NODES];
    const uint8_t n = buildPairs(a, b, pc, out.gained, out.lost, out.thin);

    int16_t dr[RUN_NODES], ds[RUN_NODES];
    uint16_t k = 0;
    for (uint8_t i = 0; i < n; i++) {
        if (!pc[i].enough) continue;
        dr[k] = pc[i].dRssi; ds[k] = pc[i].dSnr4; k++;
        if (pc[i].dRssi > 0)      out.better++;
        else if (pc[i].dRssi < 0) out.worse++;
        else                      out.same++;
    }
    out.paired = k;
    if (k) {
        out.spreadLo = out.spreadHi = dr[0];
        for (uint16_t i = 1; i < k; i++) {
            if (dr[i] < out.spreadLo) out.spreadLo = dr[i];
            if (dr[i] > out.spreadHi) out.spreadHi = dr[i];
        }
        // arrayMedian sorts its argument, so the spread is taken first.
        out.medDRssi = arrayMedian(dr, k);
        out.medDSnr4 = arrayMedian(ds, k);
    }
    out.signPermille = signTestPermille(out.better, out.worse);

    // Evidence, compared. Frames always; wall time as well when both runs have
    // finished, because a run that listened twice as long had twice the chance
    // of hearing a node at all and "gained a node" would then be a fact about
    // the clock.
    const uint32_t fa = ra.frames, fb = rb.frames;
    const uint32_t loF = fa < fb ? fa : fb, hiF = fa < fb ? fb : fa;
    out.lopsided = loF * 2 < hiF;
    if (ra.stopMs && rb.stopMs) {
        const uint32_t da = ra.stopMs - ra.startMs, db = rb.stopMs - rb.startMs;
        const uint32_t loD = da < db ? da : db, hiD = da < db ? db : da;
        if (loD * 2 < hiD) out.lopsided = true;
    }
    out.incomparable = ra.hopping != rb.hopping;

    // The verdict, in the order include/lora_survey.h states it.
    if (!out.paired)                                   out.verdict = V_NO_DATA;
    else if (out.incomparable)                         out.verdict = V_MIXED;
    else if (out.lopsided)                             out.verdict = V_LOPSIDED;
    else if (out.paired < 3)                           out.verdict = V_THIN;
    else if (out.gained > out.lost && out.medDRssi < 0) out.verdict = V_MIXED;
    else if (out.lost > out.gained && out.medDRssi > 0) out.verdict = V_MIXED;
    else if (out.better && out.worse && out.signPermille > 250) out.verdict = V_MIXED;
    else if (out.medDRssi >= 1 || (out.gained > out.lost && out.medDRssi >= 0)) out.verdict = V_BETTER;
    else if (out.medDRssi <= -1 || (out.lost > out.gained && out.medDRssi <= 0)) out.verdict = V_WORSE;
    else                                               out.verdict = V_SAME;
    return true;
}

uint8_t pairs(uint8_t a, uint8_t b, Pair* out, uint8_t cap, uint8_t from) {
    if (!s_st || a >= RUNS || b >= RUNS || a == b || !out || !cap) return 0;
    if (!s_st->runs[a].used || !s_st->runs[b].used) return 0;
    PairCore pc[RUN_NODES];
    uint16_t g = 0, l = 0, t = 0;
    const uint8_t n = buildPairs(a, b, pc, g, l, t);
    // Biggest improvement first, thin rows last: a view reads down from the
    // node that gained the most, and a row nobody should read a number off
    // does not sit in the middle of that list.
    for (uint8_t i = 1; i < n; i++) {
        const PairCore v = pc[i];
        int j = (int)i - 1;
        while (j >= 0 && ((pc[j].enough == v.enough && pc[j].dRssi < v.dRssi) || (!pc[j].enough && v.enough))) {
            pc[j + 1] = pc[j]; j--;
        }
        pc[j + 1] = v;
    }
    uint8_t w = 0;
    for (uint8_t i = from; i < n && w < cap; i++) {
        const NodeAcc& na = s_st->acc[a][pc[i].ai];
        const NodeAcc& nb = s_st->acc[b][pc[i].bi];
        Pair& p = out[w++];
        memset(&p, 0, sizeof p);
        p.proto = na.proto; p.id = na.id;
        // Whichever side named it. A run can hold a row whose only frame
        // arrived before the decoder set the tag.
        memcpy(p.tag, na.tag[0] ? na.tag : nb.tag, sizeof p.tag);
        p.framesA = na.frames; p.framesB = nb.frames;
        p.dRssi = pc[i].dRssi; p.dSnr4 = pc[i].dSnr4;
        p.enough = pc[i].enough;
    }
    return w;
}

// ---- the live trend --------------------------------------------------------

uint8_t trend(Proto p, uint64_t id, Sample* out, uint8_t cap) {
    if (!s_st || !out || !cap) return 0;
    const TrendRow* t = nullptr;
    for (uint8_t i = 0; i < TREND_NODES; i++)
        if (s_st->trend[i].used && s_st->trend[i].proto == p && s_st->trend[i].id == id) { t = &s_st->trend[i]; break; }
    if (!t) return 0;
    const uint8_t held = t->count < TREND_LEN ? (uint8_t)t->count : TREND_LEN;
    // Oldest first. With the ring not yet full the oldest is index 0; once it
    // has wrapped, head is both the next slot and the oldest reading.
    const uint8_t first = t->count < TREND_LEN ? 0 : t->head;
    uint8_t w = 0;
    // When the caller's buffer is smaller than what is held, the NEWEST
    // readings are the ones that fit: a trend line is about the last few
    // seconds of turning the antenna.
    const uint8_t skip = held > cap ? (uint8_t)(held - cap) : 0;
    for (uint8_t i = skip; i < held && w < cap; i++) out[w++] = t->s[(uint8_t)((first + i) % TREND_LEN)];
    return w;
}

uint8_t trendNodes() {
    if (!s_st) return 0;
    uint8_t n = 0;
    for (uint8_t i = 0; i < TREND_NODES; i++) if (s_st->trend[i].used) n++;
    return n;
}

// ---- saying it in words ----------------------------------------------------

const char* verdictText(Verdict v) {
    switch (v) {
        case V_LOPSIDED: return "UNEVEN RUNS";
        case V_THIN:     return "TOO THIN";
        case V_MIXED:    return "MIXED";
        case V_BETTER:   return "B BETTER";
        case V_WORSE:    return "B WORSE";
        case V_SAME:     return "NO CHANGE";
        default:         return "NO DATA";
    }
}

void verdictLine(const Compare& c, char* out, size_t cap) {
    if (!cap) return;
    // Every one of these says what the figure rests on, because the figure on
    // its own is the lie this feature exists to avoid. Traffic is sporadic: a
    // forty-second run can hold three frames, and three frames from one node
    // is a reading, not a measurement.
    switch (c.verdict) {
        case V_NO_DATA:
            snprintf(out, cap, "no node heard %u+ times in both runs (%u gained, %u lost) -- nothing to compare",
                     (unsigned)EVIDENCE_MIN, (unsigned)c.gained, (unsigned)c.lost);
            break;
        case V_MIXED:
            if (c.incomparable)
                snprintf(out, cap, "one run hopped profiles and the other was parked: not comparable. Park with LORA FOCUS and redo");
            else if ((c.gained > c.lost && c.medDRssi < 0) || (c.lost > c.gained && c.medDRssi > 0))
                snprintf(out, cap, "nodes and dB disagree: %+d dB median over %u nodes, but %u gained and %u lost",
                         (int)c.medDRssi, (unsigned)c.paired, (unsigned)c.gained, (unsigned)c.lost);
            else
                snprintf(out, cap, "%u of %u nodes better, %u worse (a coin does this %u.%u%% of the time): no result",
                         (unsigned)c.better, (unsigned)c.paired, (unsigned)c.worse,
                         (unsigned)(c.signPermille / 10), (unsigned)(c.signPermille % 10));
            break;
        case V_LOPSIDED:
            snprintf(out, cap, "the two runs hold very different evidence: %u nodes paired, but run the shorter one again before believing %+d dB",
                     (unsigned)c.paired, (int)c.medDRssi);
            break;
        case V_THIN:
            snprintf(out, cap, "%u node%s in common: %+d dB is that node's luck as much as the antenna's. Listen longer",
                     (unsigned)c.paired, c.paired == 1 ? "" : "s", (int)c.medDRssi);
            break;
        default: {
            // snr4 is quarter-dB, and C++ truncates toward zero: -3 quarters
            // would print as "0.75" with the minus lost. The sign is taken off
            // first and the magnitude formatted on its own.
            const int      q   = c.medDSnr4;
            const char     sgn = q < 0 ? '-' : '+';
            const unsigned mag = (unsigned)(q < 0 ? -q : q);
            snprintf(out, cap, "%+d dB median over %u node%s (spread %+d to %+d), snr %c%u.%02u dB, %u gained %u lost",
                     (int)c.medDRssi, (unsigned)c.paired, c.paired == 1 ? "" : "s",
                     (int)c.spreadLo, (int)c.spreadHi,
                     sgn, mag / 4, (mag % 4) * 25,
                     (unsigned)c.gained, (unsigned)c.lost);
            break;
        }
    }
    out[cap - 1] = '\0';
}

}
}
