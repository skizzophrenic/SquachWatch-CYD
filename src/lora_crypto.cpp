// SquachWatch-CYD — AES-128, SHA-256, HMAC, CMAC and the small hashes the
// LoRa decoders lean on. See include/lora_crypto.h for why these are here.
#include "lora_crypto.h"
#include <string.h>

namespace LoraCrypto {

// ---- AES-128 --------------------------------------------------------------
static const uint8_t SBOX[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16 };

static uint8_t s_invSbox[256];
static bool    s_invReady = false;
static void invSboxInit() {
    if (s_invReady) return;
    for (int i = 0; i < 256; i++) s_invSbox[SBOX[i]] = (uint8_t)i;
    s_invReady = true;
}

static inline uint8_t xtime(uint8_t x) { return (uint8_t)((x << 1) ^ ((x & 0x80) ? 0x1b : 0)); }
static inline uint8_t gmul(uint8_t a, uint8_t b) {
    uint8_t p = 0;
    for (int i = 0; i < 8; i++) { if (b & 1) p ^= a; a = xtime(a); b >>= 1; }
    return p;
}

void Aes128::setKey(const uint8_t key[16]) {
    invSboxInit();
    for (int i = 0; i < 4; i++)
        rk[i] = ((uint32_t)key[4*i] << 24) | ((uint32_t)key[4*i+1] << 16) | ((uint32_t)key[4*i+2] << 8) | key[4*i+3];
    uint32_t rcon = 0x01000000u;
    for (int i = 4; i < 44; i++) {
        uint32_t t = rk[i-1];
        if ((i & 3) == 0) {
            t = ((uint32_t)SBOX[(t >> 16) & 0xff] << 24) | ((uint32_t)SBOX[(t >> 8) & 0xff] << 16) |
                ((uint32_t)SBOX[t & 0xff] << 8) | SBOX[(t >> 24) & 0xff];
            t ^= rcon;
            rcon = (uint32_t)xtime((uint8_t)(rcon >> 24)) << 24;
        }
        rk[i] = rk[i-4] ^ t;
    }
}

static inline void addRoundKey(uint8_t s[16], const uint32_t* k) {
    for (int c = 0; c < 4; c++) {
        s[4*c]   ^= (uint8_t)(k[c] >> 24); s[4*c+1] ^= (uint8_t)(k[c] >> 16);
        s[4*c+2] ^= (uint8_t)(k[c] >> 8);  s[4*c+3] ^= (uint8_t)k[c];
    }
}

void Aes128::encryptBlock(const uint8_t in[16], uint8_t out[16]) const {
    uint8_t s[16];
    memcpy(s, in, 16);
    addRoundKey(s, rk);
    for (int round = 1; round <= 10; round++) {
        for (int i = 0; i < 16; i++) s[i] = SBOX[s[i]];
        // ShiftRows: state is column-major, s[4*c + r]
        uint8_t t;
        t = s[1]; s[1] = s[5]; s[5] = s[9]; s[9] = s[13]; s[13] = t;
        t = s[2]; s[2] = s[10]; s[10] = t; t = s[6]; s[6] = s[14]; s[14] = t;
        t = s[15]; s[15] = s[11]; s[11] = s[7]; s[7] = s[3]; s[3] = t;
        if (round != 10) {
            for (int c = 0; c < 4; c++) {
                uint8_t* p = s + 4*c;
                const uint8_t a0 = p[0], a1 = p[1], a2 = p[2], a3 = p[3];
                const uint8_t all = a0 ^ a1 ^ a2 ^ a3;
                p[0] ^= all ^ xtime(a0 ^ a1);
                p[1] ^= all ^ xtime(a1 ^ a2);
                p[2] ^= all ^ xtime(a2 ^ a3);
                p[3] ^= all ^ xtime(a3 ^ a0);
            }
        }
        addRoundKey(s, rk + 4*round);
    }
    memcpy(out, s, 16);
}

void Aes128::decryptBlock(const uint8_t in[16], uint8_t out[16]) const {
    uint8_t s[16];
    memcpy(s, in, 16);
    addRoundKey(s, rk + 40);
    for (int round = 9; round >= 0; round--) {
        // InvShiftRows
        uint8_t t;
        t = s[13]; s[13] = s[9]; s[9] = s[5]; s[5] = s[1]; s[1] = t;
        t = s[2]; s[2] = s[10]; s[10] = t; t = s[6]; s[6] = s[14]; s[14] = t;
        t = s[3]; s[3] = s[7]; s[7] = s[11]; s[11] = s[15]; s[15] = t;
        for (int i = 0; i < 16; i++) s[i] = s_invSbox[s[i]];
        addRoundKey(s, rk + 4*round);
        if (round != 0) {
            for (int c = 0; c < 4; c++) {
                uint8_t* p = s + 4*c;
                const uint8_t a0 = p[0], a1 = p[1], a2 = p[2], a3 = p[3];
                p[0] = gmul(a0,14) ^ gmul(a1,11) ^ gmul(a2,13) ^ gmul(a3,9);
                p[1] = gmul(a0,9)  ^ gmul(a1,14) ^ gmul(a2,11) ^ gmul(a3,13);
                p[2] = gmul(a0,13) ^ gmul(a1,9)  ^ gmul(a2,14) ^ gmul(a3,11);
                p[3] = gmul(a0,11) ^ gmul(a1,13) ^ gmul(a2,9)  ^ gmul(a3,14);
            }
        }
    }
    memcpy(out, s, 16);
}

void ctr(const Aes128& k, const uint8_t nonce[16], uint8_t* buf, size_t len) {
    uint8_t ctr[16], ks[16];
    memcpy(ctr, nonce, 16);
    for (size_t off = 0; off < len; off += 16) {
        k.encryptBlock(ctr, ks);
        const size_t n = (len - off < 16) ? (len - off) : 16;
        for (size_t i = 0; i < n; i++) buf[off + i] ^= ks[i];
        // The block counts up as one big-endian number, last byte first --
        // what mbedtls and rweather's CTR both do, and what Meshtastic runs.
        for (int i = 15; i >= 0 && ++ctr[i] == 0; i--) {}
    }
}

static void shiftLeft1(const uint8_t in[16], uint8_t out[16]) {
    uint8_t carry = 0;
    for (int i = 15; i >= 0; i--) {
        out[i] = (uint8_t)((in[i] << 1) | carry);
        carry = (in[i] & 0x80) ? 1 : 0;
    }
}

void cmac(const Aes128& k, const uint8_t* msg, size_t len, uint8_t mac[16]) {
    uint8_t zero[16] = {0}, L[16], K1[16], K2[16];
    k.encryptBlock(zero, L);
    shiftLeft1(L, K1);  if (L[0] & 0x80)  K1[15] ^= 0x87;
    shiftLeft1(K1, K2); if (K1[0] & 0x80) K2[15] ^= 0x87;

    size_t nBlocks = (len + 15) / 16;
    bool complete = (len > 0) && (len % 16 == 0);
    if (nBlocks == 0) nBlocks = 1;

    uint8_t x[16] = {0}, y[16], last[16];
    for (size_t b = 0; b + 1 < nBlocks; b++) {
        for (int i = 0; i < 16; i++) y[i] = x[i] ^ msg[16*b + i];
        k.encryptBlock(y, x);
    }
    if (complete) {
        for (int i = 0; i < 16; i++) last[i] = msg[16*(nBlocks-1) + i] ^ K1[i];
    } else {
        memset(last, 0, 16);
        const size_t rem = len - 16*(nBlocks-1);
        memcpy(last, msg + 16*(nBlocks-1), rem);
        last[rem] = 0x80;
        for (int i = 0; i < 16; i++) last[i] ^= K2[i];
    }
    for (int i = 0; i < 16; i++) y[i] = x[i] ^ last[i];
    k.encryptBlock(y, mac);
}

// ---- SHA-256 --------------------------------------------------------------
static const uint32_t K256[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2 };

struct Sha256Ctx {
    uint32_t h[8];
    uint8_t  buf[64];
    size_t   bufLen;
    uint64_t total;
};
static inline uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
static void shaBlock(Sha256Ctx& c, const uint8_t* p) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++) w[i] = rd32be(p + 4*i);
    for (int i = 16; i < 64; i++) {
        const uint32_t s0 = rotr(w[i-15], 7) ^ rotr(w[i-15], 18) ^ (w[i-15] >> 3);
        const uint32_t s1 = rotr(w[i-2], 17) ^ rotr(w[i-2], 19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    uint32_t a = c.h[0], b = c.h[1], cc = c.h[2], d = c.h[3], e = c.h[4], f = c.h[5], g = c.h[6], h = c.h[7];
    for (int i = 0; i < 64; i++) {
        const uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        const uint32_t ch = (e & f) ^ (~e & g);
        const uint32_t t1 = h + S1 + ch + K256[i] + w[i];
        const uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        const uint32_t maj = (a & b) ^ (a & cc) ^ (b & cc);
        const uint32_t t2 = S0 + maj;
        h = g; g = f; f = e; e = d + t1; d = cc; cc = b; b = a; a = t1 + t2;
    }
    c.h[0] += a; c.h[1] += b; c.h[2] += cc; c.h[3] += d; c.h[4] += e; c.h[5] += f; c.h[6] += g; c.h[7] += h;
}
static void shaInit(Sha256Ctx& c) {
    static const uint32_t iv[8] = { 0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19 };
    memcpy(c.h, iv, sizeof iv); c.bufLen = 0; c.total = 0;
}
static void shaUpdate(Sha256Ctx& c, const uint8_t* p, size_t n) {
    c.total += n;
    while (n) {
        const size_t take = (64 - c.bufLen < n) ? (64 - c.bufLen) : n;
        memcpy(c.buf + c.bufLen, p, take);
        c.bufLen += take; p += take; n -= take;
        if (c.bufLen == 64) { shaBlock(c, c.buf); c.bufLen = 0; }
    }
}
static void shaFinal(Sha256Ctx& c, uint8_t out[32]) {
    const uint64_t bits = c.total * 8;
    uint8_t pad = 0x80;
    shaUpdate(c, &pad, 1);
    uint8_t z = 0;
    while (c.bufLen != 56) shaUpdate(c, &z, 1);
    uint8_t lenb[8];
    for (int i = 0; i < 8; i++) lenb[i] = (uint8_t)(bits >> (56 - 8*i));
    shaUpdate(c, lenb, 8);
    for (int i = 0; i < 8; i++) {
        out[4*i] = (uint8_t)(c.h[i] >> 24); out[4*i+1] = (uint8_t)(c.h[i] >> 16);
        out[4*i+2] = (uint8_t)(c.h[i] >> 8); out[4*i+3] = (uint8_t)c.h[i];
    }
}

void sha256(const uint8_t* msg, size_t len, uint8_t out[32]) {
    Sha256Ctx c; shaInit(c); shaUpdate(c, msg, len); shaFinal(c, out);
}
void sha256Two(const uint8_t* a, size_t alen, const uint8_t* b, size_t blen, uint8_t out[32]) {
    Sha256Ctx c; shaInit(c); shaUpdate(c, a, alen); shaUpdate(c, b, blen); shaFinal(c, out);
}
void hmacSha256(const uint8_t* key, size_t klen, const uint8_t* msg, size_t len, uint8_t out[32]) {
    uint8_t k[64] = {0};
    if (klen > 64) sha256(key, klen, k); else memcpy(k, key, klen);
    uint8_t ipad[64], opad[64];
    for (int i = 0; i < 64; i++) { ipad[i] = k[i] ^ 0x36; opad[i] = k[i] ^ 0x5c; }
    uint8_t inner[32];
    sha256Two(ipad, 64, msg, len, inner);
    sha256Two(opad, 64, inner, 32, out);
}

// ---- hashes and codings ----------------------------------------------------
uint32_t crc32(const uint8_t* p, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int b = 0; b < 8; b++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
    }
    return ~c;
}

