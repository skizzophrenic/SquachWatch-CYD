// SquachWatch-CYD — SquachMesh messages: the pure half.
//
// Everything here is arithmetic over bytes: the word list, rolling a phrase,
// the frame format, text packing, reassembly, replay rejection and counter
// reservation. The cipher is INJECTED (see Crypto), so this compiles and is
// tested on a desktop with no crypto library at all -- which matters, because
// CI builds and tests it on every push and CI has none.
//
// The cipher itself is AES-128-CCM with an 8-byte tag, keyed by
// PBKDF2-HMAC-SHA256 over a five-word phrase. On the device that is mbedtls
// (meshcrypto.cpp). It is pinned to an INDEPENDENT implementation -- Python's
// `cryptography`, via test/gen_meshmsg_vectors.py -- by golden frames the
// device must reproduce byte for byte at boot before it will send or read a
// single message. Two implementations that must agree about a format neither
// owns is a test; one implementation checked against itself is not.
//
// WHERE THIS RIDES: the scan response. The limit that shapes everything below
// is BLE 4.2 legacy advertising -- 31 bytes a packet -- because the ESP32 on
// these boards has no extended advertising. After the packet's own length and
// type bytes and the company ID, a frame gets 27 bytes, and TEXT_FRAME_LEN is
// exactly that.
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
// release changes every group's key and they stop reading each other.
//
// Measured 2026-09-10 on an ESP32 (ST7789 CYD): 2753 ms. Kept rather than
// cut to about a second, because the cost lands only when a phrase is set --
// the derived key is stored and loaded at boot, never re-stretched -- while
// every offline guess at a phrase pays it every time.
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
// Version 2. After the manufacturer company ID (0xFFFF, same as SquachMesh):
//
//   0  2  magic 'S','T'
//   2  1  version (high four bits) and kind (low four)
//   3  3  counter, little-endian -- per sender, never reused under one key
//   6  n  ciphertext
//   6+n 8 tag
//
// Version 1 spent ten bytes on this header, four of them on a magic chosen to
// keep out of v1.5.23's way. Nothing released ever read it, so v2 took the
// bytes back for text: six more characters in every part.
//
// The six header bytes are authenticated as associated data. The nonce is the
// SENDER'S BLUETOOTH ADDRESS, the counter, and four zero bytes: with one key
// shared by a whole group, two devices on the same counter would otherwise
// reuse a nonce, which does not weaken AES-CCM, it ends it. Putting the
// address in the nonce also binds a frame to the device that sent it -- the
// same bytes replayed from any other address fail authentication.
//
// SquachMesh's own advert starts 'S','Q', so the two magics cannot collide.
constexpr uint8_t  MAGIC[2]    = { 'S', 'T' };
constexpr uint8_t  VERSION     = 2;
constexpr uint8_t  KIND_CANNED = 1;
constexpr uint8_t  KIND_TEXT   = 2;
constexpr size_t   HDR_LEN     = 6;
// Three bytes of counter. At three counters a typed message that is sixteen
// million sends: not a limit anybody meets, and a sender refuses rather than
// wraps if one ever does.
constexpr uint32_t COUNTER_MAX = 0xFFFFFFu;

constexpr size_t CANNED_FRAME_LEN = HDR_LEN + 1 + TAG_LEN;   // 15

// ---- typed messages ---------------------------------------------------------
// Up to 48 characters, sent as up to three parts of sixteen. Each part is its
// own sealed frame -- its own counter, its own tag -- with one byte inside the
// ciphertext saying which part it is and how many there are (index in the high
// four bits, total in the low). The parts of one message use consecutive
// counters, so a receiver finds a message's first counter as `counter - index`.
//
// Characters are six-bit codes into TEXT_CHARSET, sixteen to a part in twelve
// bytes. Uppercase, because the display face and both keyboards are.
constexpr uint8_t TEXT_MAX         = 48;
constexpr uint8_t TEXT_PART_CHARS  = 16;
constexpr uint8_t TEXT_PARTS_MAX   = 3;
constexpr size_t  TEXT_PART_BYTES  = TEXT_PART_CHARS * 6 / 8;             // 12
constexpr size_t  TEXT_FRAME_LEN   = HDR_LEN + 1 + TEXT_PART_BYTES + TAG_LEN; // 27
constexpr size_t  FRAME_MAX        = TEXT_FRAME_LEN;
static_assert(TEXT_MAX == TEXT_PART_CHARS * TEXT_PARTS_MAX, "three whole parts");
static_assert(2 + FRAME_MAX <= 29, "a frame and its company ID must fit one legacy AD");

