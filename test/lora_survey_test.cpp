// The antenna survey's arithmetic -- src/lora_survey.cpp -- on a desktop.
//
// This is the feature, and it is the arithmetic that IS the feature: if the
// median is wrong, or the paired delta is differenced the wrong way round, or a
// run with one frame in it is allowed to produce a verdict, then nothing
// crashes. The screen prints a confident number and somebody spends an
// afternoon bolting up the worse antenna. So every figure the comparison can
// print is pinned here, including the ones whose job is to say "this is not a
// measurement".
//
// No radio and no node table: noteDirect() takes the two numbers a reading is,
// which is also the whole contract lora_nodes.cpp calls it on.
#include "lora_survey.h"
#include "test_util.h"
#include <cstdlib>
#include <cstring>
#include <cstdio>

using namespace Lora;

// Two stations. The ids are arbitrary; what matters is that the same id in two
// runs is the same station, which is the entire basis of the measurement.
static const uint64_t ALPHA = 0x1111111111111111ull;
static const uint64_t BRAVO = 0x2222222222222222ull;
static const uint64_t CHARLIE = 0x3333333333333333ull;

static void feed(uint64_t id, const char* tag, const int16_t* rssi, int n, int8_t snr4, uint32_t t0) {
    for (int i = 0; i < n; i++)
        Survey::noteDirect(Proto::MESHCORE, id, tag, rssi[i], snr4, t0 + (uint32_t)i * 1000);
}

static bool statsOf(uint8_t run, uint64_t id, Survey::NodeStats& out) {
    return Survey::runNode(run, Proto::MESHCORE, id, out);
}

