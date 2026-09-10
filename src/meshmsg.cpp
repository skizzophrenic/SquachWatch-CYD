// SquachWatch-CYD — SquachMesh messages, the pure half. See include/meshmsg.h.
#include "meshmsg.h"

#if SQUACH_MESH
#include <string.h>

namespace MeshMsg {

const char SALT[] = "SquachWatch/msg/v1";

uint16_t pickUniform(uint32_t (*rng)(), uint16_t n) {
    if (n == 0) return 0;
    // The largest multiple of n that fits in 2^32. Anything at or past it is
    // thrown away and drawn again. Done in 64 bits because 2^32 itself does
    // not fit in the type the generator returns.
    const uint64_t lim = 0x100000000ull - (0x100000000ull % n);
    uint32_t r;
    do { r = rng(); } while ((uint64_t)r >= lim);
    return (uint16_t)(r % n);
}

void roll(uint32_t (*rng)(), uint16_t out[PHRASE_WORDS]) {
    for (uint8_t i = 0; i < PHRASE_WORDS; i++) out[i] = pickUniform(rng, WORD_N);
}

size_t phraseText(const uint16_t idx[PHRASE_WORDS], char* out, size_t cap) {
    size_t n = 0;
    for (uint8_t i = 0; i < PHRASE_WORDS; i++) {
        if (idx[i] >= WORD_N) return 0;
        const char* w = WORDS[idx[i]];
        if (i) {
            if (n + 1 >= cap) return 0;
            out[n++] = ' ';
        }
        for (size_t j = 0; w[j]; j++) {
            if (n + 1 >= cap) return 0;
            out[n++] = w[j];
        }
    }
    if (n >= cap) return 0;
    out[n] = '\0';
    return n;
}

uint8_t wordsStartingWith(char c, uint16_t* out, uint8_t cap) {
    uint8_t n = 0;
    for (uint16_t i = 0; i < WORD_N && n < cap; i++)
        if (WORDS[i][0] == c) out[n++] = i;
    return n;
}

void nonceFor(const uint8_t mac[6], uint32_t counter, uint8_t nonce[NONCE_LEN]) {
    memcpy(nonce, mac, 6);
    nonce[6]  = (uint8_t)counter;
    nonce[7]  = (uint8_t)(counter >> 8);
    nonce[8]  = (uint8_t)(counter >> 16);
    nonce[9]  = (uint8_t)(counter >> 24);
    nonce[10] = nonce[11] = nonce[12] = 0;
}

bool isFrame(const uint8_t* in, size_t len) {
    return in && len >= sizeof MAGIC && memcmp(in, MAGIC, sizeof MAGIC) == 0;
}

bool parseHeader(const uint8_t* in, size_t len, uint32_t& counter, uint8_t& kind) {
    if (!isFrame(in, len) || len < HDR_LEN) return false;
    if (in[4] != VERSION) return false;
    kind    = in[5];
    counter = (uint32_t)in[6] | ((uint32_t)in[7] << 8) |
              ((uint32_t)in[8] << 16) | ((uint32_t)in[9] << 24);
    return true;
}

static void writeHeader(uint32_t counter, uint8_t kind, uint8_t* out) {
    memcpy(out, MAGIC, sizeof MAGIC);
    out[4] = VERSION;
    out[5] = kind;
    out[6] = (uint8_t)counter;
    out[7] = (uint8_t)(counter >> 8);
    out[8] = (uint8_t)(counter >> 16);
    out[9] = (uint8_t)(counter >> 24);
}

size_t sealCanned(const Crypto& c, const uint8_t mac[6], uint32_t counter,
                  uint8_t canned, uint8_t* out, size_t cap) {
    if (cap < CANNED_FRAME_LEN || canned >= CANNED_N) return 0;
    writeHeader(counter, KIND_CANNED, out);
    uint8_t nonce[NONCE_LEN];
    nonceFor(mac, counter, nonce);
    if (!c.seal(nonce, out, HDR_LEN, &canned, 1, out + HDR_LEN, out + HDR_LEN + 1)) return 0;
    return CANNED_FRAME_LEN;
}

Open openCanned(const Crypto& c, const uint8_t mac[6], const uint8_t* in, size_t len,
                uint32_t& counter, uint8_t& canned) {
    if (!isFrame(in, len)) return Open::NOT_OURS;
    uint8_t kind;
    if (!parseHeader(in, len, counter, kind)) return Open::BAD_FORMAT;
    // Exact length, not a minimum: bytes after the tag are bytes nothing
    // authenticated, and a format that tolerates them has somewhere to hide.
    if (kind != KIND_CANNED || len != CANNED_FRAME_LEN) return Open::BAD_FORMAT;
    uint8_t nonce[NONCE_LEN];
    nonceFor(mac, counter, nonce);
    uint8_t pt = 0;
    if (!c.open(nonce, in, HDR_LEN, in + HDR_LEN, 1, in + HDR_LEN + 1, &pt)) return Open::BAD_TAG;
    canned = pt;
    return (pt < CANNED_N) ? Open::OK : Open::UNKNOWN_LINE;
}

bool Replay::fresh(const uint8_t mac[6], uint32_t counter) const {
    for (uint8_t i = 0; i < N; i++)
        if (e[i].live && memcmp(e[i].mac, mac, 6) == 0) return counter > e[i].last;
    return true;          // a sender we have not heard from yet
}

void Replay::record(const uint8_t mac[6], uint32_t counter) {
    stamp++;
    uint8_t slot = 0;
    bool found = false;
    for (uint8_t i = 0; i < N; i++)
        if (e[i].live && memcmp(e[i].mac, mac, 6) == 0) { slot = i; found = true; break; }
    if (!found) {
        // An empty slot, else the sender heard from least recently.
        for (uint8_t i = 0; i < N; i++) {
            if (!e[i].live) { slot = i; break; }
            if (e[i].used < e[slot].used) slot = i;
        }
        memcpy(e[slot].mac, mac, 6);
        e[slot].live = true;
        e[slot].last = counter;
    } else if (counter > e[slot].last) {
        e[slot].last = counter;
    }
    e[slot].used = stamp;
}

uint32_t Counter::reserve(uint32_t stored) {
    // The higher of what flash holds and what this run already reserved --
    // normally equal, but taking the maximum means neither can walk the
    // other backwards.
    const uint32_t base = (stored > limit) ? stored : limit;
    next  = base;
    limit = base + BLOCK;
    return limit;
}

} // namespace MeshMsg
#endif // SQUACH_MESH
