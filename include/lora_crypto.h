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
// on mbedtls with its own self-test. Most of this only opens what other
// networks publish the keys to, or what the user holds the keys to. The one
// exception is Meshtastic's direct messages (2026-10-23): AES-256-CCM under
// an X25519 key agreement, which seals the owner's own private messages and
// is checked against vectors made by an independent library
// (test/lora_crypto_test.cpp, test/lora_meshtastic_test.cpp).
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

// ---- AES-256 and CCM (Meshtastic's direct messages) -------------------------
struct Aes256 {
    uint32_t rk[60];
    void setKey(const uint8_t key[32]);
    void encryptBlock(const uint8_t in[16], uint8_t out[16]) const;
};
// AES-CCM the way Meshtastic runs it (src/mesh/aes-ccm.cpp there, which is
// wpa_supplicant's): a 13-byte nonce, so a two-byte length (L = 2), an 8-byte
// tag, no associated data. Seal writes len bytes of ciphertext to `out` and
// the tag to `tag`; open writes the plaintext and says whether the tag held.
// `in` and `out` may be the same buffer. len is at most 65535.
void ccmSeal(const Aes256& k, const uint8_t nonce[13], const uint8_t* in, size_t len,
             uint8_t* out, uint8_t tag[8]);
bool ccmOpen(const Aes256& k, const uint8_t nonce[13], const uint8_t* in, size_t len,
             const uint8_t tag[8], uint8_t* out);

// ---- X25519 (RFC 7748) --------------------------------------------------------
// The scalar is clamped here, so a raw 32 random bytes is a private key.
// x25519() returns false when the result is all zero -- the other side's key
// was a low-order point, and nothing sealed with it would be secret.
bool x25519(uint8_t out[32], const uint8_t scalar[32], const uint8_t point[32]);
void x25519Base(uint8_t pub[32], const uint8_t priv[32]);

// ---- SHA-512 and Ed25519 (MeshCore's identities, 2026-10-23) -----------------
struct Sha512Ctx { uint64_t h[8]; uint8_t buf[128]; uint64_t total; size_t fill; };
void sha512Init(Sha512Ctx& c);
void sha512Update(Sha512Ctx& c, const uint8_t* p, size_t n);
void sha512Final(Sha512Ctx& c, uint8_t out[64]);
void sha512(const uint8_t* msg, size_t len, uint8_t out[64]);
// MeshCore keeps a key pair the way orlp's ed25519 does: the 64-byte private
// key is SHA-512 of a 32-byte seed with its first half clamped -- the scalar
// -- and its second half the signing nonce's prefix. The public key is the
// standard Ed25519 one for that seed (RFC 8032).
void ed25519KeyPair(const uint8_t seed[32], uint8_t pub[32], uint8_t prv[64]);
void ed25519Sign(uint8_t sig[64], const uint8_t* msg, size_t len, const uint8_t pub[32], const uint8_t prv[64]);
// orlp's ed25519_key_exchange, MeshCore's shared secret: their Ed25519 public
// key turned into a Montgomery u, (1 + y) / (1 - y), then X25519 with our
// scalar. The same 32 bytes from either end. False for a bad key.
bool ed25519KeyExchange(uint8_t shared[32], const uint8_t theirPub[32], const uint8_t prv[64]);

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
