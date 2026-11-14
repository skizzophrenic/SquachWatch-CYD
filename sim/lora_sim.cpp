// SquachWatch-CYD PC emulator — a made-up LoRa band for the T-Deck's LORA page.
//
// Compiled into the one-shot renderer only, with -DSQUACH_LORA_FAKE=1 (see
// sim/Makefile), which makes include/lora_sniffer.h declare the real Lora::
// calls instead of its inline no-ops. This file defines the ones
// src/ui_lorahome.cpp makes -- the strip's two trails, the node table, the
// newest packet, the chat ring, the mode and the counters, and sending -- and
// nothing else: a call nobody here makes is a link error, which is the loud
// way to find out a screen started wanting more. The profile table and its
// MHz/bandwidth formatting are the real src/lora_profiles.cpp, on the US 915
// plan the T-Deck runs.
//
// Everything is a pure function of the emulator's clock, so a scene renders
// the same every run and still moves: the noise floor wobbles round -112 dBm
// a sample a second, a packet lands every five or six seconds (Meshtastic and
// MeshCore in turn, -70 to -100 dBm), and one always lands 0.7 s into the
// capture, on the open tab's network, carrying a new chat line.
//
// HOUSE RULES for what is on this fake air, all of them hard:
//   * The board sits at Bluff Creek, 41.43910,-123.69580 -- the project's one
//     example place -- and every node is placed in miles from there. Never
//     any other real location.
//   * Everyone on the air is a cryptid. No real person's name, ever.
//   * No WiFi network names.
#include "lora_sim.h"
#include "lora_sniffer.h"
#include "lora_profiles.h"
#include "lora_meshtastic.h"
#include "lora_meshcore.h"
#include "ui_lorachat.h"
#include "gnss.h"
#include <Arduino.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <vector>

