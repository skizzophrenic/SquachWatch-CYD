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

// The cipher rounds, shared by both key sizes: AES-128 runs 10, AES-256 14.
static void encryptRounds(const uint32_t* rk, int rounds, const uint8_t in[16], uint8_t out[16]);

void Aes128::encryptBlock(const uint8_t in[16], uint8_t out[16]) const { encryptRounds(rk, 10, in, out); }

void Aes256::setKey(const uint8_t key[32]) {
    for (int i = 0; i < 8; i++)
        rk[i] = ((uint32_t)key[4*i] << 24) | ((uint32_t)key[4*i+1] << 16) | ((uint32_t)key[4*i+2] << 8) | key[4*i+3];
    uint32_t rcon = 0x01000000u;
    for (int i = 8; i < 60; i++) {
        uint32_t t = rk[i-1];
        if ((i & 7) == 0) {
            t = ((uint32_t)SBOX[(t >> 16) & 0xff] << 24) | ((uint32_t)SBOX[(t >> 8) & 0xff] << 16) |
                ((uint32_t)SBOX[t & 0xff] << 8) | SBOX[(t >> 24) & 0xff];
            t ^= rcon;
            rcon = (uint32_t)xtime((uint8_t)(rcon >> 24)) << 24;
        } else if ((i & 7) == 4) {
            t = ((uint32_t)SBOX[(t >> 24) & 0xff] << 24) | ((uint32_t)SBOX[(t >> 16) & 0xff] << 16) |
                ((uint32_t)SBOX[(t >> 8) & 0xff] << 8) | SBOX[t & 0xff];
        }
        rk[i] = rk[i-8] ^ t;
    }
}

void Aes256::encryptBlock(const uint8_t in[16], uint8_t out[16]) const { encryptRounds(rk, 14, in, out); }

