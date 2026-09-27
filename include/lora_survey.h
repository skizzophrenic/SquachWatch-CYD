// SquachWatch-CYD — the antenna survey: which antenna, and which spot, hears
// the neighbours best.
//
// NOT Lora::Mode::SURVEY. That is the radio's profile-walking scan and has
// nothing to do with this file; the collision is unfortunate and the console
// keeps them apart by words (bare `LORA SURVEY` still switches the radio mode,
// `LORA SURVEY START` records an antenna run). Mode::SURVEY is in fact the
// mode a survey run must NOT be taken in -- see Run::hopping below.
//
// WHAT THIS MEASURES, AND THE TRAP IT IS BUILT AROUND
//
// An absolute RSSI is not comparable between two nodes. A reading of -60 dBm
// off one station and -95 dBm off another says nothing about the antenna in
// your hand: it carries the other station's transmit power, its antenna, its
// height and its distance, none of which you control and none of which you
// can see. So a survey that ranks antennas by "mean RSSI over everything it
// heard" is not measuring the antenna. It is measuring which stations happened
// to transmit during the run -- and since traffic is sporadic, that changes
// between any two runs whatever is screwed onto the connector.
//
// The measurement is THE SAME NODE COMPARED AGAINST ITSELF ACROSS TWO RUNS.
// Everything in the node's half of the link is the same in both, so the
// difference is the half you changed. That is why the central output of this
// file is not a number per run but a PAIRED DELTA per node, and a verdict over
// the paired set. A run on its own is raw material; two runs are a
// measurement. The comparison is the feature.
//
// DIRECT ONLY, AND WHY THE RELAYED COPIES STILL MATTER
//
// Only frames the protocol establishes we received from the node's OWN
// transmitter are counted. include/lora_nodes.h has the per-network rules and
// the reason they exist: a relayed copy measures the last repeater's link to
// here, so crediting it to an eleven-hop originator invents a signal strength
// for a station on the other side of the country. A MeshCore relay row is an
// exception that is not an exception -- that relay IS the transmitter we heard,
// so its own row takes the reading.
//
// This is also what makes the screen useful rather than crowded: the direct
// filter leaves the handful of stations actually around you, which is exactly
// the set an antenna comparison is about. A relayed packet is not thrown away;
// it lands on the relay's own row and measures the link to the relay, which is
// a neighbour too.
//
// WHERE THE MEMORY IS
//
// Everything here lives in one block the caller hands over, because the runs
// are the largest thing this firmware keeps outside the frame buffer and
// contiguous internal RAM on the CrowPanel 7 measures about 57 kB. The caller
// allocates from PSRAM (Lora::begin does), asks bytesNeeded() how much, and
// calls begin(). Until begin() succeeds every note() is a no-op and every read
// is empty, which is also how every board without PSRAM and without a radio
// pays nothing for this file being compiled.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "lora_pkt.h"