namespace {

SimLora::Config s_cfg;

// Bluff Creek, as degrees x 10^7, and as the NMEA the GPS would send.
const int32_t HOME_LAT7 = 414391000, HOME_LON7 = -1236958000;
const char* const GGA = "GNGGA,120000.000,4126.3460,N,12341.7480,W,1,09,0.9,121.0,M,-26.0,M,,";
const char* const RMC = "GNRMC,120000.000,A,4126.3460,N,12341.7480,W,0.00,0.0,140926,,,A";

uint32_t mix32(uint32_t x) { x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x; }

void nmea(const char* body, uint32_t now) {
    uint8_t cs = 0;
    for (const char* p = body; *p; p++) cs ^= (uint8_t)*p;
    char line[128];
    snprintf(line, sizeof line, "$%s*%02X", body, cs);
    Gnss::sentence(line, now);
}

// ---- the packets -----------------------------------------------------------------
// Hit k lands about k * PERIOD after the one in the capture (k = 0, exactly
// 0.7 s in), give or take 0.9 s; negative k are the history the page opens on.
const uint32_t PERIOD = 5600;

int64_t hitTime(int64_t k) {
    int64_t t = (int64_t)s_cfg.captureAt + 700 + k * (int64_t)PERIOD;
    if (k) t += (int64_t)(mix32((uint32_t)k * 2654435761u) % 1800) - 900;
    return t;
}
Lora::Proto hitProto(int64_t k) { return ((k + s_cfg.net) & 1) ? Lora::Proto::MESHCORE : Lora::Proto::MESHTASTIC; }
int16_t hitRssi(int64_t k) { return k == 0 ? -74 : (int16_t)(-70 - (int)(mix32((uint32_t)k * 7u + 3u) % 31)); }

bool hitHeard(int64_t t, uint32_t now) {
    if (t < 1 || t > (int64_t)now) return false;
    if (s_cfg.quietSecs && t >= (int64_t)s_cfg.captureAt - (int64_t)s_cfg.quietSecs * 1000) return false;
    return true;
}

// The newest `cap` packets heard by `now`, oldest first (the sniffer's order).
uint8_t hitsAt(uint32_t now, Lora::Hit* out, int64_t* ks, uint8_t cap) {
    const int64_t rel = (int64_t)now - (int64_t)s_cfg.captureAt - 700;
    int64_t k = (rel >= 0 ? rel / (int64_t)PERIOD : -((-rel + PERIOD - 1) / (int64_t)PERIOD)) + 1;
    uint8_t n = 0;
    Lora::Hit tmp[64]; int64_t tk[64];
    for (int guard = 0; n < cap && n < 64 && guard < 400; guard++, k--) {
        const int64_t t = hitTime(k);
        if (!hitHeard(t, now)) { if (t < 1) break; continue; }
        tmp[n] = Lora::Hit{ (uint32_t)t, hitRssi(k), hitProto(k) };
        tk[n] = k;
        n++;
    }
    for (uint8_t i = 0; i < n; i++) { out[i] = tmp[n - 1 - i]; if (ks) ks[i] = tk[n - 1 - i]; }
    return n;
}

// ---- the noise floor -------------------------------------------------------------
int8_t noiseAt(uint32_t sec) {
    const float wob = 2.4f * sinf(sec * 0.21f) + 1.3f * sinf(sec * 0.057f + 1.0f);
    const int jit = (int)(mix32(sec * 40503u + 17u) % 5) - 2;
    return (int8_t)lroundf(-112.0f + wob + jit);
}

// ---- the chats ---------------------------------------------------------------------
struct Line { Lora::Proto proto; int32_t at; const char* who; const char* text; uint8_t hops; bool direct; };
const Line LINES[] = {
    { Lora::Proto::MESHTASTIC,      -9000, "YETI",       "anyone else hear that howl?",      0, true  },
    { Lora::Proto::MESHTASTIC,     -74000, "NESSIE",     "relay up on the ridge. 3 bars",    2, false },
    { Lora::Proto::MESHTASTIC,    -200000, "MOTHMAN",    "lights over the creek again",      1, false },
    { Lora::Proto::MESHTASTIC,    -545000, "OGOPOGO",    "who left footprints by the lake",  3, false },
    { Lora::Proto::MESHTASTIC,   -1320000, "YETI",       "bring snacks. lots of snacks",     0, true  },
    { Lora::Proto::MESHCORE,       -21000, "JERSEY DVL", "repeater on the fire tower is up", 0, true  },
    { Lora::Proto::MESHCORE,      -130000, "SKUNK APE",  "smells like rain. or me",          0, true  },
    { Lora::Proto::MESHCORE,      -380000, "CHUPACABRA", "goats accounted for. all 12",      1, false },
    { Lora::Proto::MESHCORE,      -900000, "WENDIGO",    "so cold. who took my coat",        2, false },
    { Lora::Proto::MESHCORE,     -1800000, "SKUNK APE",  "first post on the new mesh!",      0, true  },
};
// The line the packet in the capture brings, one per network.
const Line LIVE[2] = {
    { Lora::Proto::MESHTASTIC, 700, "OGOPOGO",    "trail cam got something. blurry", 2, false },
    { Lora::Proto::MESHCORE,   700, "JERSEY DVL", "who keeps pinging the ridge?",     0, true  },
};
std::vector<Lora::Msgs::Msg> s_sent;   // what the compose line sent, oldest first

// Private messages (Meshtastic DMs) with two of the nodes below: YETI is
// toNode(0)'s number, NESSIE toNode(1)'s. State 2 is delivered, 3 failed.
struct DmLine { uint32_t peer; int32_t at; bool mine; const char* who; const char* text; uint8_t state; };
const DmLine DMS[] = {
    { 0x3b1c9a00u,   -60000, false, "YETI",   "on my way. don't wander off",       0 },
    { 0x3b1c9a00u,  -540000, true,  "YOU",    "yeah. bring the good flashlight",   2 },
    { 0x3b1c9a00u,  -600000, false, "YETI",   "you still up on the ridge?",        0 },
    { 0x3b1c9b11u, -3400000, true,  "YOU",    "is the lake trail flooded?",        3 },
    { 0x3b1c9b11u, -3600000, false, "NESSIE", "swim or hike, your call",           0 },
};
// And one on MeshCore, with JERSEY DVL (its key's first four bytes).
const DmLine MC_DMS[] = {
    { 0x4e0a7f12u,  -240000, false, "JERSEY DVL", "tower repeater is back up",     0 },
    { 0x4e0a7f12u,  -300000, true,  "YOU",        "is the fire tower one down?",   2 },
};

// Where the chats and the nodes count back from: the capture, or on a quiet
// band the moment it went quiet -- a line is a packet, so none is newer.
int64_t anchor() { return (int64_t)s_cfg.captureAt - (int64_t)s_cfg.quietSecs * 1000; }

Lora::Msgs::Msg toMsg(const Line& l) {
    Lora::Msgs::Msg m{};
    m.proto = l.proto;
    m.chan = 0;
    m.port = l.proto == Lora::Proto::MESHTASTIC ? 1 : 5;   // TEXT_MESSAGE_APP, GRP_TXT
    m.ms = (uint32_t)(anchor() + l.at);
    m.rssi = l.direct ? -81 : -96;
    m.snr4 = l.direct ? 22 : -8;
    m.hops = l.hops;
    m.direct = l.direct;
    snprintf(m.sender, sizeof m.sender, "%s", l.who);
    snprintf(m.text, sizeof m.text, "%s", l.text);
    return m;
}

// Every message heard by `now`, newest first.
std::vector<Lora::Msgs::Msg> msgsAt(uint32_t now) {
    std::vector<Lora::Msgs::Msg> v;
    for (const Line& l : LINES) {
        const int64_t t = anchor() + l.at;
        if (t >= 1 && t <= (int64_t)now) v.push_back(toMsg(l));
    }
    if (!s_cfg.quietSecs) {
        const Line& l = LIVE[s_cfg.net ? 1 : 0];
        if ((int64_t)s_cfg.captureAt + l.at <= (int64_t)now) v.push_back(toMsg(l));
    }
    for (const DmLine& d : DMS) {
        const int64_t t = anchor() + d.at;
        if (t < 1 || t > (int64_t)now) continue;
        Lora::Msgs::Msg m{};
        m.proto = Lora::Proto::MESHTASTIC; m.chan = Lora::Msgs::DM_CHAN; m.port = 1;
        m.ms = (uint32_t)t; m.rssi = -84; m.snr4 = 18; m.direct = true;
        m.peer = d.peer; m.mine = d.mine; m.dmState = d.state; m.senderId = d.mine ? 0 : d.peer;
        snprintf(m.sender, sizeof m.sender, "%s", d.who);
        snprintf(m.text, sizeof m.text, "%s", d.text);
        v.push_back(m);
    }
    for (const DmLine& d : MC_DMS) {
        const int64_t t = anchor() + d.at;
        if (t < 1 || t > (int64_t)now) continue;
        Lora::Msgs::Msg m{};
        m.proto = Lora::Proto::MESHCORE; m.chan = Lora::Msgs::DM_CHAN; m.port = 2;
        m.ms = (uint32_t)t; m.rssi = -79; m.snr4 = 24; m.direct = true;
        m.peer = d.peer; m.mine = d.mine; m.dmState = d.state; m.senderId = 0;
        snprintf(m.sender, sizeof m.sender, "%s", d.who);
        snprintf(m.text, sizeof m.text, "%s", d.text);
        v.push_back(m);
    }
    for (auto m : s_sent) {
        if (m.ms > now) continue;
        // A DM typed in the emulator: dots, then its tick three seconds later.
        if (m.chan == Lora::Msgs::DM_CHAN) m.dmState = now - m.ms > 3000 ? 2 : 1;
        v.push_back(m);
    }
    std::stable_sort(v.begin(), v.end(), [](const Lora::Msgs::Msg& a, const Lora::Msgs::Msg& b) { return a.ms > b.ms; });
    return v;
}

// ---- the nodes ---------------------------------------------------------------------
// Miles east and north of the board; pos = false sits on the rim. Direct ones
// were heard off their own transmitter, the rest only through relays.
struct Who { Lora::Proto proto; const char* name; bool pos; float east, north; bool direct; int32_t lastAgo; };
const Who NODES[] = {
    { Lora::Proto::MESHTASTIC, "YETI",        true,   0.35f,  0.50f, true,     9000 },
    { Lora::Proto::MESHTASTIC, "NESSIE",      true,  -1.70f,  2.10f, false,   74000 },
    { Lora::Proto::MESHTASTIC, "OGOPOGO",     true,  -4.60f, -3.90f, false,  540000 },
    { Lora::Proto::MESHTASTIC, "MOTHMAN",     false,  0.00f,  0.00f, false,  200000 },
    { Lora::Proto::MESHCORE,   "JERSEY DVL",  true,   2.90f, -1.30f, true,    21000 },
    { Lora::Proto::MESHCORE,   "CHUPACABRA",  true,   0.80f, -2.00f, false,  380000 },
    { Lora::Proto::MESHCORE,   "SKUNK APE",   false,  0.00f,  0.00f, true,   130000 },
    { Lora::Proto::MESHCORE,   "WENDIGO",     false,  0.00f,  0.00f, false,  900000 },
};
const uint8_t NODE_N = sizeof NODES / sizeof NODES[0];

Lora::Nodes::Node toNode(uint8_t i) {
    const Who& w = NODES[i];
    Lora::Nodes::Node n{};
    n.proto = w.proto;
    n.id = 0x3b1c9a00u + i * 0x111u;
    if (w.proto == Lora::Proto::MESHTASTIC) snprintf(n.tag, sizeof n.tag, "!%08x", (unsigned)n.id);
    else                                    snprintf(n.tag, sizeof n.tag, "%02X Rep", (unsigned)(0x40 + i));
    snprintf(n.name, sizeof n.name, "%s", w.name);
    n.hasPos = w.pos;
    if (w.pos) {
        const double lat0 = HOME_LAT7 / 1e7;
        n.latE7 = HOME_LAT7 + (int32_t)lround(w.north / 69.0 * 1e7);
        n.lonE7 = HOME_LON7 + (int32_t)lround(w.east / (69.0 * cos(lat0 * 0.017453292519943295)) * 1e7);
    }
    n.rssi = w.direct ? (int16_t)(-78 - i * 2) : -200;
    n.snr4 = w.direct ? 20 : 0;
    n.viaRssi = w.direct ? -200 : (int16_t)(-90 - i);
    n.directPackets = w.direct ? (uint16_t)(6 + i) : 0;
    n.viaPackets = w.direct ? 0 : (uint16_t)(3 + i);
    n.packets = n.directPackets + n.viaPackets;
    n.lastMs = (uint32_t)(anchor() - w.lastAgo);
    n.firstMs = n.lastMs > 3000000u ? n.lastMs - 3000000u : 1;
    n.hops = w.direct ? 0 : 2;
    n.hopLimit = 3;
    return n;
}

Lora::Stats s_stats{};

}  // namespace

