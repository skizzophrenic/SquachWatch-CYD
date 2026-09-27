// SquachWatch-Sim — the radio that is not there.
//
// WHY THIS FILE EXISTS. src/ui_lora.cpp is ten views, a three-slot bar and a
// modal panel, and until now the only way to look at any of it was to flash a
// CrowPanel 7: the emulator compiled the screen but not the radio behind it, so
// -DSQUACH_LORA was off, every Lora:: call was the header's inline no-op, and
// the screen rendered nothing but its chrome and "NO MODULE". A LoRa-screen bug
// therefore could not be reproduced off the board -- which is how one reached
// the owner's bench (docs: the VIEWS button). This is the stand-in that closes
// that hole.
//
// WHAT IT IS AND IS NOT. It is a FIXTURE, not an emulation of an SX1262: a
// handful of frames built the way test/lora_nodes_test.cpp builds them, run
// through the real classifier and the real node table, plus two survey runs of
// fabricated samples. Everything it invents says so out loud -- the node names
// are SIM-*, the survey runs are "sim walk A"/"sim walk B", the status line
// begins "SIM" -- because a fake capture that reads like a real one is how a
// screenshot ends up in a bug report as evidence. Nothing here is a measurement.
//
// WHAT IS REAL IN IT. The frame bytes are genuine MeshCore and Meshtastic
// frames and they go through Lora::classify(), Nodes::note(), Msgs::note() and
// Survey::, all the firmware's own code -- so the rows, the decoded summaries,
// the channel colours and the comparison arithmetic on screen are the real
// thing over invented input. Only the sniffer's own plumbing (the ring, the
// lock, the counters, the channel-key list) is reimplemented here, because
// src/lora_sniffer.cpp is freertos, SPI and RadioLib and cannot be compiled on
// a desktop.
//
// HOW IT IS SELECTED. include/lora_sniffer.h declares this API under
// `#if SQUACH_LORA || SQUACH_LORA_FAKE`, and sim/Makefile defines the second
// one. SQUACH_LORA itself stays off here: it also switches on the network
// halves of lora_feed.cpp and lora_enrich.cpp, which want WiFi.h and
// HTTPClient.h, and there is no WiFi shim in sim/.
#include "lora_sniffer.h"
#include "lora_classify.h"
#include "lora_crypto.h"
#include "lora_meshcore.h"
#include "lora_meshtastic.h"
#include "lora_profiles.h"
#include <Arduino.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace Lora {

// The device's copy lives in src/lora_sniffer.cpp, which this build cannot
// compile. Four strings; keep them identical to that one.
const char* modeName(Mode m) {
    switch (m) {
        case Mode::FOCUS:  return "FOCUS";
        case Mode::SURVEY: return "SURVEY";
        case Mode::SWEEP:  return "SWEEP";
        default:           return "OFF";
    }
}

