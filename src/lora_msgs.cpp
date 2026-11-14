// SquachWatch-CYD — the decoded message ring. See include/lora_msgs.h.
#include "lora_msgs.h"
#include <string.h>

namespace Lora {
namespace Msgs {

namespace {

struct Store {
    Msg      m[CAP];
    uint16_t head;    // where the next one goes
    uint16_t held;
    uint32_t total;
    uint32_t dropped;
    // The identities of the last few messages, for note()'s copy check. A
    // flood's copies arrive within seconds of each other; 32 covers a busy
    // channel's ten minutes many times over.
    uint32_t seenKey[32];
    uint32_t seenMs[32];
    uint8_t  seenNext;
};

Store* s_st = nullptr;

// The ring index of the i-th newest, or CAP when there is no such message.
uint16_t newest(uint16_t i) {
    if (!s_st || i >= s_st->held) return CAP;
    return (uint16_t)((s_st->head + CAP - 1 - i) % CAP);
}

// Copy a string into a fixed field, always terminated. strncpy leaves a field
// unterminated when the source fills it exactly, and a 40-byte sender printed
// past its end is how a screen shows the next message's text.
void put(char* dst, size_t cap, const char* src) {
    if (!cap) return;
    if (!src) { dst[0] = '\0'; return; }
    size_t n = 0;
    while (n + 1 < cap && src[n]) { dst[n] = src[n]; n++; }
    dst[n] = '\0';
}

}  // namespace

size_t bytesNeeded() { return sizeof(Store); }

bool begin(void* mem, size_t bytes) {
    if (!mem || bytes < sizeof(Store)) return false;
    s_st = (Store*)mem;
    memset(s_st, 0, sizeof(Store));
    return true;
}

bool ready() { return s_st != nullptr; }

void clear() {
    if (!s_st) return;
    // The counters go too: "256 held, 40 dropped" after a clear would be a
    // report about messages nobody can read any more.
    memset(s_st, 0, sizeof(Store));
}

uint32_t meshtasticKey(uint32_t from, uint32_t packetId) {
    const uint32_t k = (from * 2654435761u) ^ packetId;
    return k ? k : 1;
}

uint32_t meshCoreKey(const char* sender, const char* text, uint32_t timestamp) {
    uint32_t h = 2166136261u;                                  // FNV-1a
    auto mix = [&](const char* s) { for (; s && *s; s++) { h ^= (uint8_t)*s; h *= 16777619u; } h ^= 0xFF; h *= 16777619u; };
    mix(sender);
    mix(text);
    for (int i = 0; i < 4; i++) { h ^= (uint8_t)(timestamp >> (8 * i)); h *= 16777619u; }
    return h ? h : 1;
}

void note(Proto p, uint8_t chan, uint8_t port, const char* sender, uint64_t senderId, const char* text,
          const Packet& pk, uint32_t now, bool direct, uint8_t hops, uint32_t key) {
    if (!s_st) return;
    // An empty line is a frame that opened and parsed into nothing worth
    // reading. It would fill the ring with blank rows and push out words.
    if (!text || !text[0]) return;
    // A relay's copy of something already here, or our own send coming back.
    if (key) {
        for (uint8_t i = 0; i < 32; i++)
            if (s_st->seenKey[i] == key && now - s_st->seenMs[i] < SEEN_MS) return;
        s_st->seenKey[s_st->seenNext] = key;
        s_st->seenMs[s_st->seenNext] = now;
        s_st->seenNext = (uint8_t)((s_st->seenNext + 1) % 32);
    }
    Msg& m = s_st->m[s_st->head];
    memset(&m, 0, sizeof m);
    m.proto = p;
    m.chan = chan;
    m.port = port;
    m.ms = now;
    m.epoch = pk.epoch;
    m.rssi = pk.rssi;
    m.snr4 = pk.snr4;
    m.hops = hops;
    m.direct = direct;
    m.senderId = senderId;
    put(m.sender, sizeof m.sender, sender);
    put(m.text, sizeof m.text, text);
    s_st->head = (uint16_t)((s_st->head + 1) % CAP);
    if (s_st->held < CAP) s_st->held++;
    else                  s_st->dropped++;
    s_st->total++;
}

void noteDm(uint32_t peer, bool mine, uint32_t pktId, uint8_t dmState, const char* sender, const char* text,
            const Packet& pk, uint32_t now, bool direct, uint8_t hops, uint32_t key, Proto p) {
    if (!s_st || !peer) return;
    const uint32_t before = s_st->total;
    // The network's own type: Meshtastic's TEXT port, MeshCore's TXT_MSG.
    note(p, DM_CHAN, p == Proto::MESHCORE ? 2 : 1, sender, mine ? 0 : peer, text, pk, now, direct, hops, key);
    if (s_st->total == before) return;      // a copy, dropped
    Msg& m = s_st->m[(s_st->head + CAP - 1) % CAP];
    m.peer = peer;
    m.mine = mine;
    m.pktId = pktId;
    m.dmState = dmState;
}

bool setDmState(uint32_t pktId, uint8_t dmState) {
    if (!s_st || !pktId) return false;
    for (uint16_t i = 0; i < s_st->held; i++) {
        Msg& m = s_st->m[newest(i)];
        if (m.chan == DM_CHAN && m.mine && m.pktId == pktId) { m.dmState = dmState; return true; }
    }
    return false;
}

uint16_t countForPeer(uint32_t peer) {
    if (!s_st || !peer) return 0;
    uint16_t n = 0;
    for (uint16_t i = 0; i < s_st->held; i++) {
        const Msg& m = s_st->m[newest(i)];
        if (m.chan == DM_CHAN && m.peer == peer) n++;
    }
    return n;
}

uint16_t pageForPeer(uint32_t peer, Msg* out, uint16_t cap, uint16_t from) {
    if (!s_st || !out || !cap || !peer) return 0;
    uint16_t seen = 0, w = 0;
    for (uint16_t i = 0; i < s_st->held && w < cap; i++) {
        const Msg& m = s_st->m[newest(i)];
        if (m.chan != DM_CHAN || m.peer != peer) continue;
        if (seen++ < from) continue;
        out[w++] = m;
    }
    return w;
}

uint16_t dmPeers(uint32_t* out, uint16_t cap) {
    if (!s_st || !out || !cap) return 0;
    uint16_t w = 0;
    for (uint16_t i = 0; i < s_st->held && w < cap; i++) {
        const Msg& m = s_st->m[newest(i)];
        if (m.chan != DM_CHAN || !m.peer) continue;
        bool have = false;
        for (uint16_t j = 0; j < w && !have; j++) have = out[j] == m.peer;
        if (!have) out[w++] = m.peer;
    }
    return w;
}

uint16_t renameSender(Proto p, uint64_t senderId, const char* sender) {
    if (!s_st || !sender || !sender[0]) return 0;
    uint16_t n = 0;
    for (uint16_t i = 0; i < s_st->held; i++) {
        Msg& m = s_st->m[newest(i)];
        if (m.proto != p || m.senderId != senderId || !strcmp(m.sender, sender)) continue;
        put(m.sender, sizeof m.sender, sender);
        n++;
    }
    return n;
}

uint16_t count()   { return s_st ? s_st->held : 0; }
uint32_t total()   { return s_st ? s_st->total : 0; }
uint32_t dropped() { return s_st ? s_st->dropped : 0; }

bool at(uint16_t i, Msg& out) {
    const uint16_t r = newest(i);
    if (r >= CAP) { memset(&out, 0, sizeof out); return false; }
    out = s_st->m[r];
    return true;
}

uint16_t countFor(Proto p, uint8_t chan) {
    if (!s_st) return 0;
    uint16_t n = 0;
    for (uint16_t i = 0; i < s_st->held; i++) {
        const Msg& m = s_st->m[newest(i)];
        if (m.proto == p && m.chan == chan) n++;
    }
    return n;
}

bool atFor(Proto p, uint8_t chan, uint16_t idx, Msg& out) {
    Msg one;
    if (pageFor(p, chan, &one, 1, idx) != 1) { memset(&out, 0, sizeof out); return false; }
    out = one;
    return true;
}

uint16_t pageFor(Proto p, uint8_t chan, Msg* out, uint16_t cap, uint16_t from) {
    if (!s_st || !out || !cap) return 0;
    uint16_t seen = 0, w = 0;
    for (uint16_t i = 0; i < s_st->held && w < cap; i++) {
        const Msg& m = s_st->m[newest(i)];
        if (m.proto != p || m.chan != chan) continue;
        if (seen++ < from) continue;
        out[w++] = m;
    }
    return w;
}

}
}