namespace SimLora {

void begin(const Config& c) {
    s_cfg = c;
    s_sent.clear();
    Lora::setRegion(Lora::REGION_US);   // the T-Deck's band plan
    // The made-up user channels below, in the decoders' real tables too, so
    // the compose line's channel chip (Lora::Chan, the real code) has them
    // to step through. The keys are the same made-up bytes as the list's.
    MeshCore::clearUserChannels();
    Meshtastic::clearUserChannels();
    MeshCore::addChannel("#sasquatch", nullptr);
    MeshCore::addChannel("#bigfoot-sightings", nullptr);
    MeshCore::addChannel("RidgeCrew", "3c910e5d77a2481fb62be0946cd3058e");
    Meshtastic::addChannel("Yowie", "6a1df239805ec7149b42e80d73af26b1");
    step(millis());
}

void step(uint32_t now) {
    nmea(GGA, now);
    nmea(RMC, now);
}

}  // namespace SimLora

// ---- what the page calls -------------------------------------------------------------
namespace Lora {

Mode mode() { return Mode::SURVEY; }

uint16_t rssiTrail(int8_t* out, uint16_t cap) {
    const uint32_t sec = millis() / 1000;
    uint16_t n = TRAIL_CAP < cap ? TRAIL_CAP : cap;
    if (n > sec + 1) n = (uint16_t)(sec + 1);
    for (uint16_t i = 0; i < n; i++) out[i] = noiseAt(sec - (n - 1) + i);
    return n;
}

uint8_t hitTrail(Hit* out, uint8_t cap) { return hitsAt(millis(), out, nullptr, cap); }

const Stats& stats() {
    // The floor: the quietest of the last twenty noise samples.
    int8_t trail[TRAIL_CAP];
    const uint16_t n = rssiTrail(trail, TRAIL_CAP);
    int16_t lo = 0;
    for (uint16_t i = n > 20 ? n - 20 : 0; i < n; i++) if (!lo || trail[i] < lo) lo = trail[i];
    s_stats.noiseDbm = lo;
    return s_stats;
}

bool packetAt(uint16_t idxFromNewest, Packet& out) {
    Hit hits[HIT_CAP]; int64_t ks[HIT_CAP];
    const uint8_t n = hitsAt(millis(), hits, ks, HIT_CAP);
    if (idxFromNewest >= n) return false;
    const Hit& h = hits[n - 1 - idxFromNewest];
    out = Packet{};
    out.ms = h.ms;
    out.rssi = h.rssi;
    out.snr4 = (int8_t)(h.rssi + 120);   // SNR x 4: about 6-12 dB over this floor
    out.proto = h.proto;
    const bool mc = h.proto == Proto::MESHCORE;
    out.freqHz = mc ? 910525000u : 906875000u;
    out.bwKhz10 = mc ? 625 : 2500;
    out.sf = mc ? 7 : 11;
    out.cr = 5;
    out.sync = mc ? 0x12 : 0x2B;
    out.len = 40;
    return true;
}

uint8_t nodeCount() { return NODE_N; }

uint8_t nodeSnapshot(Nodes::Node* out, uint8_t cap, uint8_t from) {
    Nodes::Node all[NODE_N];
    for (uint8_t i = 0; i < NODE_N; i++) all[i] = toNode(i);
    std::stable_sort(all, all + NODE_N, [](const Nodes::Node& a, const Nodes::Node& b) { return a.lastMs > b.lastMs; });
    uint8_t n = 0;
    for (uint8_t i = from; i < NODE_N && n < cap; i++) out[n++] = all[i];
    return n;
}

uint16_t msgCount() { return (uint16_t)msgsAt(millis()).size(); }

bool msgAt(uint16_t idxFromNewest, Msgs::Msg& out) {
    const auto v = msgsAt(millis());
    if (idxFromNewest >= v.size()) { out = Msgs::Msg(); return false; }
    out = v[idxFromNewest];
    return true;
}

uint32_t msgDropped() { return 0; }

bool sendText(Proto p, const char* name, const char* text, int8_t, int16_t chan) {
    if (!text || !*text) return false;
    Msgs::Msg m{};
    m.proto = p;
    m.chan = chan < 0 ? 0 : (uint8_t)chan;
    m.ms = millis();
    m.rssi = 0;
    m.direct = true;
    snprintf(m.sender, sizeof m.sender, "%s", name ? name : "SQUACHY");
    snprintf(m.text, sizeof m.text, "%s", text);
    s_sent.push_back(m);
    return true;
}

uint32_t sendDm(Proto p, uint32_t to, const char* text) {
    if (!to || !text || !*text) return 0;
    Msgs::Msg m{};
    m.proto = p; m.chan = Msgs::DM_CHAN; m.port = p == Proto::MESHCORE ? 2 : 1;
    m.ms = millis(); m.direct = true;
    m.peer = to; m.mine = true; m.pktId = 0x5000u + (uint32_t)s_sent.size(); m.dmState = 1;
    snprintf(m.sender, sizeof m.sender, "YOU");
    snprintf(m.text, sizeof m.text, "%s", text);
    s_sent.push_back(m);
    return m.pktId;
}
uint8_t dmState(uint32_t) { return 2; }
bool    dmHasKey(Proto, uint32_t) { return true; }
void    nodeName(Proto p, uint32_t num, char* out, size_t cap) {
    if (p == Proto::MESHCORE && num == 0x4e0a7f12u) { snprintf(out, cap, "JERSEY DVL"); return; }
    for (uint8_t i = 0; i < NODE_N; i++)
        if (NODES[i].proto == Proto::MESHTASTIC && 0x3b1c9a00u + i * 0x111u == num) { snprintf(out, cap, "%s", NODES[i].name); return; }
    snprintf(out, cap, "!%08lx", (unsigned long)num);
}

}  // namespace Lora