namespace {

// Twelve frames: more than the LIST body holds at rowH() (nine rows at
// 400x240), so the fixture exercises the scrollbar and the clamp as well as
// the rows -- and so a tap that lands one row PAST the last drawn row has a
// row to land on, which is exactly the geometry a hit-test bug hides in.
static const uint16_t RING = 12;
Packet   s_ring[RING];          // newest first, the order packetAt() promises
uint16_t s_count = 0;
uint32_t s_total = 0;
bool     s_present = false;
Mode     s_mode = Mode::SURVEY;
uint8_t  s_focus = 0;
uint64_t s_mask = ~0ull;
uint8_t  s_current = 0;         // the profile the fake frames were built for
Stats    s_stats = {};
bool     s_tap = false;
uint8_t  s_spectrum[SPECTRUM_BINS] = { 0 };

// The channel keys the CHANS view lists. Fabricated rather than read out of
// Lora::Chan, whose store is NVS on the device: three rows is all the view
// needs to be looked at, and the fourth column (frames seen) is what makes the
// rows differ from each other.
struct FakeChan { Proto proto; const char* name; uint8_t hash; uint32_t frames; bool builtIn; bool derived; uint16_t keyBits; };
const FakeChan CHANS[] = {
    { Proto::MESHCORE,   "Public",        0x8F, 7, true,  false, 128 },
    { Proto::MESHCORE,   "#sim-hashtag",  0x21, 2, false, true,  256 },
    { Proto::MESHTASTIC, "LongFast",      0x08, 4, true,  false, 128 },
};
const uint8_t CHAN_N = (uint8_t)(sizeof CHANS / sizeof CHANS[0]);
bool s_chanOn[CHAN_N] = { true, true, true };

void pushFrame(const Packet& pk) {
    if (s_count < RING) s_count++;
    for (int i = (int)s_count - 1; i > 0; i--) s_ring[i] = s_ring[i - 1];
    s_ring[0] = pk;
    s_total++;
    s_stats.packets++;
    s_stats.airtimeMs += pk.toaUs / 1000;
    s_stats.byProto[(int)pk.proto]++;
    if (pk.profile < 64) s_stats.byProfile[pk.profile]++;
    Nodes::note(pk, pk.ms, 0);
}

// ---- the frames, built the way test/lora_nodes_test.cpp builds them ---------

// A MeshCore flood advert: 32 bytes of key, a timestamp, a signature nobody
// checks, then the name. `hops` path bytes in front say how many repeaters this
// copy crossed; zero means we heard the advertiser's own transmitter, which is
// what the survey counts.
void mcAdvert(uint8_t key0, uint8_t hops, int16_t rssi, int8_t snr4,
              const char* name, uint32_t ms) {
    Packet pk; memset(&pk, 0, sizeof pk);
    pk.ms = ms;
    pk.sync = 0x12; pk.sf = 8; pk.bwKhz10 = 625; pk.freqHz = 869618000; pk.cr = 8;
    pk.flags = PK_CRC_PRESENT | PK_CRC_OK | PK_LIVE;
    pk.profile = s_current;
    pk.rssi = rssi; pk.snr4 = snr4; pk.ferrHz = -240;
    uint8_t pay[200]; uint8_t n = 0;
    for (int i = 0; i < 32; i++) pay[n++] = (uint8_t)(key0 + i);
    LoraCrypto::wr32le(pay + n, 1790000000u); n = (uint8_t)(n + 4);
    for (int i = 0; i < 64; i++) pay[n++] = 0xEE;             // the signature
    pay[n++] = (uint8_t)(0x80 | MeshCore::NODE_REPEATER);     // a name follows
    const uint8_t nl = (uint8_t)strlen(name);
    memcpy(pay + n, name, nl); n = (uint8_t)(n + nl);
    uint8_t d = 0;
    pk.data[d++] = 0x11;                                      // flood ADVERT
    pk.data[d++] = hops;                                      // one-byte path hashes
    for (uint8_t i = 0; i < hops; i++) pk.data[d++] = (uint8_t)(0xA0 + i);
    memcpy(pk.data + d, pay, n); d = (uint8_t)(d + n);
    pk.len = d;
    pk.toaUs = timeOnAirUs(pk.sf, pk.bwKhz10, pk.cr, 32, pk.len, true, false, false);
    classify(pk);
    pushFrame(pk);
}

// A Meshtastic frame on channel 0, encrypted the way the air carries it (CTR,
// so the test's decrypt() call IS the encrypt) and then classified and decoded
// back out again -- which is what puts a decoded line on the LIST row and a
// message in the MESSAGES view.
void mtFrame(uint32_t from, uint8_t flags, const uint8_t* data, uint8_t dlen,
             uint32_t id, int16_t rssi, int8_t snr4, uint32_t ms) {
    Packet pk; memset(&pk, 0, sizeof pk);
    pk.ms = ms;
    pk.sync = 0x2B; pk.sf = 11; pk.bwKhz10 = 2500; pk.freqHz = 869525000; pk.cr = 5;
    pk.flags = PK_CRC_PRESENT | PK_CRC_OK | PK_LIVE;
    pk.profile = 0;                                           // MT LongFast
    pk.rssi = rssi; pk.snr4 = snr4; pk.ferrHz = 180;
    LoraCrypto::wr32le(pk.data, Meshtastic::BROADCAST);
    LoraCrypto::wr32le(pk.data + 4, from);
    LoraCrypto::wr32le(pk.data + 8, id);
    pk.data[12] = flags; pk.data[13] = 0x08; pk.data[14] = 0; pk.data[15] = (uint8_t)from;
    memcpy(pk.data + 16, data, dlen);
    pk.len = (uint8_t)(16 + dlen);
    Meshtastic::Header h; Meshtastic::parseHeader(pk.data, pk.len, h);
    Meshtastic::Channel c = Meshtastic::channel(0);
    Meshtastic::decrypt(h, c, pk.data + 16, dlen);
    pk.toaUs = timeOnAirUs(pk.sf, pk.bwKhz10, pk.cr, 16, pk.len, true, false, false);
    classify(pk);
    pushFrame(pk);
}

// A frame nothing claims: the classifier's UNKNOWN row, which is a sighting
// too, and the one the LIST draws in red when the CRC failed.
void unknownFrame(bool crcErr, int16_t rssi, uint32_t ms) {
    Packet pk; memset(&pk, 0, sizeof pk);
    pk.ms = ms;
    pk.sync = 0x12; pk.sf = 9; pk.bwKhz10 = 1250; pk.freqHz = 869400000; pk.cr = 5;
    pk.flags = (uint8_t)(PK_CRC_PRESENT | PK_LIVE | (crcErr ? PK_CRC_ERR : PK_CRC_OK));
    pk.profile = PROFILE_CUSTOM;
    pk.rssi = rssi; pk.snr4 = -12;
    static const char* SAY = "SIMULATED FRAME - NOT A CAPTURE";
    pk.len = (uint8_t)strlen(SAY);
    memcpy(pk.data, SAY, pk.len);
    pk.toaUs = timeOnAirUs(pk.sf, pk.bwKhz10, pk.cr, 16, pk.len, true, false, false);
    if (!crcErr) classify(pk);      // a broken frame is never decoded, on the device either
    pushFrame(pk);
}

// A Meshtastic text message payload: portnum 1 (TEXT_MESSAGE_APP) and the text.
uint8_t mtText(const char* text, uint8_t* out) {
    const uint8_t n = (uint8_t)strlen(text);
    out[0] = 0x08; out[1] = 0x01;          // portnum = 1
    out[2] = 0x12; out[3] = n;             // payload, length-delimited
    memcpy(out + 4, text, n);
    return (uint8_t)(4 + n);
}

void seedSurvey(uint32_t now) {
    if (!Survey::ready()) return;
    // Two runs an owner would actually take: the same three stations, heard
    // through two antennas, the second one better on all three. The numbers
    // are invented; what is real is that the comparison arithmetic on the
    // SURVEYCMP view is the firmware's own over them.
    struct Row { Proto p; uint64_t id; const char* tag; int16_t a, b; };
    static const Row ROWS[] = {
        { Proto::MESHCORE,   0x515a5b5cull, "SIM-HILL",  -104, -96 },
        { Proto::MESHCORE,   0x616a6b6cull, "SIM-ROOF",   -88,  -83 },
        { Proto::MESHTASTIC, 0x3b1c9a2eull, "!3b1c9a2e", -112, -101 },
    };
    const uint32_t startA = now - 300000, startB = now - 150000;
    for (int run = 0; run < 2; run++) {
        const uint32_t t0 = run ? startB : startA;
        Survey::start(run ? "sim walk B" : "sim walk A", s_current, false, t0);
        for (uint32_t k = 0; k < 8; k++)
            for (size_t i = 0; i < sizeof ROWS / sizeof ROWS[0]; i++) {
                const int16_t base = run ? ROWS[i].b : ROWS[i].a;
                // A little scatter, so the medians are medians of something
                // and the trend graph has a shape.
                const int16_t r = (int16_t)(base + (int16_t)(k % 3) - 1);
                Survey::noteDirect(ROWS[i].p, ROWS[i].id, ROWS[i].tag, r,
                                   (int8_t)(-20 + (int8_t)k), t0 + k * 6000);
            }
        Survey::stop(t0 + 60000);
    }
}

}   // namespace

