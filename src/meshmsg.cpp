// SquachWatch-CYD — SquachMesh messages, the pure half. See include/meshmsg.h.
#include "meshmsg.h"

#if SQUACH_MESH
#include <string.h>

namespace MeshMsg {

const char SALT[] = "SquachWatch/msg/v1";

// 43 characters, codes 0..42. See the header before changing a single one.
const char TEXT_CHARSET[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.,?!'-";
static const uint8_t TEXT_CHARSET_N = (uint8_t)(sizeof TEXT_CHARSET - 1);
static_assert(sizeof TEXT_CHARSET - 1 < TEXT_END, "END must stay past the last code");

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

// ---- text ----------------------------------------------------------------------
static int codeOf(char c) {
    for (uint8_t i = 0; i < TEXT_CHARSET_N; i++) if (TEXT_CHARSET[i] == c) return i;
    return -1;
}

bool textChar(char c) { return c && codeOf(c) >= 0; }

uint8_t textParts(const char* s) {
    if (!s || !s[0]) return 0;
    size_t n = 0;
    for (; s[n]; n++) {
        if (n >= TEXT_MAX || !textChar(s[n])) return 0;
    }
    return (uint8_t)((n + TEXT_PART_CHARS - 1) / TEXT_PART_CHARS);
}

// Sixteen six-bit codes into twelve bytes, most significant bit first.
static void pack(const uint8_t codes[TEXT_PART_CHARS], uint8_t out[TEXT_PART_BYTES]) {
    memset(out, 0, TEXT_PART_BYTES);
    for (uint8_t i = 0; i < TEXT_PART_CHARS; i++)
        for (uint8_t b = 0; b < 6; b++)
            if (codes[i] & (0x20 >> b)) {
                const unsigned bit = i * 6u + b;
                out[bit / 8] |= (uint8_t)(0x80 >> (bit % 8));
            }
}

static void unpack(const uint8_t in[TEXT_PART_BYTES], uint8_t codes[TEXT_PART_CHARS]) {
    for (uint8_t i = 0; i < TEXT_PART_CHARS; i++) {
        uint8_t v = 0;
        for (uint8_t b = 0; b < 6; b++) {
            const unsigned bit = i * 6u + b;
            v = (uint8_t)((v << 1) | ((in[bit / 8] >> (7 - bit % 8)) & 1));
        }
        codes[i] = v;
    }
}

// ---- the frame -------------------------------------------------------------------
void nonceFor(const uint8_t mac[6], uint32_t counter, uint8_t nonce[NONCE_LEN]) {
    memcpy(nonce, mac, 6);
    nonce[6] = (uint8_t)counter;
    nonce[7] = (uint8_t)(counter >> 8);
    nonce[8] = (uint8_t)(counter >> 16);
    nonce[9] = nonce[10] = nonce[11] = nonce[12] = 0;
}

bool isFrame(const uint8_t* in, size_t len) {
    return in && len >= sizeof MAGIC && memcmp(in, MAGIC, sizeof MAGIC) == 0;
}

bool parseHeader(const uint8_t* in, size_t len, uint32_t& counter, uint8_t& kind) {
    if (!isFrame(in, len) || len < HDR_LEN) return false;
    if ((in[2] >> 4) != VERSION) return false;
    kind    = in[2] & 0x0F;
    counter = (uint32_t)in[3] | ((uint32_t)in[4] << 8) | ((uint32_t)in[5] << 16);
    return true;
}

static void writeHeader(uint32_t counter, uint8_t kind, uint8_t* out) {
    memcpy(out, MAGIC, sizeof MAGIC);
    out[2] = (uint8_t)((VERSION << 4) | (kind & 0x0F));
    out[3] = (uint8_t)counter;
    out[4] = (uint8_t)(counter >> 8);
    out[5] = (uint8_t)(counter >> 16);
}

size_t sealCanned(const Crypto& c, const uint8_t mac[6], uint32_t counter,
                  uint8_t canned, uint8_t* out, size_t cap) {
    if (cap < CANNED_FRAME_LEN || canned >= CANNED_N || counter > COUNTER_MAX) return 0;
    writeHeader(counter, KIND_CANNED, out);
    uint8_t nonce[NONCE_LEN];
    nonceFor(mac, counter, nonce);
    if (!c.seal(nonce, out, HDR_LEN, &canned, 1, out + HDR_LEN, out + HDR_LEN + 1)) return 0;
    return CANNED_FRAME_LEN;
}

size_t sealTextPart(const Crypto& c, const uint8_t mac[6], uint32_t counter,
                    const char* text, uint8_t part, uint8_t total,
                    uint8_t* out, size_t cap) {
    if (cap < TEXT_FRAME_LEN || counter > COUNTER_MAX) return 0;
    if (total == 0 || textParts(text) != total || part >= total) return 0;
    uint8_t codes[TEXT_PART_CHARS];
    const size_t n = strlen(text), at = (size_t)part * TEXT_PART_CHARS;
    for (uint8_t i = 0; i < TEXT_PART_CHARS; i++)
        codes[i] = (at + i < n) ? (uint8_t)codeOf(text[at + i]) : TEXT_END;
    uint8_t pt[1 + TEXT_PART_BYTES];
    pt[0] = (uint8_t)((part << 4) | total);
    pack(codes, pt + 1);
    writeHeader(counter, KIND_TEXT, out);
    uint8_t nonce[NONCE_LEN];
    nonceFor(mac, counter, nonce);
    if (!c.seal(nonce, out, HDR_LEN, pt, sizeof pt, out + HDR_LEN, out + HDR_LEN + sizeof pt))
        return 0;
    return TEXT_FRAME_LEN;
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

Open openTextPart(const Crypto& c, const uint8_t mac[6], const uint8_t* in, size_t len,
                  uint32_t& counter, uint8_t& part, uint8_t& total,
                  char chars[TEXT_PART_CHARS + 1]) {
    if (!isFrame(in, len)) return Open::NOT_OURS;
    uint8_t kind;
    if (!parseHeader(in, len, counter, kind)) return Open::BAD_FORMAT;
    if (kind != KIND_TEXT || len != TEXT_FRAME_LEN) return Open::BAD_FORMAT;
    uint8_t nonce[NONCE_LEN];
    nonceFor(mac, counter, nonce);
    uint8_t pt[1 + TEXT_PART_BYTES];
    if (!c.open(nonce, in, HDR_LEN, in + HDR_LEN, sizeof pt, in + HDR_LEN + sizeof pt, pt))
        return Open::BAD_TAG;
    part  = pt[0] >> 4;
    total = pt[0] & 0x0F;
    // Authentic, but not a shape this build can place. Refused rather than
    // guessed at: a part put in the wrong slot is a message said wrongly.
    if (total == 0 || total > TEXT_PARTS_MAX || part >= total || counter < part)
        return Open::BAD_FORMAT;
    uint8_t codes[TEXT_PART_CHARS];
    unpack(pt + 1, codes);
    uint8_t n = 0;
    for (; n < TEXT_PART_CHARS && codes[n] != TEXT_END; n++)
        chars[n] = codes[n] < TEXT_CHARSET_N ? TEXT_CHARSET[codes[n]] : '?';
    chars[n] = '\0';
    return Open::OK;
}

// ---- reassembly -----------------------------------------------------------------
bool Assembly::add(const uint8_t mac[6], uint32_t counter, uint8_t part, uint8_t total,
                   const char* chars, char out[TEXT_MAX + 1], uint32_t& base) {
    if (total == 0 || total > TEXT_PARTS_MAX || part >= total || counter < part) return false;
    base = counter - part;
    stamp++;
    // This sender's slot, else an empty one, else the one heard from least
    // recently. One message in flight per sender: a new one replaces it.
    int slot = -1;
    for (uint8_t i = 0; i < N; i++)
        if (e[i].live && memcmp(e[i].mac, mac, 6) == 0) { slot = i; break; }
    if (slot < 0) {
        slot = 0;
        for (uint8_t i = 0; i < N; i++) {
            if (!e[i].live) { slot = i; break; }
            if (e[i].used < e[slot].used) slot = i;
        }
        e[slot].live = false;
    }
    E& s = e[slot];
    if (!s.live || s.base != base || s.total != total) {
        memcpy(s.mac, mac, 6);
        s.base  = base;
        s.total = total;
        s.have  = 0;
        s.live  = true;
    }
    s.used = stamp;
    size_t i = 0;
    for (; i < TEXT_PART_CHARS && chars[i]; i++) s.seg[part][i] = chars[i];
    s.seg[part][i] = '\0';
    s.have |= (uint8_t)(1u << part);
    if (s.have != (uint8_t)((1u << total) - 1)) return false;

    size_t n = 0;
    for (uint8_t p = 0; p < total; p++)
        for (size_t j = 0; s.seg[p][j] && n < TEXT_MAX; j++) out[n++] = s.seg[p][j];
    out[n] = '\0';
    s.live = false;
    return true;
}

// ---- replay and the counter ----------------------------------------------------------
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
