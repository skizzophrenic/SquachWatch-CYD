// SquachWatch-CYD — SquachMesh messages: the pure half.
//
// Everything here is arithmetic over bytes: the word list, rolling a phrase,
// the frame format, replay rejection and counter reservation. The cipher is
// INJECTED (see Crypto), so this compiles and is tested on a desktop with no
// crypto library at all -- which matters, because CI builds and tests it on
// every push and CI has none.
//
// The cipher itself is AES-128-CCM with an 8-byte tag, keyed by
// PBKDF2-HMAC-SHA256 over a five-word phrase. On the device that is mbedtls
// (meshcrypto.cpp). It is pinned to an INDEPENDENT implementation -- Python's
// `cryptography`, via test/gen_meshmsg_vectors.py -- by a golden frame the
// device must reproduce byte for byte at boot before it will send or read a
// single message. Two implementations that must agree about a format neither
// owns is a test; one implementation checked against itself is not.
//
// WHERE THIS RIDES: the scan response, never the advert. v1.5.23 decoders
// refuse an advert with an unknown version or a set reserved flag, so putting
// anything new in the advert would make every existing device stop seeing us
// altogether. They never parse the scan response.
#pragma once
#if SQUACH_MESH
#include <stdint.h>
#include <stddef.h>

namespace MeshMsg {

// ---- the word list (meshwords.cpp) -------------------------------------
constexpr uint8_t PHRASE_WORDS = 5;
constexpr uint8_t WORD_MAX     = 8;
// Five words of up to eight letters, four spaces, one NUL.
constexpr size_t  PHRASE_TEXT_MAX = PHRASE_WORDS * (WORD_MAX + 1);
extern const char* const WORDS[];
extern const uint16_t    WORD_N;

// The canned lines a message can carry, by index. Both ends must agree on
// what index N means, so lines may be ADDED at the end and never reordered or
// removed once a release has shipped them.
extern const char* const CANNED[];
extern const uint8_t     CANNED_N;

// ---- phrases ---------------------------------------------------------------
// A uniform index in [0, n). Rejection sampling rather than `rng() % n`, which
// over-weights the low indices whenever n does not divide 2^32 -- slightly,
// but a slightly predictable phrase generator is the one thing this file must
// not be.
uint16_t pickUniform(uint32_t (*rng)(), uint16_t n);
void     roll(uint32_t (*rng)(), uint16_t out[PHRASE_WORDS]);
// "WORD WORD WORD WORD WORD". Returns the length, or 0 for a bad index or a
// buffer too small -- never a truncated phrase, which would derive a key.
size_t   phraseText(const uint16_t idx[PHRASE_WORDS], char* out, size_t cap);
// The words starting with `c`, as indices, in list order. For the picker.
uint8_t  wordsStartingWith(char c, uint16_t* out, uint8_t cap);

// ---- key stretching ----------------------------------------------------------
extern const char SALT[];
constexpr size_t  KEY_LEN = 16;
// How many PBKDF2 rounds the phrase goes through on its way to a key.
//
// FROZEN AT RELEASE. The key depends on this number, so changing it after a
// release changes every group's key and they stop reading each other. It is a
// first guess at "about a second on the device" and MUST be measured on
// hardware before release -- the serial log prints the time on every derive.
constexpr uint32_t ITERS = 20000;

// ---- the cipher, injected -----------------------------------------------------
constexpr size_t NONCE_LEN = 13;
constexpr size_t TAG_LEN   = 8;
// Stateful by design: setKey() is called once when a key is loaded, and
// seal/open reuse it. Setting a cipher up per message is the same allocation
// churn that was already ripped out of the advertising code once.
struct Crypto {
    bool (*derive)(const char* phrase, size_t phraseLen,
                   const uint8_t* salt, size_t saltLen,
                   uint32_t iters, uint8_t key[KEY_LEN]);
    bool (*setKey)(const uint8_t key[KEY_LEN]);
    bool (*seal)(const uint8_t nonce[NONCE_LEN], const uint8_t* aad, size_t aadLen,
                 const uint8_t* pt, size_t ptLen, uint8_t* ct, uint8_t tag[TAG_LEN]);
    // False on ANY failure, authentication included. `pt` is not to be read.
    bool (*open)(const uint8_t nonce[NONCE_LEN], const uint8_t* aad, size_t aadLen,
                 const uint8_t* ct, size_t ctLen, const uint8_t tag[TAG_LEN], uint8_t* pt);
};

// ---- the frame --------------------------------------------------------------
// After the manufacturer company ID (0xFFFF, same as SquachMesh):
//
//   0  4  magic 'S','Q','M','T'
//   4  1  version
//   5  1  kind (1 = canned line)
//   6  4  counter, little-endian -- per sender, never reused under one key
//  10  n  ciphertext (n = 1 for a canned line)
//  10+n 8 tag
//
// The first ten bytes are authenticated as associated data. The nonce is the
// SENDER'S BLUETOOTH ADDRESS, the counter, and three zero bytes: with one key
// shared by a whole group, two devices on the same counter would otherwise
// reuse a nonce, which does not weaken AES-CCM, it ends it. Putting the
// address in the nonce also binds a frame to the device that sent it -- the
// same bytes replayed from any other address fail authentication.
constexpr uint8_t MAGIC[4] = { 'S', 'Q', 'M', 'T' };
constexpr uint8_t VERSION     = 1;
constexpr uint8_t KIND_CANNED = 1;
constexpr size_t  HDR_LEN          = 10;
constexpr size_t  CANNED_FRAME_LEN = HDR_LEN + 1 + TAG_LEN;   // 19

void   nonceFor(const uint8_t mac[6], uint32_t counter, uint8_t nonce[NONCE_LEN]);
bool   isFrame(const uint8_t* in, size_t len);        // magic only: is it ours at all
// Header only, no crypto: what a receiver checks BEFORE spending a decryption.
bool   parseHeader(const uint8_t* in, size_t len, uint32_t& counter, uint8_t& kind);
size_t sealCanned(const Crypto& c, const uint8_t mac[6], uint32_t counter,
                  uint8_t canned, uint8_t* out, size_t cap);

enum class Open : uint8_t {
    OK,
    NOT_OURS,       // some other 0xFFFF payload -- leave it alone
    BAD_FORMAT,     // ours, but a version, kind or length this build does not read
    BAD_TAG,        // forged, corrupted, from someone else's group, or the wrong sender
    UNKNOWN_LINE,   // authentic, from a newer build with lines this one lacks
};
Open openCanned(const Crypto& c, const uint8_t mac[6], const uint8_t* in, size_t len,
                uint32_t& counter, uint8_t& canned);

// ---- replay -----------------------------------------------------------------
// A sender repeats the same frame for as long as it is broadcasting, so every
// frame arrives dozens of times. Each (sender, counter) is delivered once.
//
// fresh() is checked BEFORE decrypting, and record() only AFTER a frame has
// authenticated -- otherwise a forger could poison the table with a huge
// counter and silence a real sender for good.
//
// Not kept across a reboot. A frame recorded by somebody else and replayed at
// a freshly booted receiver will be shown once; with no shared clock there is
// nothing to reject it on. Documented rather than pretended away.
struct Replay {
    static constexpr uint8_t N = 4;
    struct E { uint8_t mac[6]; uint32_t last; uint32_t used; bool live; };
    E        e[N] = {};
    uint32_t stamp = 0;
    bool fresh(const uint8_t mac[6], uint32_t counter) const;
    void record(const uint8_t mac[6], uint32_t counter);
};

// ---- the counter ----------------------------------------------------------------
// A sender's counter must never repeat under one key -- including across a
// crash, which is the part that is easy to get wrong. Values are reserved from
// NVS a block at a time: the stored number is always ABOVE anything handed out,
// so a crash can skip values but never reuse one.
//
//   if (c.needsReserve()) nvsWrite(c.reserve(nvsRead()));   // persist FIRST
//   use(c.take());
struct Counter {
    static constexpr uint32_t BLOCK = 64;
    uint32_t next = 0, limit = 0;
    bool     needsReserve() const { return next >= limit; }
    uint32_t reserve(uint32_t stored);   // returns the new high-water mark
    uint32_t take() { return next++; }
};

} // namespace MeshMsg
#endif