// ---- bring-up ---------------------------------------------------------------

bool begin() {
    if (s_present) return true;
    // The profile the fake MeshCore frames were built for, found by name so an
    // edit to the table cannot silently move the fixture onto another PHY.
    for (uint8_t i = 0; i < profileCount(); i++)
        if (strcmp(profile(i).name, "MC EU Narrow") == 0) { s_current = i; break; }
    s_focus = s_current;

    static void* surveyMem = nullptr;
    if (!surveyMem) {
        surveyMem = malloc(Survey::bytesNeeded());
        if (surveyMem) Survey::begin(surveyMem, Survey::bytesNeeded());
    }
    static void* msgMem = nullptr;
    if (!msgMem) {
        msgMem = malloc(Msgs::bytesNeeded());
        if (msgMem) Msgs::begin(msgMem, Msgs::bytesNeeded());
    }

    const uint32_t now = millis();
    // Oldest first: pushFrame puts each at the head, so the ring ends up
    // newest-first the way packetAt() promises.
    unknownFrame(true,  -119, now - 240000);
    mcAdvert(0x40, 2, -104, 20, "SIM-HILL",  now - 213000);
    uint8_t body[64];
    mtFrame(0x3b1c9a2e, 0x63, body, mtText("this is a simulated frame", body), 1,
            -112, -8, now - 188000);
    mcAdvert(0x60, 0,  -88, 48, "SIM-ROOF",  now - 165000);
    unknownFrame(false, -101, now - 140000);
    mcAdvert(0x40, 0, -103, 24, "SIM-HILL",  now - 121000);
    mtFrame(0x3b1c9a2e, 0x63, body, mtText("nothing here came off the air", body), 2,
            -109, -4, now - 96000);
    mcAdvert(0x60, 1,  -90, 44, "SIM-ROOF",  now - 74000);
    mcAdvert(0x80, 0, -117, 4,  "SIM-SHED",  now - 51000);
    mtFrame(0x9f00beefu, 0x23, body, mtText("fixture, not a capture", body), 3,
            -95, 12, now - 33000);
    mcAdvert(0x60, 0,  -86, 52, "SIM-ROOF",  now - 17000);
    mcAdvert(0x40, 0, -102, 28, "SIM-HILL",  now - 4000);

    s_stats.crcErrors    = 1;
    s_stats.headerErrors = 3;
    s_stats.preambles    = 41;
    s_stats.cadRounds    = 9120;
    s_stats.cadHits      = 57;
    s_stats.listenMs     = 240000;
    s_stats.noiseDbm     = -119;
    // A spectrum with two humps in it, so the STATS view's graph has a shape
    // to draw rather than a flat line. dBm + 150, the units spectrum() uses.
    for (uint8_t i = 0; i < SPECTRUM_BINS; i++) {
        const int hump = (i > 40 && i < 58) ? 18 : (i > 88 && i < 104) ? 26 : 0;
        s_spectrum[i] = (uint8_t)(31 + hump + (i % 5));
    }
    seedSurvey(now);
    s_present = true;
    return true;
}