// The one-shot renderer has no LORA CHATS screen; the page only asks it for
// the badge. The browser's T-Deck build (-DSQUACH_LORA) has the real one.
#if !SQUACH_LORA
uint16_t uiLoraChatUnread() { return s_cfg.unread; }
#endif

// ---- what LORA SETTINGS calls ---------------------------------------------------------
// The settings themselves are the real src/settings.cpp over the NVS shim;
// these are the radio's side of the page. The channel list is a made-up one
// in the same shape as the decoders' (MeshCore's Public built in, a hashtag,
// a keyed channel, and a Meshtastic preset that has opened frames), and its
// keys are random-looking bytes, nobody's real secret.
namespace {
struct SimChan { Lora::Proto proto; char name[24]; bool builtIn, derived; uint32_t frames; uint8_t key[16]; uint8_t keyLen; };
std::vector<SimChan> s_chans = {
    { Lora::Proto::MESHCORE,   "Public",     true,  false, 214, { 0x8b,0x33,0x87,0xe9,0xc5,0xcd,0xea,0x6a,0xc9,0xe5,0xed,0xba,0xa1,0x15,0xcd,0x72 }, 16 },
    { Lora::Proto::MESHCORE,   "#sasquatch", false, true,   37, { 0 }, 16 },
    { Lora::Proto::MESHCORE,   "#bigfoot-sightings", false, true, 4, { 0 }, 16 },
    { Lora::Proto::MESHCORE,   "RidgeCrew",  false, false,   9, { 0x3c,0x91,0x0e,0x5d,0x77,0xa2,0x48,0x1f,0xb6,0x2b,0xe0,0x94,0x6c,0xd3,0x05,0x8e }, 16 },
    { Lora::Proto::MESHTASTIC, "LongFast",   true,  false, 512, { 0xd4,0xf1,0xbb,0x3a,0x20,0x29,0x07,0x59,0xf0,0xbc,0xff,0xab,0xcf,0x4e,0x69,0x01 }, 16 },
    { Lora::Proto::MESHTASTIC, "Yowie",      false, false,  21, { 0x6a,0x1d,0xf2,0x39,0x80,0x5e,0xc7,0x14,0x9b,0x42,0xe8,0x0d,0x73,0xaf,0x26,0xb1 }, 16 },
};
}

