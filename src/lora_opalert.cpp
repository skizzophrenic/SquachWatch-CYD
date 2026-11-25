// SquachWatch-CYD — a LoRaWAN operator within earshot. See lora_opalert.h.
#include "lora_opalert.h"
#include <string.h>

namespace Lora {
namespace OpAlert {

namespace {

struct Slot {
    uint32_t netId;
    uint32_t lastMs;
    bool     used;
    bool     counts;     // heard well enough to be reported
};
Slot s_slots[CAP];

Slot* find(uint32_t netId) {
    for (uint8_t i = 0; i < CAP; i++)
        if (s_slots[i].used && s_slots[i].netId == netId) return &s_slots[i];
    return nullptr;
}

// A free slot, else the one unheard for longest. `now - lastMs` rather than a
// comparison of stamps, so it survives millis() wrapping.
Slot* claim(uint32_t now) {
    Slot* oldest = &s_slots[0];
    for (uint8_t i = 0; i < CAP; i++) {
        if (!s_slots[i].used) return &s_slots[i];
        if (now - s_slots[i].lastMs > now - oldest->lastMs) oldest = &s_slots[i];
    }
    return oldest;
}

// The base letter of a Latin-1 Supplement code point (U+00C0..U+00FF), or 0.
char latin1Base(uint8_t cp) {
    static const char UPPER[] = "AAAAAAACEEEEIIIIDNOOOOO\0OUUUUY\0s";   // U+00C0..U+00DF
    static const char LOWER[] = "aaaaaaaceeeeiiiidnooooo\0ouuuuy\0y";   // U+00E0..U+00FF
    if (cp >= 0xC0 && cp <= 0xDF) return UPPER[cp - 0xC0];
    if (cp >= 0xE0) return LOWER[cp - 0xE0];
    return 0;
}

}  // namespace

void macFor(uint32_t netId, uint8_t mac[6]) {
    mac[0] = 0x02; mac[1] = 0x4C; mac[2] = 0x57;
    mac[3] = (uint8_t)(netId >> 16);
    mac[4] = (uint8_t)(netId >> 8);
    mac[5] = (uint8_t)netId;
}

void asciiName(const char* utf8, char* out, size_t cap) {
    if (!out || !cap) return;
    size_t w = 0;
    const uint8_t* p = (const uint8_t*)(utf8 ? utf8 : "");
    while (*p && w + 1 < cap) {
        const uint8_t c = *p++;
        if (c < 0x80) { out[w++] = (char)c; continue; }
        // A two-byte sequence under C3 is U+00C0..U+00FF: the letters with
        // marks on them. Everything else outside ASCII goes, its continuation
        // bytes with it.
        if (c == 0xC3 && (*p & 0xC0) == 0x80) {
            const char b = latin1Base((uint8_t)(0xC0 | (*p & 0x3F)));
            p++;
            if (b) out[w++] = b;
            continue;
        }
        while ((*p & 0xC0) == 0x80) p++;
    }
    out[w] = '\0';
}

bool note(const LoRaWAN::Decoded& d, const Packet& pk, uint32_t now, Hit& out) {
    if (d.isBeacon) return false;
    const uint8_t mt = d.f.mtype;
    if (mt != LoRaWAN::UNCONF_UP && mt != LoRaWAN::UNCONF_DOWN &&
        mt != LoRaWAN::CONF_UP   && mt != LoRaWAN::CONF_DOWN) return false;
    if (!d.net.op) return false;
    if (d.net.netId <= 0x000001) return false;       // private/experimental: nobody's network

    const bool crcOk = (pk.flags & PK_CRC_PRESENT) && (pk.flags & PK_CRC_OK);
    Slot* s = find(d.net.netId);
    if (!s) {
        s = claim(now);
        s->used = true;
        s->netId = d.net.netId;
        s->counts = crcOk;
    } else if (!s->counts) {
        // The second frame. Too long after the first and it is a first again.
        s->counts = crcOk || (now - s->lastMs) <= CONFIRM_MS;
    }
    s->lastMs = now;
    if (!s->counts) return false;

    memset(&out, 0, sizeof out);
    macFor(d.net.netId, out.mac);
    out.netId = d.net.netId;
    asciiName(d.net.op, out.name, sizeof out.name);
    out.rssi  = (int8_t)(pk.rssi < -127 ? -127 : pk.rssi > 0 ? 0 : pk.rssi);
    out.exact = d.net.type < 3;
    return true;
}

void clear() { memset(s_slots, 0, sizeof s_slots); }

}
}