bool present() { return s_present; }

void statusLine(char* out, size_t cap) {
    snprintf(out, cap, "SIM: no radio -- %u fabricated frames, %u nodes",
             (unsigned)s_count, (unsigned)Nodes::count());
}

// Nothing arrives after begin(): a fixture that grew a frame every few seconds
// would make every rendered PNG differ from the last one for no reason.
void tick(uint32_t) {}

void setMode(Mode m) { s_mode = m; }
Mode mode() { return s_mode; }
void setFocus(uint8_t p) { s_focus = p; }
uint8_t focus() { return s_focus; }
void setSurveyMask(uint64_t m) { s_mask = m; }
uint64_t surveyMask() { return s_mask; }
uint8_t currentProfile() { return s_current; }

// ---- the ring ---------------------------------------------------------------

uint32_t packetTotal() { return s_total; }
uint16_t packetCount() { return s_count; }

bool packetAt(uint16_t idxFromNewest, Packet& out) {
    if (idxFromNewest >= s_count) return false;
    out = s_ring[idxFromNewest];
    return true;
}

uint16_t packetSnapshot(Packet* out, uint16_t cap, uint16_t from) {
    uint16_t n = 0;
    for (uint16_t i = from; i < s_count && n < cap; i++) out[n++] = s_ring[i];
    return n;
}

const Stats& stats() { return s_stats; }

// ---- the node table, which is the firmware's own ---------------------------

uint8_t nodeCount() { return Nodes::count(); }
uint8_t nodeOrder(uint8_t* idx, uint8_t cap) { return Nodes::order(idx, cap); }

bool nodeAt(uint8_t i, Nodes::Node& out) {
    const Nodes::Node* n = Nodes::at(i);
    if (!n) return false;
    out = *n;
    return true;
}

uint8_t nodeSnapshot(Nodes::Node* out, uint8_t cap, uint8_t from) {
    uint8_t idx[Nodes::CAP];
    const uint8_t n = Nodes::order(idx, Nodes::CAP);
    uint8_t w = 0;
    for (uint8_t i = from; i < n && w < cap; i++) {
        const Nodes::Node* p = Nodes::at(idx[i]);
        if (p) out[w++] = *p;
    }
    return w;
}

// ---- the channel keys ------------------------------------------------------

uint8_t channelRowCount() { return CHAN_N; }