// The characters a message can hold, in code order: code i is TEXT_CHARSET[i].
// Codes may be ADDED at the end once released, never reordered. The code past
// the last character is END, which pads a final part.
extern const char TEXT_CHARSET[];
constexpr uint8_t TEXT_END = 63;
bool    textChar(char c);
// How many parts `s` needs: 1..3, or 0 if it is empty, longer than TEXT_MAX,
// or holds a character outside TEXT_CHARSET.
uint8_t textParts(const char* s);

void   nonceFor(const uint8_t mac[6], uint32_t counter, uint8_t nonce[NONCE_LEN]);
bool   isFrame(const uint8_t* in, size_t len);        // magic only: is it ours at all
// Header only, no crypto: what a receiver checks BEFORE spending a decryption.
bool   parseHeader(const uint8_t* in, size_t len, uint32_t& counter, uint8_t& kind);
size_t sealCanned(const Crypto& c, const uint8_t mac[6], uint32_t counter,
                  uint8_t canned, uint8_t* out, size_t cap);
// Part `part` of `total` of `text` -- which must be exactly textParts(text)
// parts long. Returns the frame length, or 0.
size_t sealTextPart(const Crypto& c, const uint8_t mac[6], uint32_t counter,
                    const char* text, uint8_t part, uint8_t total,
                    uint8_t* out, size_t cap);

enum class Open : uint8_t {
    OK,
    NOT_OURS,       // some other 0xFFFF payload -- leave it alone
    BAD_FORMAT,     // ours, but a version, kind or length this build does not read
    BAD_TAG,        // forged, corrupted, from someone else's group, or the wrong sender
    UNKNOWN_LINE,   // authentic, from a newer build with lines this one lacks
};
Open openCanned(const Crypto& c, const uint8_t mac[6], const uint8_t* in, size_t len,
                uint32_t& counter, uint8_t& canned);
// One part of a typed message. `chars` gets its characters, NUL-terminated and
// stopping at END; a code this build has no character for comes out as '?'.
Open openTextPart(const Crypto& c, const uint8_t mac[6], const uint8_t* in, size_t len,
                  uint32_t& counter, uint8_t& part, uint8_t& total,
                  char chars[TEXT_PART_CHARS + 1]);

// ---- reassembly ----------------------------------------------------------------
// Parts arrive in whatever order the scan catches them, and each is repeated
// for as long as the sender is broadcasting. Only AUTHENTICATED parts are
// added -- a forger cannot fill a slot -- and a message is handed over whole
// or not at all.
struct Assembly {
    static constexpr uint8_t N = 2;
    struct E {
        uint8_t  mac[6];
        uint32_t base;          // the message's first counter
        uint8_t  total, have;   // have: one bit per part received
        char     seg[TEXT_PARTS_MAX][TEXT_PART_CHARS + 1];
        uint32_t used;
        bool     live;
    };
    E        e[N] = {};
    uint32_t stamp = 0;
    // True when this part completes its message: the whole text is in `out`,
    // and `base` is the message's first counter.
    bool add(const uint8_t mac[6], uint32_t counter, uint8_t part, uint8_t total,
             const char* chars, char out[TEXT_MAX + 1], uint32_t& base);
};

// ---- replay -----------------------------------------------------------------
// A sender repeats the same frame for as long as it is broadcasting, so every
// frame arrives dozens of times. Each (sender, counter) is delivered once.
//
// fresh() is checked BEFORE decrypting, and record() only AFTER a message has
// authenticated -- a whole typed message, not one of its parts -- otherwise a
// forger could poison the table with a huge counter and silence a real sender
// for good. A typed message records its LAST counter, so its own parts arriving
// again afterwards are stale.
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