static void encryptRounds(const uint32_t* rk, int rounds, const uint8_t in[16], uint8_t out[16]) {
    uint8_t s[16];
    memcpy(s, in, 16);
    addRoundKey(s, rk);
    for (int round = 1; round <= rounds; round++) {
        for (int i = 0; i < 16; i++) s[i] = SBOX[s[i]];
        // ShiftRows: state is column-major, s[4*c + r]
        uint8_t t;
        t = s[1]; s[1] = s[5]; s[5] = s[9]; s[9] = s[13]; s[13] = t;
        t = s[2]; s[2] = s[10]; s[10] = t; t = s[6]; s[6] = s[14]; s[14] = t;
        t = s[15]; s[15] = s[11]; s[11] = s[7]; s[7] = s[3]; s[3] = t;
        if (round != rounds) {
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

// ---- AES-CCM, L = 2, M = 8, no associated data ------------------------------
// RFC 3610's construction with the fields Meshtastic fixes. B0 is the flags
// byte (no AAD; (M-2)/2 = 3 in bits 3-5; L-1 = 1), the nonce, the length
// big-endian; the MAC is CBC over B0 then the zero-padded message. The
// counter blocks are flags L-1, the nonce, the block number big-endian, and
// block 0's keystream masks the tag.
static void ccmMac(const Aes256& k, const uint8_t nonce[13], const uint8_t* msg, size_t len, uint8_t x[16]) {
    uint8_t b[16];
    b[0] = (uint8_t)((((8 - 2) / 2) << 3) | (2 - 1));
    memcpy(b + 1, nonce, 13);
    b[14] = (uint8_t)(len >> 8); b[15] = (uint8_t)len;
    k.encryptBlock(b, x);
    for (size_t off = 0; off < len; off += 16) {
        const size_t n = (len - off < 16) ? (len - off) : 16;
        for (size_t i = 0; i < n; i++) x[i] ^= msg[off + i];
        k.encryptBlock(x, x);
    }
}
static void ccmCtr(const Aes256& k, const uint8_t nonce[13], const uint8_t* in, size_t len, uint8_t* out,
                   uint8_t s0[16]) {
    uint8_t a[16], ks[16];
    a[0] = (uint8_t)(2 - 1);
    memcpy(a + 1, nonce, 13);
    a[14] = a[15] = 0;
    k.encryptBlock(a, s0);
    uint16_t ctrN = 1;
    for (size_t off = 0; off < len; off += 16, ctrN++) {
        a[14] = (uint8_t)(ctrN >> 8); a[15] = (uint8_t)ctrN;
        k.encryptBlock(a, ks);
        const size_t n = (len - off < 16) ? (len - off) : 16;
        for (size_t i = 0; i < n; i++) out[off + i] = in[off + i] ^ ks[i];
    }
}
void ccmSeal(const Aes256& k, const uint8_t nonce[13], const uint8_t* in, size_t len,
             uint8_t* out, uint8_t tag[8]) {
    uint8_t x[16], s0[16];
    ccmMac(k, nonce, in, len, x);      // over the plaintext, before `out` (maybe `in`) is overwritten
    ccmCtr(k, nonce, in, len, out, s0);
    for (int i = 0; i < 8; i++) tag[i] = x[i] ^ s0[i];
}
bool ccmOpen(const Aes256& k, const uint8_t nonce[13], const uint8_t* in, size_t len,
             const uint8_t tag[8], uint8_t* out) {
    uint8_t x[16], s0[16];
    ccmCtr(k, nonce, in, len, out, s0);
    ccmMac(k, nonce, out, len, x);
    uint8_t diff = 0;
    for (int i = 0; i < 8; i++) diff |= (uint8_t)(x[i] ^ s0[i] ^ tag[i]);
    if (diff) { memset(out, 0, len); return false; }
    return true;
}

// ---- X25519 -------------------------------------------------------------------
// TweetNaCl's field arithmetic (public domain): sixteen 16-bit limbs held in
// int64, a Montgomery ladder in constant time. Slow next to a tuned version
// -- tens of milliseconds on the S3 -- and run once per contact, because the
// key it gives is kept (Lora::Dm).
typedef int64_t Gf[16];
static void car25519(Gf o) {
    for (int i = 0; i < 16; i++) {
        o[i] += (int64_t)1 << 16;
        const int64_t c = o[i] >> 16;
        o[(i + 1) * (i < 15)] += c - 1 + 37 * (c - 1) * (i == 15);
        o[i] -= c * 65536;
    }
}
static void sel25519(Gf p, Gf q, int b) {
    const int64_t c = ~((int64_t)b - 1);
    for (int i = 0; i < 16; i++) { const int64_t t = c & (p[i] ^ q[i]); p[i] ^= t; q[i] ^= t; }
}
static void pack25519(uint8_t o[32], const Gf n) {
    Gf m, t;
    for (int i = 0; i < 16; i++) t[i] = n[i];
    car25519(t); car25519(t); car25519(t);
    for (int j = 0; j < 2; j++) {
        m[0] = t[0] - 0xffed;
        for (int i = 1; i < 15; i++) {
            m[i] = t[i] - 0xffff - ((m[i-1] >> 16) & 1);
            m[i-1] &= 0xffff;
        }
        m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1);
        const int b = (int)((m[15] >> 16) & 1);
        m[14] &= 0xffff;
        sel25519(t, m, 1 - b);
    }
    for (int i = 0; i < 16; i++) { o[2*i] = (uint8_t)(t[i] & 0xff); o[2*i+1] = (uint8_t)(t[i] >> 8); }
}
static void unpack25519(Gf o, const uint8_t n[32]) {
    for (int i = 0; i < 16; i++) o[i] = n[2*i] + ((int64_t)n[2*i+1] << 8);
    o[15] &= 0x7fff;
}
static void fAdd(Gf o, const Gf a, const Gf b) { for (int i = 0; i < 16; i++) o[i] = a[i] + b[i]; }
static void fSub(Gf o, const Gf a, const Gf b) { for (int i = 0; i < 16; i++) o[i] = a[i] - b[i]; }
static void fMul(Gf o, const Gf a, const Gf b) {
    int64_t t[31];
    for (int i = 0; i < 31; i++) t[i] = 0;
    for (int i = 0; i < 16; i++) for (int j = 0; j < 16; j++) t[i+j] += a[i] * b[j];
    for (int i = 0; i < 15; i++) t[i] += 38 * t[i+16];
    for (int i = 0; i < 16; i++) o[i] = t[i];
    car25519(o); car25519(o);
}
static void fSq(Gf o, const Gf a) { fMul(o, a, a); }
static void inv25519(Gf o, const Gf in) {
    Gf c;
    for (int a = 0; a < 16; a++) c[a] = in[a];
    for (int a = 253; a >= 0; a--) {
        fSq(c, c);
        if (a != 2 && a != 4) fMul(c, c, in);
    }
    for (int a = 0; a < 16; a++) o[a] = c[a];
}

bool x25519(uint8_t out[32], const uint8_t scalar[32], const uint8_t point[32]) {
    static const Gf K121665 = {0xDB41, 1};
    uint8_t z[32];
    Gf x, a, b, c, d, e, f;
    for (int i = 0; i < 31; i++) z[i] = scalar[i];
    z[31] = (uint8_t)((scalar[31] & 127) | 64);
    z[0] &= 248;
    unpack25519(x, point);
    for (int i = 0; i < 16; i++) { b[i] = x[i]; d[i] = a[i] = c[i] = 0; }
    a[0] = d[0] = 1;
    for (int i = 254; i >= 0; --i) {
        const int r = (z[i >> 3] >> (i & 7)) & 1;
        sel25519(a, b, r); sel25519(c, d, r);
        fAdd(e, a, c); fSub(a, a, c); fAdd(c, b, d); fSub(b, b, d);
        fSq(d, e); fSq(f, a); fMul(a, c, a); fMul(c, b, e);
        fAdd(e, a, c); fSub(a, a, c); fSq(b, a); fSub(c, d, f);
        fMul(a, c, K121665); fAdd(a, a, d); fMul(c, c, a); fMul(a, d, f);
        fMul(d, b, x); fSq(b, e);
        sel25519(a, b, r); sel25519(c, d, r);
    }
    inv25519(c, c);
    fMul(a, a, c);
    pack25519(out, a);
    uint8_t any = 0;
    for (int i = 0; i < 32; i++) any |= out[i];
    return any != 0;
}

void x25519Base(uint8_t pub[32], const uint8_t priv[32]) {
    static const uint8_t NINE[32] = {9};
    x25519(pub, priv, NINE);
}

// ---- SHA-512 (FIPS 180-4) ------------------------------------------------------
// The constants were generated from their definition (cube and square roots
// of the first primes), not typed: scratchpad sha512_consts.py, 2026-10-23.
static const uint64_t SHA512_K[80] = {
    0x428a2f98d728ae22ull, 0x7137449123ef65cdull, 0xb5c0fbcfec4d3b2full, 0xe9b5dba58189dbbcull,
    0x3956c25bf348b538ull, 0x59f111f1b605d019ull, 0x923f82a4af194f9bull, 0xab1c5ed5da6d8118ull,
    0xd807aa98a3030242ull, 0x12835b0145706fbeull, 0x243185be4ee4b28cull, 0x550c7dc3d5ffb4e2ull,
    0x72be5d74f27b896full, 0x80deb1fe3b1696b1ull, 0x9bdc06a725c71235ull, 0xc19bf174cf692694ull,
    0xe49b69c19ef14ad2ull, 0xefbe4786384f25e3ull, 0x0fc19dc68b8cd5b5ull, 0x240ca1cc77ac9c65ull,
    0x2de92c6f592b0275ull, 0x4a7484aa6ea6e483ull, 0x5cb0a9dcbd41fbd4ull, 0x76f988da831153b5ull,
    0x983e5152ee66dfabull, 0xa831c66d2db43210ull, 0xb00327c898fb213full, 0xbf597fc7beef0ee4ull,
    0xc6e00bf33da88fc2ull, 0xd5a79147930aa725ull, 0x06ca6351e003826full, 0x142929670a0e6e70ull,
    0x27b70a8546d22ffcull, 0x2e1b21385c26c926ull, 0x4d2c6dfc5ac42aedull, 0x53380d139d95b3dfull,
    0x650a73548baf63deull, 0x766a0abb3c77b2a8ull, 0x81c2c92e47edaee6ull, 0x92722c851482353bull,
    0xa2bfe8a14cf10364ull, 0xa81a664bbc423001ull, 0xc24b8b70d0f89791ull, 0xc76c51a30654be30ull,
    0xd192e819d6ef5218ull, 0xd69906245565a910ull, 0xf40e35855771202aull, 0x106aa07032bbd1b8ull,
    0x19a4c116b8d2d0c8ull, 0x1e376c085141ab53ull, 0x2748774cdf8eeb99ull, 0x34b0bcb5e19b48a8ull,
    0x391c0cb3c5c95a63ull, 0x4ed8aa4ae3418acbull, 0x5b9cca4f7763e373ull, 0x682e6ff3d6b2b8a3ull,
    0x748f82ee5defb2fcull, 0x78a5636f43172f60ull, 0x84c87814a1f0ab72ull, 0x8cc702081a6439ecull,
    0x90befffa23631e28ull, 0xa4506cebde82bde9ull, 0xbef9a3f7b2c67915ull, 0xc67178f2e372532bull,
    0xca273eceea26619cull, 0xd186b8c721c0c207ull, 0xeada7dd6cde0eb1eull, 0xf57d4f7fee6ed178ull,
    0x06f067aa72176fbaull, 0x0a637dc5a2c898a6ull, 0x113f9804bef90daeull, 0x1b710b35131c471bull,
    0x28db77f523047d84ull, 0x32caab7b40c72493ull, 0x3c9ebe0a15c9bebcull, 0x431d67c49c100d4cull,
    0x4cc5d4becb3e42b6ull, 0x597f299cfc657e2aull, 0x5fcb6fab3ad6faecull, 0x6c44198c4a475817ull,
};
static const uint64_t SHA512_H0[8] = {
    0x6a09e667f3bcc908ull, 0xbb67ae8584caa73bull, 0x3c6ef372fe94f82bull, 0xa54ff53a5f1d36f1ull,
    0x510e527fade682d1ull, 0x9b05688c2b3e6c1full, 0x1f83d9abfb41bd6bull, 0x5be0cd19137e2179ull,
};
static inline uint64_t rotr64(uint64_t x, int n) { return (x >> n) | (x << (64 - n)); }
static void sha512Block(Sha512Ctx& c, const uint8_t* p) {
    uint64_t w[80];
    for (int i = 0; i < 16; i++) {
        w[i] = 0;
        for (int b = 0; b < 8; b++) w[i] = (w[i] << 8) | p[8 * i + b];
    }
    for (int i = 16; i < 80; i++) {
        const uint64_t s0 = rotr64(w[i-15], 1) ^ rotr64(w[i-15], 8) ^ (w[i-15] >> 7);
        const uint64_t s1 = rotr64(w[i-2], 19) ^ rotr64(w[i-2], 61) ^ (w[i-2] >> 6);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    uint64_t a = c.h[0], b = c.h[1], cc = c.h[2], d = c.h[3], e = c.h[4], f = c.h[5], g = c.h[6], h = c.h[7];
    for (int i = 0; i < 80; i++) {
        const uint64_t S1 = rotr64(e, 14) ^ rotr64(e, 18) ^ rotr64(e, 41);
        const uint64_t ch = (e & f) ^ (~e & g);
        const uint64_t t1 = h + S1 + ch + SHA512_K[i] + w[i];
        const uint64_t S0 = rotr64(a, 28) ^ rotr64(a, 34) ^ rotr64(a, 39);
        const uint64_t mj = (a & b) ^ (a & cc) ^ (b & cc);
        const uint64_t t2 = S0 + mj;
        h = g; g = f; f = e; e = d + t1; d = cc; cc = b; b = a; a = t1 + t2;
    }
    c.h[0] += a; c.h[1] += b; c.h[2] += cc; c.h[3] += d; c.h[4] += e; c.h[5] += f; c.h[6] += g; c.h[7] += h;
}
void sha512Init(Sha512Ctx& c) { for (int i = 0; i < 8; i++) c.h[i] = SHA512_H0[i]; c.total = 0; c.fill = 0; }
void sha512Update(Sha512Ctx& c, const uint8_t* p, size_t n) {
    c.total += n;
    while (n) {
        const size_t take = (128 - c.fill < n) ? 128 - c.fill : n;
        memcpy(c.buf + c.fill, p, take);
        c.fill += take; p += take; n -= take;
        if (c.fill == 128) { sha512Block(c, c.buf); c.fill = 0; }
    }
}
void sha512Final(Sha512Ctx& c, uint8_t out[64]) {
    const uint64_t bits = c.total * 8;
    c.buf[c.fill++] = 0x80;
    if (c.fill > 112) { while (c.fill < 128) c.buf[c.fill++] = 0; sha512Block(c, c.buf); c.fill = 0; }
    while (c.fill < 120) c.buf[c.fill++] = 0;
    for (int i = 0; i < 8; i++) c.buf[120 + i] = (uint8_t)(bits >> (56 - 8 * i));
    sha512Block(c, c.buf);
    for (int i = 0; i < 8; i++) for (int b = 0; b < 8; b++) out[8 * i + b] = (uint8_t)(c.h[i] >> (56 - 8 * b));
}
void sha512(const uint8_t* msg, size_t len, uint8_t out[64]) {
    Sha512Ctx c; sha512Init(c); sha512Update(c, msg, len); sha512Final(c, out);
}

// ---- Ed25519 ------------------------------------------------------------------------
// TweetNaCl again (public domain): extended coordinates, a constant-time
// ladder, the scalar reduced mod L. The point constants were generated
// (scratchpad ed_vectors.py) and match TweetNaCl's own.
static const Gf GF0 = {0}, GF1 = {1};
static const Gf ED_D2 = { 0xf159, 0x26b2, 0x9b94, 0xebd6, 0xb156, 0x8283, 0x149a, 0x00e0, 0xd130, 0xeef3, 0x80f2, 0x198e, 0xfce7, 0x56df, 0xd9dc, 0x2406 };
static const Gf ED_X  = { 0xd51a, 0x8f25, 0x2d60, 0xc956, 0xa7b2, 0x9525, 0xc760, 0x692c, 0xdc5c, 0xfdd6, 0xe231, 0xc0a4, 0x53fe, 0xcd6e, 0x36d3, 0x2169 };
static const Gf ED_Y  = { 0x6658, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666 };
static void fSet(Gf r, const Gf a) { for (int i = 0; i < 16; i++) r[i] = a[i]; }
static int  fPar(const Gf a) { uint8_t d[32]; pack25519(d, a); return d[0] & 1; }
static void edAdd(Gf p[4], Gf q[4]) {
    Gf a, b, c, d, t, e, f, g, h;
    fSub(a, p[1], p[0]); fSub(t, q[1], q[0]); fMul(a, a, t);
    fAdd(b, p[0], p[1]); fAdd(t, q[0], q[1]); fMul(b, b, t);
    fMul(c, p[3], q[3]); fMul(c, c, ED_D2);
    fMul(d, p[2], q[2]); fAdd(d, d, d);
    fSub(e, b, a); fSub(f, d, c); fAdd(g, d, c); fAdd(h, b, a);
    fMul(p[0], e, f); fMul(p[1], h, g); fMul(p[2], g, f); fMul(p[3], e, h);
}
static void edCswap(Gf p[4], Gf q[4], int b) { for (int i = 0; i < 4; i++) sel25519(p[i], q[i], b); }
static void edPack(uint8_t r[32], Gf p[4]) {
    Gf tx, ty, zi;
    inv25519(zi, p[2]);
    fMul(tx, p[0], zi); fMul(ty, p[1], zi);
    pack25519(r, ty);
    r[31] ^= (uint8_t)(fPar(tx) << 7);
}
static void edScalarmult(Gf p[4], Gf q[4], const uint8_t s[32]) {
    fSet(p[0], GF0); fSet(p[1], GF1); fSet(p[2], GF1); fSet(p[3], GF0);
    for (int i = 255; i >= 0; --i) {
        const int b = (s[i / 8] >> (i & 7)) & 1;
        edCswap(p, q, b); edAdd(q, p); edAdd(p, p); edCswap(p, q, b);
    }
}
static void edScalarbase(Gf p[4], const uint8_t s[32]) {
    Gf q[4];
    fSet(q[0], ED_X); fSet(q[1], ED_Y); fSet(q[2], GF1); fMul(q[3], ED_X, ED_Y);
    edScalarmult(p, q, s);
}
static const int64_t ED_L[32] = { 0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58, 0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14,
                                  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x10 };
static void modL(uint8_t r[32], int64_t x[64]) {
    int64_t carry;
    for (int i = 63; i >= 32; --i) {
        carry = 0;
        int j;
        for (j = i - 32; j < i - 12; ++j) {
            x[j] += carry - 16 * x[i] * ED_L[j - (i - 32)];
            carry = (x[j] + 128) >> 8;
            x[j] -= carry * 256;
        }
        x[j] += carry;
        x[i] = 0;
    }
    carry = 0;
    for (int j = 0; j < 32; j++) {
        x[j] += carry - (x[31] >> 4) * ED_L[j];
        carry = x[j] >> 8;
        x[j] &= 255;
    }
    for (int j = 0; j < 32; j++) x[j] -= carry * ED_L[j];
    for (int i = 0; i < 32; i++) { x[i + 1] += x[i] >> 8; r[i] = (uint8_t)(x[i] & 255); }
}
static void reduce64(uint8_t r[64]) {
    int64_t x[64];
    for (int i = 0; i < 64; i++) x[i] = (int64_t)(uint64_t)r[i];
    for (int i = 0; i < 64; i++) r[i] = 0;
    modL(r, x);
}

void ed25519KeyPair(const uint8_t seed[32], uint8_t pub[32], uint8_t prv[64]) {
    sha512(seed, 32, prv);
    prv[0] &= 248; prv[31] &= 63; prv[31] |= 64;
    Gf p[4];
    edScalarbase(p, prv);
    edPack(pub, p);
}

void ed25519Sign(uint8_t sig[64], const uint8_t* msg, size_t len, const uint8_t pub[32], const uint8_t prv[64]) {
    uint8_t r[64], h[64];
    Sha512Ctx c;
    sha512Init(c); sha512Update(c, prv + 32, 32); sha512Update(c, msg, len); sha512Final(c, r);
    reduce64(r);
    Gf p[4];
    edScalarbase(p, r);
    edPack(sig, p);                                   // R
    sha512Init(c); sha512Update(c, sig, 32); sha512Update(c, pub, 32); sha512Update(c, msg, len); sha512Final(c, h);
    reduce64(h);
    int64_t x[64];
    for (int i = 0; i < 64; i++) x[i] = 0;
    for (int i = 0; i < 32; i++) x[i] = (int64_t)(uint64_t)r[i];
    for (int i = 0; i < 32; i++) for (int j = 0; j < 32; j++) x[i + j] += (int64_t)h[i] * (int64_t)prv[j];
    modL(sig + 32, x);                                // S
}

bool ed25519KeyExchange(uint8_t shared[32], const uint8_t theirPub[32], const uint8_t prv[64]) {
    Gf y, one, num, den, u;
    unpack25519(y, theirPub);                         // the top (sign) bit is dropped, as fe_frombytes does
    fSet(one, GF1);
    fAdd(num, one, y);
    fSub(den, one, y);
    inv25519(den, den);
    fMul(u, num, den);
    uint8_t ub[32];
    pack25519(ub, u);
    return x25519(shared, prv, ub);
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