bool channelRow(uint8_t i, ChannelRow& out) {
    if (i >= CHAN_N) return false;
    out = ChannelRow{};
    out.proto = CHANS[i].proto;
    snprintf(out.name, sizeof out.name, "%s", CHANS[i].name);
    out.hash    = CHANS[i].hash;
    out.frames  = CHANS[i].frames;
    out.lastMs  = millis() - 30000u * (i + 1);
    out.builtIn = CHANS[i].builtIn;
    out.enabled = s_chanOn[i];
    out.derived = CHANS[i].derived;
    out.keyBits = CHANS[i].keyBits;
    return true;
}

bool toggleChannelRow(uint8_t i) {
    if (i >= CHAN_N || CHANS[i].builtIn) return false;
    s_chanOn[i] = !s_chanOn[i];
    return true;
}

bool channelRowKey(uint8_t row, Proto& proto, uint8_t& idx) {
    if (row >= CHAN_N) return false;
    proto = CHANS[row].proto;
    // The decoder's own channel index. Row order and channel index are the same
    // thing in a fixture this size; on the device they are not, which is what
    // Lora::channelRowKey exists for.
    idx = row;
    return true;
}

uint8_t channelsQuietBuiltIn() { return 28; }   // the Meshtastic presets nothing arrived on

void channelCapacity(uint8_t& mcUsed, uint8_t& mcMax, uint8_t& mtUsed, uint8_t& mtMax) {
    mcUsed = 1; mcMax = 8; mtUsed = 0; mtMax = 8;
}

uint8_t spectrum(uint8_t* live, uint8_t* hold, uint8_t cap) {
    const uint8_t n = cap < SPECTRUM_BINS ? cap : SPECTRUM_BINS;
    for (uint8_t i = 0; i < n; i++) {
        if (live) live[i] = s_spectrum[i];
        if (hold) hold[i] = (uint8_t)(s_spectrum[i] + 6);
    }
    return n;
}

uint32_t spectrumSweeps() { return 14; }

// ---- the survey, which is the firmware's own -------------------------------

int8_t surveyStart(const char* label) {
    return Survey::start(label && label[0] ? label : "sim run", s_current,
                         s_mode == Mode::SURVEY, millis());
}
bool     surveyStop() { return Survey::stop(millis()); }
bool     surveyLabel(const char* label) { return Survey::label(label); }
int8_t   surveyRecording() { return Survey::recording(); }
uint8_t  surveyRunCount() { return Survey::runCount(); }
bool     surveyRun(uint8_t i, Survey::Run& out) { return Survey::run(i, out); }
uint8_t  surveyRunNodes(uint8_t run, Survey::NodeStats* out, uint8_t cap, uint8_t from) {
    return Survey::runNodes(run, out, cap, from);
}
bool     surveyRunNode(uint8_t run, Proto p, uint64_t id, Survey::NodeStats& out) {
    return Survey::runNode(run, p, id, out);
}
bool     surveyCompare(uint8_t a, uint8_t b, Survey::Compare& out) { return Survey::compare(a, b, out); }
uint8_t  surveyPairs(uint8_t a, uint8_t b, Survey::Pair* out, uint8_t cap, uint8_t from) {
    return Survey::pairs(a, b, out, cap, from);
}
uint8_t  surveyTrend(Proto p, uint64_t id, Survey::Sample* out, uint8_t cap) {
    return Survey::trend(p, id, out, cap);
}
void     surveyClear() { Survey::clear(); }
bool     surveyDropRun(uint8_t i) { return Survey::clearRun(i); }
const char* surveyVerdictText(Survey::Verdict v) { return Survey::verdictText(v); }
void        surveyVerdictLine(const Survey::Compare& c, char* out, size_t cap) {
    Survey::verdictLine(c, out, cap);
}

// ---- the decoded messages, which are the firmware's own --------------------

uint16_t msgCount() { return Msgs::count(); }
uint32_t msgDropped() { return Msgs::dropped(); }
bool     msgAt(uint16_t i, Msgs::Msg& out) { return Msgs::at(i, out); }
uint16_t msgCountFor(Proto p, uint8_t chan) { return Msgs::countFor(p, chan); }
uint16_t msgPageFor(Proto p, uint8_t chan, Msgs::Msg* out, uint16_t cap, uint16_t from) {
    return Msgs::pageFor(p, chan, out, cap, from);
}

void setTap(bool on) { s_tap = on; }
bool tap() { return s_tap; }

// The console is the device's; there is no serial line here.
bool console(const char*) { return false; }

}   // namespace Lora
