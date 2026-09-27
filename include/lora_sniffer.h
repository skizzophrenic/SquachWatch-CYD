// SquachWatch-CYD — the LoRa sniffer: what main.cpp, the console and the
// screens see of the SX1262. The radio itself is in lora_radio.h.
//
// Only the CrowPanel 7 has the slot (-DSQUACH_LORA). Everywhere else the
// calls below are inline no-ops, so the emulator and the other boards include
// this header and never link the radio.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "lora_pkt.h"
#include "lora_nodes.h"
#include "lora_survey.h"
#include "lora_msgs.h"

namespace Lora {

// SWEEP is the spectrum: the radio walks the band reading the instantaneous
// RSSI and hears nothing else meanwhile. Not a saved setting.
enum class Mode : uint8_t { OFF = 0, FOCUS = 1, SURVEY = 2, SWEEP = 3, COUNT = 4 };
const char* modeName(Mode m);

// Counters since boot, for the CHANNEL screen and the console.
struct Stats {
    uint32_t packets;       // frames read, good or not
    uint32_t crcErrors;     // frames with a failed payload CRC
    uint32_t headerErrors;  // a header that did not check: another sync word, or a collision
    uint32_t preambles;     // preambles that never became a header
    uint32_t cadRounds;     // CAD checks run
    uint32_t cadHits;       // ...that heard something
    uint32_t airtimeMs;     // sum of the time on air of every frame read
    uint32_t listenMs;      // how long the radio has been in RX or CAD in total
    int16_t  noiseDbm;      // the quietest recent instantaneous RSSI reading
    uint16_t byProfile[64]; // frames per profile index
    uint16_t byProto[(int)Proto::COUNT];
};

// SQUACH_LORA_FAKE takes this branch with sim/lora_sim.cpp as the definitions
// instead of src/lora_sniffer.cpp: the emulator has to be able to DRIVE this
// screen, and the no-op branch below renders ten views of nothing. It is a
// second macro rather than -DSQUACH_LORA in sim/Makefile because SQUACH_LORA
// also switches on the network halves of lora_feed.cpp and lora_enrich.cpp,
// which want WiFi.h and HTTPClient.h, and sim/ has no WiFi shim.
#if SQUACH_LORA || SQUACH_LORA_FAKE
// After the WiFi and Bluetooth radios are up. True when a module answered.
bool begin();
bool present();
// One line for DIAGNOSTICS: the chip, the oscillator, the mode.
void statusLine(char* out, size_t cap);
// Each pass of loop(): prints new frames to the console and keeps the
// counters. Cheap when nothing arrived.
void tick(uint32_t now);

void setMode(Mode m);
Mode mode();
void setFocus(uint8_t profileIdx);
uint8_t focus();
// Which profiles the survey rotates through, as a bitmask over the table
// (bit i = profile i). Defaults to everything in the module's own band.
void setSurveyMask(uint64_t mask);
uint64_t surveyMask();
// Which profile the radio is on right now.
uint8_t currentProfile();

// The ring: newest first. packetAt copies, so the caller may hold it across
// frames; false past the end.
uint32_t packetTotal();
uint16_t packetCount();
bool     packetAt(uint16_t idxFromNewest, Packet& out);
// A page of the ring in ONE lock acquisition, newest first: up to `cap` records
// starting at `from` in that order. Returns how many were written.
//
// The same shape and the same reason as nodeSnapshot below: push() runs on the
// radio task inside this mutex, so every acquisition is a chance to wait on a
// decoder, and a view that draws fourteen rows one packetAt at a time takes
// fourteen of those waits per frame. A live traffic monitor redraws constantly
// and a filtered list (adverts only) has to walk further than it draws, which
// makes this the call both of them want.
//
// A Packet is about 290 bytes, so the page size is the caller's memory
// decision: fourteen rows is 4 kB and contiguous internal RAM on the CrowPanel
// 7 measures about 57 kB. Full records rather than a summary struct on purpose
// -- deciding whether a frame is a MeshCore advert needs MeshCore::parse() over
// the bytes, which is plaintext header work with no crypto in it, and a summary
// that left the bytes behind would send the caller back for them one at a time.
uint16_t packetSnapshot(Packet* out, uint16_t cap, uint16_t from = 0);
const Stats& stats();

// The node table, copied out under the sniffer's lock: how many, the order
// newest first, and one row.
uint8_t nodeCount();
uint8_t nodeOrder(uint8_t* idx, uint8_t cap);
bool    nodeAt(uint8_t i, Nodes::Node& out);
// A page of the table in ONE lock acquisition, newest first: up to `cap` rows
// starting at `from` in that order. Returns how many were written, which is
// short of `cap` at the end of the table.
//
// The reason it exists rather than a loop over nodeAt(): note() runs on the
// radio task INSIDE the same mutex (src/lora_sniffer.cpp's push), so every
// acquisition here is a chance to wait on a decoder. Drawing fourteen rows
// took fifteen acquisitions -- one for the order and one per row -- and the
// enrichment scan wants the same copy anyway. One call, one wait.
//
// Paged rather than whole because the whole table is 96 rows of about 100
// bytes, and ten kilobytes is neither a stack buffer nor a good use of the
// internal RAM there is.
uint8_t nodeSnapshot(Nodes::Node* out, uint8_t cap, uint8_t from = 0);

// ---- the channel keys, for the CHANNELS view --------------------------------
// One row per key worth showing. The screens go through here rather than
// calling MeshCore:: and Meshtastic:: themselves, the same way they do for
// nodes: those namespaces are compiled on every board, this slot exists on
// one, and the no-op branch below is what keeps ui_lora.cpp board-agnostic.
struct ChannelRow {
    Proto    proto;
    char     name[24];
    uint8_t  hash;
    uint32_t frames;    // frames this key has opened since boot
    uint32_t lastMs;    // when the last one landed; 0 = never
    bool     builtIn;   // Public, or a Meshtastic preset: cannot be muted or dropped
    bool     enabled;
    bool     derived;   // MeshCore hashtag: the key comes from the name
    uint16_t keyBits;   // 128 or 256 -- and 256 does not fit in a byte; 0 = no encryption (ham mode)
};
uint8_t channelRowCount();
bool    channelRow(uint8_t i, ChannelRow& out);
// Mute or unmute the key on that row and write the list back to the store.
// A built-in row is left alone; returns false when nothing changed.
bool    toggleChannelRow(uint8_t i);
// Built-in keys that are NOT listed because nothing has arrived on them --
// twenty-eight Meshtastic presets on a quiet band. Counted, not hidden: the
// view says how many there are.
// Which decoder and which of ITS channel indices a CHANS row is. The row order
// is MeshCore's list and Meshtastic's flattened together and it changes as quiet
// presets start being listed, so a row number is not a channel identity -- this
// is what turns a tapped row into one, for reading that channel's messages.
bool    channelRowKey(uint8_t row, Proto& proto, uint8_t& idx);
uint8_t channelsQuietBuiltIn();
// How many of each table are the user's, and the room there is. For the one
// line on screen that says whether the list can still grow.
void    channelCapacity(uint8_t& mcUsed, uint8_t& mcMax, uint8_t& mtUsed, uint8_t& mtMax);

// The spectrum from SWEEP: one value per bin from 863.0 to 870.0 MHz in
// 50 kHz steps, as dBm + 150 (0 = nothing yet). `hold` keeps the loudest
// reading with a slow decay. Returns the bin count.
static const uint8_t SPECTRUM_BINS = 141;
uint8_t spectrum(uint8_t* live, uint8_t* hold, uint8_t cap);
uint32_t spectrumSweeps();

// ---- the antenna survey, under the sniffer's lock ---------------------------
// Lora::Survey is fed from the radio task (through Nodes::note, inside this
// mutex) and read from loop(), so the screens and the console go through here
// rather than calling Survey:: themselves -- exactly as they do for the node
// table. include/lora_survey.h is where the measurement is explained.
//
// surveyStart takes the radio's own state with it: the profile it is parked on,
// and whether it is hopping (Mode::SURVEY), which is the state an antenna run
// must NOT be taken in.
int8_t   surveyStart(const char* label);
bool     surveyStop();
bool     surveyLabel(const char* label);
int8_t   surveyRecording();
uint8_t  surveyRunCount();
bool     surveyRun(uint8_t i, Survey::Run& out);
uint8_t  surveyRunNodes(uint8_t run, Survey::NodeStats* out, uint8_t cap, uint8_t from = 0);
// One named row of one run, for a view that is already drawing that node and
// wants to say how much evidence it has given the run recording.
bool     surveyRunNode(uint8_t run, Proto p, uint64_t id, Survey::NodeStats& out);
bool     surveyCompare(uint8_t a, uint8_t b, Survey::Compare& out);
uint8_t  surveyPairs(uint8_t a, uint8_t b, Survey::Pair* out, uint8_t cap, uint8_t from = 0);
uint8_t  surveyTrend(Proto p, uint64_t id, Survey::Sample* out, uint8_t cap);
void     surveyClear();
// One run gone, the rest left exactly where they are -- LORA SURVEY DROP <n> and
// the SURVEYCMP view's hold-to-delete both come through here. False when that
// slot held nothing; include/lora_survey.h's clearRun says what it does to the
// slot table and what it does to a run that is recording.
bool     surveyDropRun(uint8_t i);
// The verdict in a word, and the sentence that says what it rests on.
//
// Pure functions over the caller's own Compare -- nothing shared, so no lock --
// and they are here rather than called as Survey:: directly because a screen may
// only speak to Lora::. That is what keeps ui_lora.cpp compiling on the boards
// with no radio, and what keeps lora_survey.o's code out of their flash: the
// 2.8" board is at 87 % of its app slot and cannot pay for a screen it can
// never reach.
const char* surveyVerdictText(Survey::Verdict v);
void        surveyVerdictLine(const Survey::Compare& c, char* out, size_t cap);

// ---- the decoded messages, under the same lock -----------------------------
// include/lora_msgs.h for what is kept and what is not.
uint16_t msgCount();
uint32_t msgDropped();
bool     msgAt(uint16_t idxFromNewest, Msgs::Msg& out);
uint16_t msgCountFor(Proto p, uint8_t chan);
uint16_t msgPageFor(Proto p, uint8_t chan, Msgs::Msg* out, uint16_t cap, uint16_t from = 0);

// LoRaTap over the console: every frame as one "[tap] <hex>" line, which
// tools/loratap2pcap.py turns into a capture Wireshark opens.
void setTap(bool on);
bool tap();

// The "LORA ..." console commands; true if the line was one.
bool console(const char* line);
#else
inline bool begin() { return false; }
inline bool present() { return false; }
inline void statusLine(char* out, size_t cap) { if (cap) out[0] = '\0'; }
inline void tick(uint32_t) {}
inline void setMode(Mode) {}
inline Mode mode() { return Mode::OFF; }
inline void setFocus(uint8_t) {}
inline uint8_t focus() { return 0; }
inline void setSurveyMask(uint64_t) {}
inline uint64_t surveyMask() { return 0; }
inline uint8_t currentProfile() { return 0; }
inline uint32_t packetTotal() { return 0; }
inline uint16_t packetCount() { return 0; }
inline bool packetAt(uint16_t, Packet&) { return false; }
inline uint16_t packetSnapshot(Packet*, uint16_t, uint16_t = 0) { return 0; }
inline const Stats& stats() { static Stats s = {}; return s; }
inline uint8_t nodeCount() { return 0; }
inline uint8_t nodeOrder(uint8_t*, uint8_t) { return 0; }
inline bool nodeAt(uint8_t, Nodes::Node&) { return false; }
inline uint8_t nodeSnapshot(Nodes::Node*, uint8_t, uint8_t = 0) { return 0; }
struct ChannelRow {
    Proto    proto;
    char     name[24];
    uint8_t  hash;
    uint32_t frames, lastMs;
    bool     builtIn, enabled, derived;
    uint16_t keyBits;
};
inline uint8_t channelRowCount() { return 0; }
inline bool channelRow(uint8_t, ChannelRow&) { return false; }
inline bool toggleChannelRow(uint8_t) { return false; }
inline bool channelRowKey(uint8_t, Proto&, uint8_t&) { return false; }
inline uint8_t channelsQuietBuiltIn() { return 0; }
inline void channelCapacity(uint8_t& a, uint8_t& b, uint8_t& c, uint8_t& d) { a = b = c = d = 0; }
static const uint8_t SPECTRUM_BINS = 141;
inline uint8_t spectrum(uint8_t*, uint8_t*, uint8_t) { return 0; }
inline uint32_t spectrumSweeps() { return 0; }
inline int8_t surveyStart(const char*) { return -1; }
inline bool surveyStop() { return false; }
inline bool surveyLabel(const char*) { return false; }
inline int8_t surveyRecording() { return -1; }
inline uint8_t surveyRunCount() { return 0; }
inline bool surveyRun(uint8_t, Survey::Run& o) { o = Survey::Run(); return false; }
inline uint8_t surveyRunNodes(uint8_t, Survey::NodeStats*, uint8_t, uint8_t = 0) { return 0; }
inline bool surveyRunNode(uint8_t, Proto, uint64_t, Survey::NodeStats& o) { o = Survey::NodeStats(); return false; }
inline bool surveyCompare(uint8_t, uint8_t, Survey::Compare& o) { o = Survey::Compare(); return false; }
inline uint8_t surveyPairs(uint8_t, uint8_t, Survey::Pair*, uint8_t, uint8_t = 0) { return 0; }
inline uint8_t surveyTrend(Proto, uint64_t, Survey::Sample*, uint8_t) { return 0; }
inline void surveyClear() {}
inline bool surveyDropRun(uint8_t) { return false; }
inline const char* surveyVerdictText(Survey::Verdict) { return "NO DATA"; }
inline void surveyVerdictLine(const Survey::Compare&, char* out, size_t cap) { if (cap) out[0] = '\0'; }
inline uint16_t msgCount() { return 0; }
inline uint32_t msgDropped() { return 0; }
inline bool msgAt(uint16_t, Msgs::Msg& o) { o = Msgs::Msg(); return false; }
inline uint16_t msgCountFor(Proto, uint8_t) { return 0; }
inline uint16_t msgPageFor(Proto, uint8_t, Msgs::Msg*, uint16_t, uint16_t = 0) { return 0; }
inline void setTap(bool) {}
inline bool tap() { return false; }
inline bool console(const char*) { return false; }
#endif

}