namespace Lora {
namespace Survey {

// ---- sizes, and what they cost ---------------------------------------------

// WHAT THE WHOLE STORE COSTS: 130,224 bytes, measured by
// test/lora_survey_test.cpp printing bytesNeeded() -- 127 kB of the CrowPanel
// 7's 7.6 MB of PSRAM, nearly all of it the two histograms per node per run.
// The packet ring next door is 256 records of 288 bytes, 72 kB, for scale.
//
// Four runs. Two is the minimum the comparison needs; four is what a bench
// session actually wants -- three antennas against a reference, or one antenna
// at four positions -- without stopping to copy numbers onto paper between
// pairs. The fifth start reuses the oldest finished run: see start().
static const uint8_t RUNS = 4;

// Rows per run. Direct reception at one spot is a handful of stations -- that
// is the whole premise of the feature -- and a location that hears 32 of them
// first-hand is not a location where telling two antennas apart is hard. When
// the table is full the run keeps the rows it has and counts the frames it could
// not place (Run::framesDropped), rather than evicting a row mid-run and
// reporting a median over half a window.
static const uint8_t RUN_NODES = 32;

// The live trend: nodes, and readings per node. 64 readings because that is
// what the screen can draw -- the logical canvas is 400x240 (SQW_LOGICAL_W in
// include/crowpanel7_board.h) and a list row there is fontHeight()+6 = 14 px
// tall, so a row that has spent its left half on a tag, an RSSI and a count
// has something like 128 px left: 64 readings at 2 px each. A single node
// given the whole width gets 64 at 5 px. Both are a line; 256 readings would
// be four pixels per sample thrown away.
static const uint8_t TREND_NODES = 32;
static const uint8_t TREND_LEN   = 64;

// The RSSI histogram's bounds, in dBm. Packet::rssi is an integer number of
// dBm (src/lora_radio.cpp rounds RadioLib's float), so 1 dB bins lose nothing
// the record carried and the median below is exact rather than approximate.
//
// The bounds are engineering bounds and a reading outside them is a driver
// fault rather than a measurement, which is why NodeStats::outOfRange counts
// them instead of the end bins swallowing them: thermal noise in the narrowest
// profile in the table (62.5 kHz) is -174 + 10*log10(62500) = -126 dBm, so
// -160 is 34 dB below anything the receiver's own front end can produce, and
// 0 dBm at the antenna port is a destroyed receiver, not a strong signal.
static const int16_t  RSSI_LO   = -160;
static const int16_t  RSSI_HI   = -1;
static const uint16_t RSSI_BINS = (uint16_t)(RSSI_HI - RSSI_LO + 1);   // 160

// The SNR histogram. Packet::snr4 is SNR x 4 in an int8_t, so 256 bins of one
// quarter-dB cover the ENTIRE range the field can hold: no clipping is
// possible, and the SNR median is exact at the resolution the networks
// themselves carry. 512 bytes a row, which PSRAM does not notice.
static const uint16_t SNR_BINS = 256;

// Frames from one node in one run before its median is a figure at all.
//
// Three, and the reason is arithmetic rather than a measurement: at two
// readings a "median" is the average of the two extremes, so a single frame
// caught at a bad bearing moves it by half its own error. Three is the
// smallest count at which the median is a middle reading that an outlier
// cannot drag. It is still a thin measurement and the comparison says so --
// this is a floor under "not a measurement at all", not a claim of accuracy.
static const uint16_t EVIDENCE_MIN = 3;

// ---- what a reading is -----------------------------------------------------

// One direct reception, for the trend line. The timestamp travels with it
// because traffic is sporadic and a view that draws 64 readings evenly spaced
// would draw a straight walk out of a five-minute gap; with the ms a view can
// break the line where nothing was heard.
struct Sample {
    uint32_t ms;
    int16_t  rssi;      // dBm
    int8_t   snr4;      // SNR x 4
    uint8_t  reserved;  // keeps the struct at 8 bytes, so the ring is a clean 512
};

// ---- a run -----------------------------------------------------------------

struct Run {
    // What is being tested. The owner sets it: "dipole 2m", "balcony rail",
    // "rubber duck indoors". A comparison is meaningless without it, because
    // the whole output is "A against B" and nobody remembers which was which.
    char     label[24];
    uint32_t startMs;
    uint32_t stopMs;        // 0 while this run is the one recording
    uint32_t frames;        // direct frames counted into it, all nodes together
    uint16_t nodes;          // rows held
    // Direct frames the run could not place because RUN_NODES was full. Frames
    // and not nodes: counting distinct turned-away stations would need a set of
    // its own, and the useful fact is how much of the air this run could not
    // account for -- a non-zero figure means the spot has more than RUN_NODES
    // first-hand neighbours and the run sampled them rather than holding them all.
    uint16_t framesDropped;
    uint8_t  profile;       // the receive profile the radio was on when it started
    // Whether the radio was walking profiles (Lora::Mode::SURVEY) rather than
    // parked on one. A hopping run listens to any single network for only a
    // fraction of its wall time, and which fraction depends on where the walk
    // happened to be -- so its frame counts are not comparable with a parked
    // run's, and Compare::incomparable is set when two runs disagree about
    // this. An antenna test should be taken parked: LORA FOCUS <n> first.
    bool     hopping;
    bool     used;
};

// ---- a run's figures for one node ------------------------------------------

struct NodeStats {
    Proto    proto;
    uint64_t id;
    // The node's short handle, as the node table spells it. Empty for a node
    // heard exactly once: the decoders set the tag AFTER the frame is counted,
    // so the second frame from a station is what fills this in. A reader with
    // an empty tag should print the id in hex rather than nothing.
    char     tag[12];
    uint16_t frames;        // direct frames from this node in this run
    int16_t  rssiMin, rssiMed, rssiMax;   // dBm
    int16_t  snrMin4, snrMed4, snrMax4;   // SNR x 4
    uint32_t firstMs, lastMs;
    uint16_t outOfRange;    // readings outside [RSSI_LO, RSSI_HI]: a driver fault
    bool     enough;        // frames >= EVIDENCE_MIN
};

// ---- the comparison, which is the point ------------------------------------

// One node present in both runs.
struct Pair {
    Proto    proto;
    uint64_t id;
    char     tag[12];
    uint16_t framesA, framesB;
    int16_t  dRssi;    // B's median minus A's, in dB. Positive = B heard it louder.
    int16_t  dSnr4;    // the same in SNR x 4
    bool     enough;   // both sides reached EVIDENCE_MIN; false rows are shown, not counted
};

enum Verdict : uint8_t {
    V_NO_DATA = 0,   // no node with enough evidence in both runs: nothing to compare
    V_LOPSIDED,      // the two runs hold very different amounts of evidence
    V_THIN,          // fewer than three paired nodes: the numbers are printed, not trusted
    // The paired nodes do not agree with each other, or the node count and the
    // dB figure point opposite ways. NOT a disagreement between RSSI and SNR:
    // those two are reported side by side and left to the reader, because
    // neither is wrong when they part company -- see the note below the rule.
    V_MIXED,
    V_BETTER,        // B, the second run, is the better antenna or the better spot
    V_WORSE,         // A was
    V_SAME           // nothing moved by as much as the measurement can express
};
const char* verdictText(Verdict v);

struct Compare {
    uint8_t  a, b;
    uint16_t paired;    // nodes in both runs with enough evidence on both sides
    uint16_t thin;      // nodes in both runs where one side is under EVIDENCE_MIN
    // Nodes B heard with enough evidence and A did not hear at all, and the
    // other way round. Deliberately NOT folded into the dB figure: a node the
    // other antenna could not hear has no delta to take a median of, and there
    // is no honest exchange rate between "one more neighbour" and "n dB". A run
    // that gained a node and lost a decibel on the rest is the better antenna,
    // and no single number says that -- so both are reported and the verdict
    // reads them together.
    uint16_t gained, lost;
    int16_t  medDRssi;  // median of the paired dRssi, dB. The headline figure.
    int16_t  medDSnr4;  // median of the paired dSnr4, SNR x 4
    // The scatter the median came out of: the smallest and largest paired
    // dRssi. A median of +2 dB out of deltas spread from -8 to +11 is not a
    // result, and a view that prints the median without this is lying by
    // omission.
    int16_t  spreadLo, spreadHi;
    uint16_t better, worse, same;   // sign of dRssi over the paired set
    // The exact two-sided sign test on (better, worse), in per-mille: the
    // probability that a coin would split the paired nodes at least this
    // unevenly. 5 of 5 nodes improving is 62; 3 against 2 is 1000. No
    // distributional assumption and no fitted constant -- it is
    // 2 * sum(C(n,k), k <= min(better,worse)) / 2^n with n = better + worse,
    // which is small-integer arithmetic this board can do exactly.
    uint16_t signPermille;
    // One run holds less than half the direct frames of the other, or ran for
    // less than half as long. Then "gained a node" is confounded with "listened
    // longer" and the comparison must say so rather than print a verdict.
    bool     lopsided;
    // One run was hopping profiles and the other was parked: see Run::hopping.
    bool     incomparable;
    Verdict  verdict;
};

// One sentence for a screen or a console line: what the verdict rests on, in
// words, including the case where it rests on almost nothing. This is the
// "evidence, always visible" rule made into a function, so that no caller has
// to remember to print the counts beside the number.
void verdictLine(const Compare& c, char* out, size_t cap);

// The rule the verdict follows, stated here rather than tuned somewhere in the
// .cpp, and checked in test/lora_survey_test.cpp:
//
//   paired == 0                                  -> V_NO_DATA
//   incomparable                                 -> V_MIXED   (one run hopped)
//   lopsided                                     -> V_LOPSIDED
//   paired < 3                                   -> V_THIN
//   gained != lost and the dB figure disagrees   -> V_MIXED
//   better and worse both > 0 and sign p > 250   -> V_MIXED
//   medDRssi >= +1, or gained > lost with medDRssi >= 0   -> V_BETTER
//   medDRssi <= -1, or lost > gained with medDRssi <= 0   -> V_WORSE
//   otherwise                                    -> V_SAME
//
// Two of those numbers are not arbitrary. The 1 dB threshold is the unit the
// measurement is expressed in: Packet::rssi is whole dBm, so a median
// difference under 1 dB is below the resolution of what was recorded. The 250
// per-mille is a stated rule and not a statistic -- "a coin would produce a
// split this uneven more than one time in four, so the nodes are not agreeing".
//
// RSSI LEADS AND SNR IS KEPT BESIDE IT. The verdict's dB figure is RSSI,
// because received power in the direction of that node is the thing an antenna
// changes, and it is the only figure in which the node's own half of the link
// cancels out in a paired comparison. The SX126x's SNR estimate moves with
// RSSI but also with the noise floor at that instant, which is set by whatever
// else is on the band and by the electrical noise of wherever the board is
// standing -- a second variable the antenna does not control. But SNR is the
// figure that decides whether a frame decodes at all, so gained and lost ARE
// an SNR verdict in disguise, and medDSnr4 is reported next to medDRssi so
// that a disagreement between them is visible instead of averaged away.

// ---- the store -------------------------------------------------------------

// Exactly how many bytes begin() wants. Two runs' worth of histogram is the
// bulk of it.
size_t bytesNeeded();
// Hand over a block at least bytesNeeded() long; it is zeroed and kept. False
// when the block is too small or null, and then nothing is ever recorded --
// there is no fallback into internal RAM, because a survey is a field feature
// and a board that cannot spare 100 kB of PSRAM should not be quietly spending
// its last contiguous internal block on one.
bool   begin(void* mem, size_t bytes);
bool   ready();

// ---- feeding it ------------------------------------------------------------

// One direct reception. Called from Lora::Nodes' heard() and from nowhere
// else, because that is the single place the DIRECT/VIA rule is decided --
// re-deriving it here would give two rules that drift apart.
//
// `tag` may be empty on a node's first frame (the decoders fill it in after
// the frame is counted); the newest non-empty tag offered is the one kept.
void noteDirect(Proto p, uint64_t id, const char* tag, int16_t rssi, int8_t snr4, uint32_t now);

// ---- runs ------------------------------------------------------------------

// Begin recording. `label` is what is being tested; `profile` and `hopping`
// are the radio's state, which the run carries so a later comparison can
// refuse two runs taken under different receive conditions.
//
// A run already recording is stopped first. With all RUNS slots used the
// oldest FINISHED one is reused -- never the one being recorded, and never a
// run newer than another free choice -- so the two runs a comparison is about
// survive a third start. Returns the run index, or -1 when the store is not up.
int8_t  start(const char* label, uint8_t profile, bool hopping, uint32_t now);
// Stop the recording run. False when none was.
bool    stop(uint32_t now);
// The run being recorded, or -1.
int8_t  recording();
// Rename the run being recorded -- the owner usually knows which antenna is on
// the board before the run and sometimes not until after. False when none is.
bool    label(const char* label);

uint8_t runCount();                       // slots in use, recording one included
bool    run(uint8_t i, Run& out);
void    clear();                          // every run and the live trend
// Drop ONE run. False when that slot held nothing, or is past the end -- which
// is what lets a console command say "no run 3" rather than report success on a
// number nobody has.
//
// The slot is emptied WHERE IT IS and every other run keeps its index, because
// a run's number IS its slot (see start(), and the "%u" in every console line):
// renumbering would move run 3 to run 2 under a reader who is halfway through
// comparing 2 against 4. An empty slot in the middle is the honest picture, and
// start() takes the first free one, so the slot comes straight back into use.
//
// Dropping the run that is RECORDING aborts it: nothing is kept and recording()
// goes back to -1. That is the case this exists for -- a run started by
// mistake, which is not a finished run and cannot be stopped into anything
// worth keeping.
//
// The live trend is NOT touched: it is a ring of the last readings per node and
// belongs to no run. clear() is the one that takes it.
bool    clearRun(uint8_t i);

// A run's rows, loudest median first, rows under EVIDENCE_MIN last. Paged the
// way Lora::nodeSnapshot is, and for the same reason: one lock acquisition per
// page rather than one per row. Returns how many were written.
uint8_t runNodes(uint8_t run, NodeStats* out, uint8_t cap, uint8_t from = 0);
// One named row of one run.
bool    runNode(uint8_t run, Proto p, uint64_t id, NodeStats& out);

// A against B, over the nodes they share. Both must be finished or the
// recording one's figures are a snapshot of a run in progress -- which is
// allowed and useful (watch the deltas settle while you hold the antenna
// still), but Run::stopMs == 0 is how a view tells.
bool    compare(uint8_t a, uint8_t b, Compare& out);
// The rows behind that comparison, biggest improvement first. Rows whose
// evidence is thin on either side come last and carry enough == false.
uint8_t pairs(uint8_t a, uint8_t b, Pair* out, uint8_t cap, uint8_t from = 0);

// ---- the live trend --------------------------------------------------------

// The last TREND_LEN direct readings from one node, OLDEST FIRST so a view can
// walk it left to right. Independent of whether a run is recording: this is
// the line the owner watches while turning the antenna, and it has to move
// before anything has been decided to record. Returns how many were written.
uint8_t trend(Proto p, uint64_t id, Sample* out, uint8_t cap);
// How many nodes the trend table holds, and the oldest-heard of them gives way
// when a new one arrives: which nodes those are is the node table's business
// (directPackets > 0 there is the same filter), so this is only for a view that
// wants to say "no trend yet".
uint8_t trendNodes();

}
}