uint16_t crc16ccitt(const uint8_t* p, size_t n, uint16_t seed) {
    uint16_t c = seed;
    for (size_t i = 0; i < n; i++) {
        c ^= (uint16_t)(p[i] << 8);
        for (int b = 0; b < 8; b++) c = (uint16_t)((c & 0x8000) ? ((c << 1) ^ 0x1021) : (c << 1));
    }
    return c;
}

uint32_t djb2(const char* s) {
    uint32_t h = 5381;
    for (; *s; s++) h = h * 33u + (uint8_t)*s;
    return h;
}

void toHex(const uint8_t* p, size_t n, char* out) {
    static const char* d = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) { out[2*i] = d[p[i] >> 4]; out[2*i+1] = d[p[i] & 15]; }
    out[2*n] = '\0';
}

static int hexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
size_t fromHex(const char* s, uint8_t* out, size_t cap) {
    size_t n = 0;
    while (s[0] && s[1] && n < cap) {
        // Spaces and colons between bytes are people's habit; let them through.
        if (*s == ' ' || *s == ':') { s++; continue; }
        const int hi = hexVal(s[0]), lo = hexVal(s[1]);
        if (hi < 0 || lo < 0) return 0;
        out[n++] = (uint8_t)((hi << 4) | lo);
        s += 2;
    }
    return (*s == '\0') ? n : 0;
}

size_t fromBase64(const char* s, uint8_t* out, size_t cap) {
    uint32_t acc = 0; int bits = 0; size_t n = 0;
    for (; *s && *s != '='; s++) {
        int v;
        if (*s >= 'A' && *s <= 'Z') v = *s - 'A';
        else if (*s >= 'a' && *s <= 'z') v = *s - 'a' + 26;
        else if (*s >= '0' && *s <= '9') v = *s - '0' + 52;
        else if (*s == '+' || *s == '-') v = 62;
        else if (*s == '/' || *s == '_') v = 63;
        else return 0;
        acc = (acc << 6) | (uint32_t)v; bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (n >= cap) return 0;
            out[n++] = (uint8_t)(acc >> bits);
        }
    }
    return n;
}

}
