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

void note(Proto p, uint8_t chan, uint8_t port, const char* sender, uint64_t senderId, const char* text,
          const Packet& pk, uint32_t now, bool direct, uint8_t hops) {
    if (!s_st) return;
    // An empty line is a frame that opened and parsed into nothing worth
    // reading. It would fill the ring with blank rows and push out words.
    if (!text || !text[0]) return;
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