namespace Lora {

void applyListen(uint8_t) {}
void identityChanged() {}
void clearChats() {}
void forgetNames() {}

uint8_t channelRowCount() { return (uint8_t)s_chans.size(); }
bool channelRow(uint8_t i, ChannelRow& out) {
    if (i >= s_chans.size()) return false;
    const SimChan& c = s_chans[i];
    memset(&out, 0, sizeof out);
    out.proto = c.proto;
    snprintf(out.name, sizeof out.name, "%s", c.name);
    out.frames = c.frames; out.lastMs = c.frames ? 1 : 0;
    out.builtIn = c.builtIn; out.enabled = true; out.derived = c.derived;
    out.keyBits = (uint16_t)(c.keyLen * 8);
    return true;
}
void channelCapacity(uint8_t& mcUsed, uint8_t& mcMax, uint8_t& mtUsed, uint8_t& mtMax) {
    mcUsed = mtUsed = 0;
    for (const SimChan& c : s_chans) if (!c.builtIn) (c.proto == Proto::MESHCORE ? mcUsed : mtUsed)++;
    mcMax = 24; mtMax = 6;
}
bool channelAdd(Proto p, const char* name, const char* key, char* msg, size_t cap) {
    SimChan c{};
    c.proto = p;
    snprintf(c.name, sizeof c.name, "%s", name ? name : "");
    c.derived = p == Proto::MESHCORE && name && name[0] == '#' && !(key && *key);
    c.keyLen = 16;
    for (int k = 0; k < 16; k++) c.key[k] = (uint8_t)(0x5A ^ (k * 37) ^ (key && key[0] ? key[k % strlen(key)] : 0));
    s_chans.push_back(c);
    if (msg && cap) snprintf(msg, cap, "ADDED AND STORED");
    return true;
}
// The row's channel in the decoders' real tables (begin() put them there),
// for the CHANNELS page's SEND tag.
bool channelRowKey(uint8_t row, Proto& proto, uint8_t& idx) {
    if (row >= s_chans.size()) return false;
    const SimChan& c = s_chans[row];
    const int i = c.proto == Proto::MESHCORE ? MeshCore::findChannel(c.name) : Meshtastic::findChannel(c.name);
    if (i < 0) return false;
    proto = c.proto; idx = (uint8_t)i;
    return true;
}

bool channelDropRow(uint8_t row) {
    if (row >= s_chans.size() || s_chans[row].builtIn) return false;
    s_chans.erase(s_chans.begin() + row);
    return true;
}
void channelKeyPreview(uint8_t row, char* out, size_t cap) {
    if (!cap) return;
    out[0] = '\0';
    if (row >= s_chans.size()) return;
    const SimChan& c = s_chans[row];
    if (c.derived) { snprintf(out, cap, "KEY FROM ITS #NAME"); return; }
    char full[40];
    for (int k = 0; k < 16; k++) snprintf(full + 2 * k, 3, "%02x", c.key[k]);
    snprintf(out, cap, "%.4s....%s", full, full + 28);
}

}  // namespace Lora

#if SQUACH_LORA
// The browser's T-Deck build calls the sniffer's start and per-loop tick, as
// the board does. The band needs neither: it is a function of the clock, and
// sim/main_wasm.cpp steps it every frame (SimLora::step).
namespace Lora {
bool begin() { return true; }
void tick(uint32_t) {}
}
#endif
