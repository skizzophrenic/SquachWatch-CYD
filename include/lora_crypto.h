// SquachWatch-CYD — the arithmetic behind the LoRa decoders.
//
// AES-128, SHA-256, HMAC and CMAC, written out here rather than taken from
// mbedtls for one reason: the decoders are tested on a desktop against
// published vectors (test/lora_crypto_test.cpp, test/lora_*_test.cpp), and a
// desktop has no mbedtls. One implementation on both sides means the bytes
// the test checked are the bytes the board runs. Packets are rare and short,
// so software AES costs nothing worth measuring.
//
// This is not the SquachMesh cipher (include/meshcrypto.h); that one stays
// on mbedtls with its own self-test. Nothing here is used to protect anything
// of ours: it only opens what other networks publish the keys to, or what the
// user holds the keys to.
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace LoraCrypto {

// ---- AES-128 --------------------------------------------------------------
struct Aes128 {
    uint32_t rk[44];            // the expanded key
    void setKey(const uint8_t key[16]);
    void encryptBlock(const uint8_t in[16], uint8_t out[16]) const;
    void decryptBlock(const uint8_t in[16], uint8_t out[16]) const;
};

// AES-CTR, standard: the 16-byte block counts up big-endian from the nonce.
// Meshtastic's channel cipher is exactly this with its own nonce. In place.
void ctr(const Aes128& k, const uint8_t nonce[16], uint8_t* buf, size_t len);

// AES-CMAC (RFC 4493), the LoRaWAN MIC's primitive.
void cmac(const Aes128& k, const uint8_t* msg, size_t len, uint8_t mac[16]);

// ---- SHA-256 --------------------------------------------------------------
void sha256(const uint8_t* msg, size_t len, uint8_t out[32]);
// Two pieces, so a caller need not glue buffers together first.
void sha256Two(const uint8_t* a, size_t alen, const uint8_t* b, size_t blen, uint8_t out[32]);
void hmacSha256(const uint8_t* key, size_t klen, const uint8_t* msg, size_t len, uint8_t out[32]);

// ---- small helpers the decoders share ------------------------------------
inline uint16_t rd16le(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
inline uint32_t rd24le(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16); }
inline uint32_t rd32le(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
inline uint16_t rd16be(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }
inline uint32_t rd24be(const uint8_t* p) { return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | (uint32_t)p[2]; }
inline uint32_t rd32be(const uint8_t* p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3]; }
inline void wr32le(uint8_t* p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
inline void wr16le(uint8_t* p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

// CRC-32 (IEEE, reflected), which Meshtastic 2.8 derives node numbers with.
uint32_t crc32(const uint8_t* p, size_t n);
// CRC-16/CCITT-FALSE (poly 0x1021): seed 0xFFFF for UKHAS, 0x0000 for the
// LoRaWAN beacon.
uint16_t crc16ccitt(const uint8_t* p, size_t n, uint16_t seed);
// djb2, which Meshtastic picks its frequency slot with.
uint32_t djb2(const char* s);

// Bytes to hex, lower case, NUL-terminated. `out` needs 2*n+1.
void toHex(const uint8_t* p, size_t n, char* out);
// Hex to bytes. Returns the count written, or 0 on a bad digit or an odd length.
size_t fromHex(const char* s, uint8_t* out, size_t cap);
// Base64, for the keys people paste from Meshtastic ("AQ==") and MeshCore.
size_t fromBase64(const char* s, uint8_t* out, size_t cap);

}