int main() {
    void* mem = malloc(Survey::bytesNeeded());
    printf("store: %u bytes (%u kB) -- %u runs x %u nodes, plus %u trend rows of %u readings\n",
           (unsigned)Survey::bytesNeeded(), (unsigned)(Survey::bytesNeeded() / 1024),
           (unsigned)Survey::RUNS, (unsigned)Survey::RUN_NODES,
           (unsigned)Survey::TREND_NODES, (unsigned)Survey::TREND_LEN);

    suite("Nothing is recorded before a store is handed over");
    {
        ck("not ready", !Survey::ready());
        Survey::noteDirect(Proto::MESHCORE, ALPHA, "a", -80, 40, 1000);
        ck("a reading before begin() is dropped, not crashed", Survey::runCount() == 0);
        ck("and start() refuses", Survey::start("x", 0, false, 1000) < 0);
        ck("begin() refuses a block one byte short", !Survey::begin(mem, Survey::bytesNeeded() - 1));
        ck("and takes an exact one", Survey::begin(mem, Survey::bytesNeeded()));
        ck("now ready", Survey::ready());
    }

    suite("The median: the middle reading, and rounded DOWN on an even count");
    {
        Survey::clear();
        ck("a run starts at slot 0", Survey::start("odd", 3, false, 1000) == 0);
        // Deliberately out of order and with the extremes far out, which is what
        // a handheld being turned produces: one burst at a bad bearing.
        const int16_t r[] = { -85, -120, -80, -90, -60 };
        feed(ALPHA, "alpha", r, 5, 40, 1000);
        Survey::NodeStats s;
        ck("the row is there", statsOf(0, ALPHA, s));
        ck("five frames", s.frames == 5);
        ck("min is the weakest reading", s.rssiMin == -120);
        ck("max is the strongest", s.rssiMax == -60);
        // Sorted: -120 -90 -85 -80 -60. The middle is -85. A MEAN would be -87,
        // dragged three decibels by the one reading at -120 -- which is exactly
        // the burst at a different bearing the median exists to ignore.
        ck("median is -85, not the mean of -87", s.rssiMed == -85);
        ck("nothing out of range", s.outOfRange == 0);
        ck("five frames is enough for a figure", s.enough);
    }
    {
        Survey::start("even", 3, false, 20000);
        const int16_t r[] = { -90, -86, -85, -80 };
        feed(BRAVO, "bravo", r, 4, 40, 20000);
        Survey::NodeStats s;
        statsOf(1, BRAVO, s);
        // The two middle readings are -86 and -85. Their average is -85.5, and
        // the rule rounds toward the WEAKER signal so that an even number of
        // frames never flatters an antenna over an odd one.
        ck("even count rounds down to -86, not up to -85", s.rssiMed == -86);
    }

    suite("SNR is kept at the resolution the networks carry");
    {
        Survey::clear();
        Survey::start("snr", 3, false, 1000);
        // snr4 is SNR x 4. 17, 18, 19 quarters = 4.25, 4.50, 4.75 dB: a whole-dB
        // histogram would call all three "4" and lose the difference an antenna
        // makes.
        Survey::noteDirect(Proto::MESHCORE, ALPHA, "a", -80, 17, 1000);
        Survey::noteDirect(Proto::MESHCORE, ALPHA, "a", -80, 18, 2000);
        Survey::noteDirect(Proto::MESHCORE, ALPHA, "a", -80, 19, 3000);
        Survey::NodeStats s;
        statsOf(0, ALPHA, s);
        ck("snr min is 4.25 dB", s.snrMin4 == 17);
        ck("snr median is 4.50 dB", s.snrMed4 == 18);
        ck("snr max is 4.75 dB", s.snrMax4 == 19);
        // The full int8 range is binned, so the extremes of the field cannot
        // fall outside the histogram the way an RSSI could.
        Survey::noteDirect(Proto::MESHCORE, BRAVO, "b", -80, -128, 4000);
        Survey::noteDirect(Proto::MESHCORE, BRAVO, "b", -80, 127, 5000);
        Survey::noteDirect(Proto::MESHCORE, BRAVO, "b", -80, 0, 6000);
        statsOf(0, BRAVO, s);
        ck("the whole int8 range of snr4 is binned", s.snrMin4 == -128 && s.snrMax4 == 127 && s.snrMed4 == 0);
    }

    suite("A reading no receiver can produce is counted, not swallowed");
    {
        Survey::clear();
        Survey::start("bogus", 3, false, 1000);
        const int16_t r[] = { -90, -88, -86 };
        feed(ALPHA, "a", r, 3, 40, 1000);
        // 0 dBm at the antenna port is a destroyed receiver, not a strong
        // signal. If it landed in the top bin it would drag the median; counted
        // separately, the median stays over the readings that were real and the
        // row says a driver fault happened.
        Survey::noteDirect(Proto::MESHCORE, ALPHA, "a", 0, 40, 5000);
        Survey::NodeStats s;
        statsOf(0, ALPHA, s);
        ck("the frame is counted", s.frames == 4);
        ck("and flagged out of range", s.outOfRange == 1);
        ck("the median is over the three real readings", s.rssiMed == -88);
        ck("but min/max still hold what arrived", s.rssiMax == 0);
    }

    suite("The paired delta: the same node, differenced B minus A");
    {
        Survey::clear();
        Survey::start("A rubber duck", 3, false, 1000);
        const int16_t a1[] = { -100, -98, -96 };            // median -98
        const int16_t a2[] = { -90, -88, -86 };             // median -88
        const int16_t a3[] = { -70, -69, -68 };             // median -69
        feed(ALPHA, "alpha", a1, 3, 20, 1000);
        feed(BRAVO, "bravo", a2, 3, 20, 2000);
        feed(CHARLIE, "charlie", a3, 3, 20, 3000);
        Survey::stop(40000);

        Survey::start("B dipole", 3, false, 50000);
        const int16_t b1[] = { -95, -93, -91 };             // median -93: +5
        const int16_t b2[] = { -86, -84, -82 };             // median -84: +4
        const int16_t b3[] = { -68, -67, -66 };             // median -67: +2
        feed(ALPHA, "alpha", b1, 3, 28, 50000);
        feed(BRAVO, "bravo", b2, 3, 28, 51000);
        feed(CHARLIE, "charlie", b3, 3, 28, 52000);
        Survey::stop(90000);

        Survey::Compare c;
        ck("the pair compares", Survey::compare(0, 1, c));
        ck("three nodes paired", c.paired == 3);
        ck("none gained or lost", c.gained == 0 && c.lost == 0);
        ck("all three improved", c.better == 3 && c.worse == 0);
        // The deltas are +5, +4, +2. Their median is +4 -- and note that the
        // ABSOLUTE readings span -98 to -66, which is the whole reason the
        // median is taken over the deltas and not over the readings.
        ck("median paired delta is +4 dB", c.medDRssi == 4);
        ck("the scatter behind it is +2 to +5", c.spreadLo == 2 && c.spreadHi == 5);
        ck("snr moved +2.00 dB", c.medDSnr4 == 8);
        // n = 3, all one way: 2 * C(3,0) / 2^3 = 2/8 = 250 per-mille exactly,
        // which is the rule's own ceiling -- three nodes agreeing is the
        // thinnest evidence that can still be called a result.
        ck("sign test is exactly 250 per-mille", c.signPermille == 250);
        ck("verdict: B is better", c.verdict == Survey::V_BETTER);
        char words[200];
        Survey::verdictLine(c, words, sizeof words);
        printf("  verdict line: %s\n", words);
        ck("the line carries the node count and the spread",
           strstr(words, "3 nodes") && strstr(words, "spread"));

        // The other direction, which must be the exact negative: a delta
        // computed with the operands swapped is the classic way a comparison
        // tells you the wrong antenna is better.
        Survey::Compare r;
        Survey::compare(1, 0, r);
        ck("A against B is the negative of B against A", r.medDRssi == -4);
        ck("and the verdict flips", r.verdict == Survey::V_WORSE);
        ck("with the scatter mirrored", r.spreadLo == -5 && r.spreadHi == -2);

        // The rows behind it, biggest improvement first.
        Survey::Pair pr[4];
        const uint8_t np = Survey::pairs(0, 1, pr, 4);
        ck("three rows", np == 3);
        ck("biggest improvement first", np == 3 && pr[0].dRssi == 5 && pr[1].dRssi == 4 && pr[2].dRssi == 2);
        ck("each row carries its own evidence on both sides",
           np == 3 && pr[0].framesA == 3 && pr[0].framesB == 3 && pr[0].enough);
        ck("and the tag the node gave", np == 3 && strcmp(pr[0].tag, "alpha") == 0);
    }

    suite("A node in one run and not the other is gained or lost, never a delta");
    {
        Survey::clear();
        Survey::start("A", 3, false, 1000);
        const int16_t a[] = { -90, -89, -88 };
        feed(ALPHA, "alpha", a, 3, 20, 1000);
        feed(BRAVO, "bravo", a, 3, 20, 2000);
        Survey::stop(40000);

        Survey::start("B", 3, false, 50000);
        feed(ALPHA, "alpha", a, 3, 20, 50000);       // unchanged: 0 dB
        feed(CHARLIE, "charlie", a, 3, 20, 51000);   // only B hears it
        Survey::stop(90000);

        Survey::Compare c;
        Survey::compare(0, 1, c);
        ck("one node paired", c.paired == 1);
        ck("charlie is gained", c.gained == 1);
        ck("bravo is lost", c.lost == 1);
        ck("the gained node has no delta to average in", c.medDRssi == 0);
        // One node in common cannot tell an antenna from a passing van.
        ck("verdict: too thin", c.verdict == Survey::V_THIN);
        char words[200];
        Survey::verdictLine(c, words, sizeof words);
        printf("  verdict line: %s\n", words);
        ck("and it says so in words", strstr(words, "luck") != nullptr);
    }

    suite("A run with one frame from one node is not a measurement");
    {
        Survey::clear();
        Survey::start("A", 3, false, 1000);
        Survey::noteDirect(Proto::MESHCORE, ALPHA, "alpha", -90, 20, 1000);
        Survey::stop(40000);
        Survey::NodeStats s;
        statsOf(0, ALPHA, s);
        ck("one frame", s.frames == 1);
        // A single reading IS the min, the median and the max. All three are
        // that reading, and `enough` is what stops a view printing them as a
        // measured spread.
        ck("min = median = max = the one reading", s.rssiMin == -90 && s.rssiMed == -90 && s.rssiMax == -90);
        ck("and it is not enough", !s.enough);

        Survey::start("B", 3, false, 50000);
        Survey::noteDirect(Proto::MESHCORE, ALPHA, "alpha", -70, 20, 50000);
        Survey::stop(90000);
        Survey::Compare c;
        Survey::compare(0, 1, c);
        // Twenty decibels between one frame and one frame. It would be the most
        // dramatic result the screen could print and it means nothing.
        ck("nothing is paired", c.paired == 0);
        ck("the node is counted as thin, not as a pair", c.thin == 1);
        ck("nothing is gained or lost either", c.gained == 0 && c.lost == 0);
        ck("no 20 dB verdict", c.verdict == Survey::V_NO_DATA && c.medDRssi == 0);
        char words[200];
        Survey::verdictLine(c, words, sizeof words);
        printf("  verdict line: %s\n", words);
        ck("and the words say what is missing", strstr(words, "nothing to compare") != nullptr);
    }

    suite("Two runs with very different evidence say so");
    {
        Survey::clear();
        Survey::start("A quick look", 3, false, 1000);
        const int16_t few[] = { -90, -89, -88 };
        feed(ALPHA, "alpha", few, 3, 20, 1000);
        feed(BRAVO, "bravo", few, 3, 20, 2000);
        feed(CHARLIE, "charlie", few, 3, 20, 3000);
        Survey::stop(11000);                     // 10 s, 9 frames

        Survey::start("B patient", 3, false, 20000);
        const int16_t many[] = { -88, -87, -86, -85, -84, -83, -82, -81 };
        feed(ALPHA, "alpha", many, 8, 20, 20000);
        feed(BRAVO, "bravo", many, 8, 20, 30000);
        feed(CHARLIE, "charlie", many, 8, 20, 40000);
        Survey::stop(200000);                    // 180 s, 24 frames

        Survey::Compare c;
        Survey::compare(0, 1, c);
        ck("three nodes still paired", c.paired == 3);
        ck("the run lengths are flagged as uneven", c.lopsided);
        // The dB figure is still computed and still printed -- it is the only
        // thing there is -- but the verdict refuses to call it a result.
        ck("a figure is still there", c.medDRssi > 0);
        ck("verdict: uneven runs", c.verdict == Survey::V_LOPSIDED);
        char words[200];
        Survey::verdictLine(c, words, sizeof words);
        printf("  verdict line: %s\n", words);
        ck("and it says to run the short one again", strstr(words, "again") != nullptr);
    }

    suite("A hopping run against a parked one is refused");
    {
        Survey::clear();
        const int16_t r[] = { -90, -89, -88 };
        Survey::start("parked", 3, false, 1000);
        feed(ALPHA, "a", r, 3, 20, 1000);
        feed(BRAVO, "b", r, 3, 20, 2000);
        feed(CHARLIE, "c", r, 3, 20, 3000);
        Survey::stop(40000);
        Survey::start("hopping", 3, true, 50000);
        feed(ALPHA, "a", r, 3, 20, 50000);
        feed(BRAVO, "b", r, 3, 20, 51000);
        feed(CHARLIE, "c", r, 3, 20, 52000);
        Survey::stop(90000);
        Survey::Compare c;
        Survey::compare(0, 1, c);
        ck("the runs are marked incomparable", c.incomparable);
        ck("verdict: mixed", c.verdict == Survey::V_MIXED);
        char words[200];
        Survey::verdictLine(c, words, sizeof words);
        printf("  verdict line: %s\n", words);
        ck("and it names the fix", strstr(words, "LORA FOCUS") != nullptr);
    }

    suite("Nodes that disagree get no verdict");
    {
        Survey::clear();
        Survey::start("A", 3, false, 1000);
        const int16_t base[] = { -90, -89, -88 };       // median -89
        feed(ALPHA, "a", base, 3, 20, 1000);
        feed(BRAVO, "b", base, 3, 20, 2000);
        feed(CHARLIE, "c", base, 3, 20, 3000);
        Survey::stop(40000);
        Survey::start("B", 3, false, 50000);
        const int16_t up[]   = { -85, -84, -83 };       // -84: +5
        const int16_t down[] = { -95, -94, -93 };       // -94: -5
        feed(ALPHA, "a", up, 3, 20, 50000);
        feed(BRAVO, "b", down, 3, 20, 51000);
        feed(CHARLIE, "c", down, 3, 20, 52000);
        Survey::stop(90000);
        Survey::Compare c;
        Survey::compare(0, 1, c);
        ck("three paired, one up two down", c.paired == 3 && c.better == 1 && c.worse == 2);
        // n = 3 split 1/2: 2 * (C(3,0) + C(3,1)) / 8 = 8/8 = 1000 per-mille. A
        // coin does this every time, so there is no result here.
        ck("a coin does this every time", c.signPermille == 1000);
        ck("verdict: mixed", c.verdict == Survey::V_MIXED);
        char words[200];
        Survey::verdictLine(c, words, sizeof words);
        printf("  verdict line: %s\n", words);
    }

    suite("The exact two-sided sign test, through the verdict");
    {
        // Built by hand rather than derived, so the table is a check on the code
        // and not a restatement of it:
        //   5-0: 2*C(5,0)/32          = 2/32      = 62 per-mille (floor)
        //   4-1: 2*(C(5,0)+C(5,1))/32 = 12/32     = 375
        //   8-0: 2*C(8,0)/256         = 2/256     = 7
        struct { int up, down; uint16_t pm; } want[] = { {5,0,62}, {4,1,375}, {8,0,7} };
        const int16_t base[] = { -90, -89, -88 };
        const int16_t up[]   = { -80, -79, -78 };
        const int16_t down[] = { -99, -98, -97 };
        for (unsigned w = 0; w < sizeof want / sizeof want[0]; w++) {
            Survey::clear();
            const int n = want[w].up + want[w].down;
            Survey::start("A", 3, false, 1000);
            for (int i = 0; i < n; i++) feed((uint64_t)(0x100 + i), "n", base, 3, 20, 1000);
            Survey::stop(40000);
            Survey::start("B", 3, false, 50000);
            for (int i = 0; i < n; i++)
                feed((uint64_t)(0x100 + i), "n", i < want[w].up ? up : down, 3, 20, 50000);
            Survey::stop(90000);
            Survey::Compare c;
            Survey::compare(0, 1, c);
            char what[64];
            snprintf(what, sizeof what, "%d better %d worse -> %u per-mille", want[w].up, want[w].down, want[w].pm);
            ck(what, c.paired == (uint16_t)n && c.better == (uint16_t)want[w].up && c.signPermille == want[w].pm);
        }
    }

    suite("A node gained is worth more than a decibel, and is never traded for one");
    {
        Survey::clear();
        const int16_t base[] = { -90, -89, -88 };
        const int16_t dip[]  = { -91, -90, -89 };     // -90: -1 dB on every paired node
        Survey::start("A", 3, false, 1000);
        for (int i = 0; i < 4; i++) feed((uint64_t)(0x200 + i), "n", base, 3, 20, 1000);
        Survey::stop(40000);
        Survey::start("B", 3, false, 50000);
        for (int i = 0; i < 4; i++) feed((uint64_t)(0x200 + i), "n", dip, 3, 20, 50000);
        feed(0x2FF, "extra", base, 3, 20, 60000);     // and one nobody heard before
        Survey::stop(90000);
        Survey::Compare c;
        Survey::compare(0, 1, c);
        ck("four paired, one gained", c.paired == 4 && c.gained == 1 && c.lost == 0);
        ck("every paired node lost a decibel", c.medDRssi == -1 && c.worse == 4);
        // There is no honest exchange rate between "one more neighbour" and
        // "n dB", so the verdict refuses to invent one: it says the two numbers
        // disagree and leaves the decision where it belongs.
        ck("verdict: mixed, not a confident WORSE", c.verdict == Survey::V_MIXED);
        char words[200];
        Survey::verdictLine(c, words, sizeof words);
        printf("  verdict line: %s\n", words);
        ck("and the line names both numbers", strstr(words, "gained") != nullptr);
    }

    suite("A run's rows come out loudest first, with the thin ones last");
    {
        Survey::clear();
        Survey::start("order", 3, false, 1000);
        const int16_t quiet[] = { -110, -109, -108 };
        const int16_t loud[]  = { -60, -59, -58 };
        const int16_t mid[]   = { -85, -84, -83 };
        feed(ALPHA, "quiet", quiet, 3, 20, 1000);
        feed(BRAVO, "loud", loud, 3, 20, 2000);
        feed(CHARLIE, "mid", mid, 3, 20, 3000);
        Survey::noteDirect(Proto::MESHCORE, 0x999, "thin", -50, 20, 4000);   // loudest, one frame
        Survey::NodeStats rows[8];
        const uint8_t n = Survey::runNodes(0, rows, 8);
        ck("four rows", n == 4);
        ck("loudest median first", n == 4 && strcmp(rows[0].tag, "loud") == 0);
        ck("then mid, then quiet", n == 4 && strcmp(rows[1].tag, "mid") == 0 && strcmp(rows[2].tag, "quiet") == 0);
        // -50 dBm is the strongest reading in the run and it sorts LAST,
        // because one frame is not a median and a view reading down the list
        // must not meet it first.
        ck("the one-frame row sorts last despite being loudest",
           n == 4 && strcmp(rows[3].tag, "thin") == 0 && !rows[3].enough);
        // Paged the way nodeSnapshot is.
        Survey::NodeStats page[2];
        ck("a page of two from the top", Survey::runNodes(0, page, 2, 0) == 2 && strcmp(page[0].tag, "loud") == 0);
        ck("and the next page continues", Survey::runNodes(0, page, 2, 2) == 2 && strcmp(page[0].tag, "quiet") == 0);
    }

    suite("The trend ring: the last readings, oldest first");
    {
        Survey::clear();
        // No run recording: the trend is fed regardless, because it is what the
        // owner watches while turning the antenna before deciding to record.
        ck("nothing is recording", Survey::recording() < 0);
        for (int i = 0; i < 6; i++)
            Survey::noteDirect(Proto::MESHCORE, ALPHA, "a", (int16_t)(-100 + i), 20, (uint32_t)(1000 + i * 500));
        Survey::Sample s[Survey::TREND_LEN];
        uint8_t n = Survey::trend(Proto::MESHCORE, ALPHA, s, Survey::TREND_LEN);
        ck("six readings", n == 6);
        ck("oldest first", n == 6 && s[0].rssi == -100 && s[5].rssi == -95);
        ck("each carries its own timestamp, so a view can break the line",
           n == 6 && s[0].ms == 1000 && s[5].ms == 3500);
        ck("no run was needed", Survey::runCount() == 0);

        // Past the end of the ring: the newest TREND_LEN survive.
        for (int i = 6; i < Survey::TREND_LEN + 10; i++)
            Survey::noteDirect(Proto::MESHCORE, ALPHA, "a", (int16_t)(-100 + i), 20, (uint32_t)(1000 + i * 500));
        n = Survey::trend(Proto::MESHCORE, ALPHA, s, Survey::TREND_LEN);
        ck("the ring holds exactly TREND_LEN", n == Survey::TREND_LEN);
        ck("and they are the newest, still oldest-first",
           n == Survey::TREND_LEN && s[0].rssi == (int16_t)(-100 + 10) && s[n - 1].rssi == (int16_t)(-100 + Survey::TREND_LEN + 9));
        // A caller with a narrower buffer than the ring gets the NEWEST, because
        // a trend line is about the last few seconds.
        Survey::Sample five[5];
        ck("a short buffer gets the newest five",
           Survey::trend(Proto::MESHCORE, ALPHA, five, 5) == 5 &&
           five[4].rssi == (int16_t)(-100 + Survey::TREND_LEN + 9));
        ck("an unheard node has no trend", Survey::trend(Proto::MESHCORE, 0xDEAD, s, Survey::TREND_LEN) == 0);
    }

    suite("The run slots: four held, and the oldest finished one gives way");
    {
        Survey::clear();
        const int16_t r[] = { -90, -89, -88 };
        for (uint8_t i = 0; i < Survey::RUNS; i++) {
            char lab[16]; snprintf(lab, sizeof lab, "run%u", (unsigned)i);
            Survey::start(lab, 3, false, (uint32_t)(1000 + i * 10000));
            feed((uint64_t)(0x300 + i), "n", r, 3, 20, (uint32_t)(1000 + i * 10000));
            Survey::stop((uint32_t)(5000 + i * 10000));
        }
        ck("four runs held", Survey::runCount() == Survey::RUNS);
        Survey::Run got;
        ck("slot 0 is the oldest", Survey::run(0, got) && strcmp(got.label, "run0") == 0);
        const int8_t fifth = Survey::start("run4", 3, false, 100000);
        ck("the fifth start reuses slot 0", fifth == 0);
        ck("and slot 0 is now the new run with none of the old rows",
           Survey::run(0, got) && strcmp(got.label, "run4") == 0 && got.nodes == 0 && got.frames == 0);
        ck("the three runs that were not oldest are untouched",
           Survey::run(1, got) && strcmp(got.label, "run1") == 0);
        // Starting a run while one is recording stops the first rather than
        // losing it: a forgotten STOP should not throw away a run.
        ck("the new run is recording", Survey::recording() == 0);
        Survey::start("run5", 3, false, 110000);
        ck("the previous one was stopped, not dropped", Survey::run(0, got) && got.stopMs != 0);
    }

    suite("Dropping ONE run: the slot empties where it is and the rest do not move");
    {
        Survey::clear();
        const int16_t r[] = { -90, -89, -88 };
        for (uint8_t i = 0; i < Survey::RUNS; i++) {
            char lab[16]; snprintf(lab, sizeof lab, "run%u", (unsigned)i);
            Survey::start(lab, 3, false, (uint32_t)(1000 + i * 10000));
            feed((uint64_t)(0x500 + i), "n", r, 3, 20, (uint32_t)(1000 + i * 10000));
            Survey::stop((uint32_t)(5000 + i * 10000));
        }
        ck("four runs held", Survey::runCount() == Survey::RUNS);
        // The bench case this exists for: run 2 was mis-started, holds two
        // frames, and sits in the middle of every comparison that follows.
        ck("dropping run 2 says it was there", Survey::clearRun(1));
        ck("and it is gone", Survey::runCount() == (uint8_t)(Survey::RUNS - 1));
        Survey::Run got;
        ck("the slot reads empty", !Survey::run(1, got));
        // THE WHOLE POINT: 0, 2 and 3 are still 0, 2 and 3. A reader with "2 vs
        // 4" written on the back of their hand still has it.
        ck("slot 0 did not move", Survey::run(0, got) && strcmp(got.label, "run0") == 0);
        ck("slot 2 did not move", Survey::run(2, got) && strcmp(got.label, "run2") == 0);
        ck("slot 3 did not move", Survey::run(3, got) && strcmp(got.label, "run3") == 0);
        Survey::Compare c;
        ck("and 3 against 4 still compares, by those numbers",
           Survey::compare(2, 3, c) && c.a == 2 && c.b == 3);
        ck("dropping the empty slot says nothing was there", !Survey::clearRun(1));
        ck("a slot past the end is refused", !Survey::clearRun(Survey::RUNS));
        ck("and neither took anything with it", Survey::runCount() == (uint8_t)(Survey::RUNS - 1));
        // start() takes the first FREE slot, so the hole is what gets used next
        // -- the owner who mis-started run 2 gets run 2 back, not run 5.
        ck("the next start takes the freed slot", Survey::start("again", 3, false, 200000) == 1);
        ck("and nothing else was disturbed by that either",
           Survey::run(2, got) && strcmp(got.label, "run2") == 0);
    }

    suite("Dropping the RECORDING run aborts it; the live trend belongs to no run");
    {
        Survey::clear();
        const int16_t r[] = { -90, -89, -88 };
        Survey::start("keep", 3, false, 1000);
        feed(ALPHA, "a", r, 3, 20, 1000);
        Survey::stop(5000);
        Survey::start("oops", 3, false, 10000);
        feed(BRAVO, "b", r, 3, 20, 10000);
        ck("slot 1 is the one recording", Survey::recording() == 1);
        ck("dropping it succeeds", Survey::clearRun(1));
        ck("nothing is recording afterwards", Survey::recording() == -1);
        ck("and the finished run is still there", Survey::runCount() == 1);
        Survey::Run got;
        ck("...under its own number", Survey::run(0, got) && strcmp(got.label, "keep") == 0);
        // The radio task keeps feeding readings whatever the screen just did, and
        // a frame arriving one tick later must not resurrect the slot.
        Survey::noteDirect(Proto::MESHCORE, BRAVO, "b", -90, 20, 11000);
        ck("a reading after the abort lands in no run",
           Survey::runCount() == 1 && !Survey::run(1, got));
        // The trend is a ring per NODE, fed whether or not anything records, and
        // it is the line the owner watches while turning the antenna. Dropping a
        // run must not blank it; clear() is the one that takes it.
        Survey::Sample s[Survey::TREND_LEN];
        ck("the dropped run's station keeps its trend",
           Survey::trend(Proto::MESHCORE, BRAVO, s, (uint8_t)Survey::TREND_LEN) > 0);
        Survey::clear();
        ck("and clear() takes the trend too",
           Survey::trend(Proto::MESHCORE, BRAVO, s, (uint8_t)Survey::TREND_LEN) == 0);
    }

    suite("A run keeps the rows it has rather than evicting mid-window");
    {
        Survey::clear();
        Survey::start("crowd", 3, false, 1000);
        const int16_t r[] = { -90, -89, -88 };
        for (int i = 0; i < Survey::RUN_NODES + 5; i++)
            feed((uint64_t)(0x400 + i), "n", r, 3, 20, (uint32_t)(1000 + i * 100));
        Survey::Run got;
        Survey::run(0, got);
        ck("the table filled", got.nodes == Survey::RUN_NODES);
        // A row replaced halfway through would report a median over half a
        // window, which is a wrong number rather than a missing one.
        ck("and the frames it had no row for are counted", got.framesDropped == 5 * 3);
        Survey::NodeStats s;
        ck("the first node kept all its frames", statsOf(0, 0x400, s) && s.frames == 3);
        ck("and the 33rd was never taken", !statsOf(0, (uint64_t)(0x400 + Survey::RUN_NODES), s));
    }

    suite("A node heard exactly once has no tag, and the second frame names it");
    {
        Survey::clear();
        Survey::start("tag", 3, false, 1000);
        // Every decoder in lora_nodes.cpp sets Node::tag AFTER calling heard(),
        // so the first frame from a station offers an empty one.
        Survey::noteDirect(Proto::MESHCORE, ALPHA, "", -90, 20, 1000);
        Survey::NodeStats s;
        statsOf(0, ALPHA, s);
        ck("no tag yet", s.tag[0] == '\0');
        Survey::noteDirect(Proto::MESHCORE, ALPHA, "40 DL Rep", -89, 20, 2000);
        statsOf(0, ALPHA, s);
        ck("the second frame names it", strcmp(s.tag, "40 DL Rep") == 0);
        ck("and both frames are counted", s.frames == 2);
    }

    suite("Comparing a run with itself, or with one that does not exist");
    {
        Survey::clear();
        Survey::start("only", 3, false, 1000);
        const int16_t r[] = { -90, -89, -88 };
        feed(ALPHA, "a", r, 3, 20, 1000);
        Survey::stop(10000);
        Survey::Compare c;
        ck("a run against itself is refused", !Survey::compare(0, 0, c));
        ck("an empty slot is refused", !Survey::compare(0, 1, c));
        ck("a slot past the end is refused", !Survey::compare(0, Survey::RUNS, c));
        ck("and the refused Compare is zeroed, not stale", c.paired == 0 && c.verdict == Survey::V_NO_DATA);
    }

    // ---- swapping A and B must only negate the answer ------------------
    // Which run an owner calls the first one is arbitrary, so a swap has to
    // negate the verdict and its dB figure and change nothing else. It did not:
    // arrayMedian floored a half, which is the conservative choice for a LEVEL
    // and a directional one for a DIFFERENCE. A true median of +1.5 dB floored
    // to +1 and understated the gain; -1.5 dB floored to -2 and overstated the
    // loss. Over every delta set of length 3..6 drawn from -2..+2, 316 of
    // 19,500 changed the verdict WORD on a swap and 7,380 changed the
    // magnitude. Swept rather than sampled, because no hand-picked case would
    // have found 1.6 %.
    {
        int asym = 0, flipped = 0, checked = 0;
        for (uint16_t n = 3; n <= 6; n++) {
            int total = 1;
            for (uint16_t i = 0; i < n; i++) total *= 5;
            for (int code = 0; code < total; code++) {
                Survey::clear();
                // Run A: every node at a different absolute level, so nothing
                // can accidentally depend on the levels rather than the deltas.
                Survey::start("A", 3, false, 1000);
                int c = code;
                int16_t d[8];
                for (uint16_t i = 0; i < n; i++) { d[i] = (int16_t)((c % 5) - 2); c /= 5; }
                for (uint16_t i = 0; i < n; i++) {
                    const int16_t base = (int16_t)(-60 - 9 * (int)i);
                    const int16_t r[3] = { base, base, base };
                    feed(ALPHA + i, "n", r, 3, 20, 1000);
                }
                Survey::stop(10000);
                Survey::start("B", 3, false, 20000);
                for (uint16_t i = 0; i < n; i++) {
                    const int16_t base = (int16_t)(-60 - 9 * (int)i + d[i]);
                    const int16_t r[3] = { base, base, base };
                    feed(ALPHA + i, "n", r, 3, 20, 20000);
                }
                Survey::stop(30000);

                Survey::Compare ab, ba;
                if (!Survey::compare(0, 1, ab) || !Survey::compare(1, 0, ba)) continue;
                checked++;
                if (ab.medDRssi != -ba.medDRssi) asym++;
                const bool abUp = ab.verdict == Survey::V_BETTER, abDn = ab.verdict == Survey::V_WORSE;
                const bool baUp = ba.verdict == Survey::V_BETTER, baDn = ba.verdict == Survey::V_WORSE;
                if (abUp != baDn || abDn != baUp) flipped++;
            }
        }
        ck("the swept comparison ran", checked > 15000);
        ck("swapping A and B negates the dB figure, every time", asym == 0);
        ck("and never changes which way the verdict points", flipped == 0);
    }

    free(mem);
    return report();
}
