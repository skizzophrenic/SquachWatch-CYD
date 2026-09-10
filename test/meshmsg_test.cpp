// SquachMesh messages: everything but the cipher.
//
// The cipher is not here on purpose. CI has no crypto library, and the one the
// device uses cannot be built on a desktop in any form worth trusting. So this
// suite runs the protocol against a TOY AEAD -- one that binds the key, the
// nonce, the associated data and the ciphertext into its tag the way a real one
// does, so every tamper case below behaves as it would on the device -- and the
// real cipher is pinned elsewhere: include/meshmsg_vectors.h holds a frame built
// by an independent implementation (Python's `cryptography`, see
// gen_meshmsg_vectors.py) that the device must reproduce byte for byte at boot.
//
// What this suite CAN check against that golden frame, it does: the header and
// the nonce. Those are the parts no cipher can rescue if they are laid out
// wrong, and the parts where two implementations quietly disagreeing would
// otherwise only show up as two boards that cannot read each other.
#include "meshmsg.h"
#include "meshmsg_vectors.h"
#include "test_util.h"
#include <cstring>
#include <cstdio>

using namespace MeshMsg;

// ---- the toy AEAD ------------------------------------------------------------
static uint8_t s_key[KEY_LEN];
static bool    s_keyed = false;
static uint8_t s_lastNonce[NONCE_LEN];
static uint8_t s_lastAad[32];
static size_t  s_lastAadLen = 0;

static uint64_t fnv(uint64_t h, const uint8_t* p, size_t n) {
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 0x100000001b3ULL; }
    return h;
}
static void toyTag(const uint8_t* nonce, const uint8_t* aad, size_t aadLen,
                   const uint8_t* ct, size_t ctLen, uint8_t tag[TAG_LEN]) {
    uint64_t h = 0xcbf29ce484222325ULL;
    h = fnv(h, s_key, KEY_LEN);
    h = fnv(h, nonce, NONCE_LEN);
    const uint8_t alen = (uint8_t)aadLen;     // length first, so aad||ct cannot be re-split
    h = fnv(h, &alen, 1);
    h = fnv(h, aad, aadLen);
    h = fnv(h, ct, ctLen);
    for (size_t i = 0; i < TAG_LEN; i++) tag[i] = (uint8_t)(h >> (8 * i));
}
static bool toyDerive(const char* p, size_t pl, const uint8_t* s, size_t sl,
                      uint32_t iters, uint8_t key[KEY_LEN]) {
    uint64_t h = fnv(0xcbf29ce484222325ULL, (const uint8_t*)p, pl);
    h = fnv(h, s, sl);
    for (size_t i = 0; i < KEY_LEN; i++)
        key[i] = (uint8_t)((h >> (8 * (i % 8))) ^ i ^ iters);
    return true;
}
static bool toySetKey(const uint8_t key[KEY_LEN]) {
    memcpy(s_key, key, KEY_LEN);
    s_keyed = true;
    return true;
}
static bool toySeal(const uint8_t* nonce, const uint8_t* aad, size_t aadLen,
                    const uint8_t* pt, size_t ptLen, uint8_t* ct, uint8_t* tag) {
    if (!s_keyed) return false;
    memcpy(s_lastNonce, nonce, NONCE_LEN);
    s_lastAadLen = aadLen < sizeof s_lastAad ? aadLen : sizeof s_lastAad;
    memcpy(s_lastAad, aad, s_lastAadLen);
    for (size_t i = 0; i < ptLen; i++) ct[i] = pt[i] ^ s_key[i % KEY_LEN] ^ nonce[i % NONCE_LEN];
    toyTag(nonce, aad, aadLen, ct, ptLen, tag);
    return true;
}
static bool toyOpen(const uint8_t* nonce, const uint8_t* aad, size_t aadLen,
                    const uint8_t* ct, size_t ctLen, const uint8_t* tag, uint8_t* pt) {
    if (!s_keyed) return false;
    uint8_t want[TAG_LEN];
    toyTag(nonce, aad, aadLen, ct, ctLen, want);
    if (memcmp(want, tag, TAG_LEN) != 0) return false;
    for (size_t i = 0; i < ctLen; i++) pt[i] = ct[i] ^ s_key[i % KEY_LEN] ^ nonce[i % NONCE_LEN];
    return true;
}
static const Crypto TOY = { toyDerive, toySetKey, toySeal, toyOpen };

// ---- random sources ---------------------------------------------------------------
static const uint32_t* s_seq;
static size_t s_seqN = 0, s_seqI = 0;
static uint32_t scripted() { return s_seqI < s_seqN ? s_seq[s_seqI++] : 0; }
static uint32_t s_lcgState = 12345;
static uint32_t lcg() { s_lcgState = s_lcgState * 1664525u + 1013904223u; return s_lcgState; }

int main() {
    suite("The word list");
    {
        ck("at least 256 words", WORD_N >= 256);
        bool letters = true, lengths = true, sorted = true;
        for (uint16_t i = 0; i < WORD_N; i++) {
            const size_t n = strlen(WORDS[i]);
            if (n < 3 || n > WORD_MAX) lengths = false;
            for (size_t j = 0; j < n; j++)
                if (WORDS[i][j] < 'A' || WORDS[i][j] > 'Z') letters = false;
            if (i && strcmp(WORDS[i - 1], WORDS[i]) >= 0) sorted = false;
        }
        ck("capital letters only", letters);
        ck("three to eight letters each", lengths);
        ck("in alphabetical order, the order the picker shows", sorted);
        bool prefix = true;
        for (uint16_t i = 0; i < WORD_N; i++)
            for (uint16_t j = i + 1; j < WORD_N; j++)
                if (!strncmp(WORDS[i], WORDS[j], 3)) prefix = false;
        ck("no two words share their first three letters", prefix);

        uint16_t buf[64];
        int total = 0, worst = 0;
        for (char c = 'A'; c <= 'Z'; c++) {
            const int n = wordsStartingWith(c, buf, 64);
            total += n;
            if (n > worst) worst = n;
        }
        ck("every word is under exactly one letter", total == WORD_N);
        ck("no letter has more words than the picker shows (18)", worst <= 18);

        char tmp[64];
        snprintf(tmp, sizeof tmp, "%s", MeshMsgVec::PHRASE);
        bool real = true;
        for (char* w = strtok(tmp, " "); w; w = strtok(nullptr, " ")) {
            bool found = false;
            for (uint16_t i = 0; i < WORD_N; i++) if (!strcmp(WORDS[i], w)) found = true;
            if (!found) real = false;
        }
        ck("the golden phrase is made of real words", real);
        printf("  (%u words: %.1f bits in a five-word phrase)\n",
               (unsigned)WORD_N, 5.0 * log2((double)WORD_N));
    }

    suite("The canned lines");
    {
        ck("at least one, and every index fits a byte", CANNED_N >= 1 && CANNED_N < 255);
        bool fits = true;
        for (uint8_t i = 0; i < CANNED_N; i++) {
            const size_t n = strlen(CANNED[i]);
            if (n == 0 || n > 24) fits = false;
        }
        ck("every line fits one row of one speech bubble", fits);
        ck("the golden line is a real line", MeshMsgVec::CANNED < CANNED_N);
    }

    suite("Rolling is uniform, not merely random");
    {
        // 300 does not divide 2^32, so the top (2^32 mod 300) values must be
        // thrown away, or the low indices come up slightly more often.
        const uint64_t lim = 0x100000000ull - (0x100000000ull % 300);
        const uint32_t seq[] = { 0xFFFFFFFFu, (uint32_t)lim, (uint32_t)(lim - 1) };
        s_seq = seq; s_seqN = 3; s_seqI = 0;
        const uint16_t v = pickUniform(scripted, 300);
        ck("values past the last whole multiple are drawn again", s_seqI == 3);
        ck("and the first acceptable one is used", v == (uint16_t)((lim - 1) % 300));
        const uint32_t seq2[] = { 0xFFFFFFFFu };
        s_seq = seq2; s_seqN = 1; s_seqI = 0;
        ck("a power of two throws nothing away", pickUniform(scripted, 256) == 255 && s_seqI == 1);
    }

    suite("A rolled phrase");
    {
        uint16_t idx[PHRASE_WORDS];
        roll(lcg, idx);
        bool inRange = true;
        for (uint16_t i : idx) if (i >= WORD_N) inRange = false;
        ck("five indices, all inside the list", inRange);

        char text[PHRASE_TEXT_MAX];
        const size_t n = phraseText(idx, text, sizeof text);
        ck("it becomes text", n > 0 && n == strlen(text));
        int spaces = 0;
        bool doubled = false;
        for (size_t i = 0; i < n; i++)
            if (text[i] == ' ') { spaces++; if (i && text[i - 1] == ' ') doubled = true; }
        ck("five words, one space between each", spaces == 4 && !doubled &&
                                                  text[0] != ' ' && text[n - 1] != ' ');

        uint16_t li = 0;
        for (uint16_t i = 0; i < WORD_N; i++) if (strlen(WORDS[i]) > strlen(WORDS[li])) li = i;
        uint16_t longest[PHRASE_WORDS] = { li, li, li, li, li };
        ck("the longest possible phrase fits the buffer", phraseText(longest, text, sizeof text) > 0);
        uint16_t bad[PHRASE_WORDS] = { 0, 0, 0, 0, WORD_N };
        ck("an index past the list is refused", phraseText(bad, text, sizeof text) == 0);
        ck("a buffer too small is refused, never truncated", phraseText(idx, text, 5) == 0);
    }

    suite("Header and nonce agree with an independent implementation");
    {
        uint8_t nonce[NONCE_LEN];
        nonceFor(MeshMsgVec::MAC, MeshMsgVec::COUNTER, nonce);
        ck("the nonce is the golden nonce, byte for byte",
           memcmp(nonce, MeshMsgVec::NONCE, NONCE_LEN) == 0);

        uint8_t k[KEY_LEN] = { 1 };
        TOY.setKey(k);
        uint8_t f[CANNED_FRAME_LEN];
        const size_t n = sealCanned(TOY, MeshMsgVec::MAC, MeshMsgVec::COUNTER,
                                    MeshMsgVec::CANNED, f, sizeof f);
        ck("a canned frame is the golden length", n == sizeof MeshMsgVec::FRAME);
        ck("its header is the golden header, byte for byte",
           memcmp(f, MeshMsgVec::FRAME, HDR_LEN) == 0);
        ck("the cipher was handed exactly that nonce",
           memcmp(s_lastNonce, MeshMsgVec::NONCE, NONCE_LEN) == 0);
        ck("and exactly that header as associated data",
           s_lastAadLen == HDR_LEN && memcmp(s_lastAad, f, HDR_LEN) == 0);
        ck("the salt is the one the vectors were made with",
           strcmp(SALT, MeshMsgVec::SALT) == 0);
    }

    uint8_t key[KEY_LEN];
    for (size_t i = 0; i < KEY_LEN; i++) key[i] = (uint8_t)(0x40 + i);
    const uint8_t me[6]  = { 0x24, 0x0A, 0xC4, 0xAA, 0xBB, 0xCC };
    const uint8_t you[6] = { 0x24, 0x0A, 0xC4, 0x11, 0x22, 0x33 };

    suite("A message round-trips");
    {
        TOY.setKey(key);
        uint8_t f[CANNED_FRAME_LEN];
        ck("it seals", sealCanned(TOY, me, 41, 3, f, sizeof f) == CANNED_FRAME_LEN);
        uint32_t c = 0;
        uint8_t line = 0xFF;
        ck("it opens", openCanned(TOY, me, f, sizeof f, c, line) == Open::OK);
        ck("the counter survives", c == 41);
        ck("the line survives", line == 3);
    }

    suite("Anything tampered with is refused, not misread");
    {
        TOY.setKey(key);
        uint8_t good[CANNED_FRAME_LEN], f[CANNED_FRAME_LEN];
        sealCanned(TOY, me, 41, 3, good, sizeof good);
        uint32_t c;
        uint8_t line;
        memcpy(f, good, sizeof f); f[HDR_LEN] ^= 0x01;
        ck("a flipped ciphertext bit", openCanned(TOY, me, f, sizeof f, c, line) == Open::BAD_TAG);
        memcpy(f, good, sizeof f); f[sizeof f - 1] ^= 0x80;
        ck("a flipped tag bit", openCanned(TOY, me, f, sizeof f, c, line) == Open::BAD_TAG);
        memcpy(f, good, sizeof f); f[6] ^= 0x01;
        ck("a changed counter", openCanned(TOY, me, f, sizeof f, c, line) == Open::BAD_TAG);
        ck("the right frame from the wrong sender",
           openCanned(TOY, you, good, sizeof good, c, line) == Open::BAD_TAG);
        uint8_t other[KEY_LEN];
        memcpy(other, key, KEY_LEN);
        other[0] ^= 1;
        TOY.setKey(other);
        ck("the right frame under the wrong key",
           openCanned(TOY, me, good, sizeof good, c, line) == Open::BAD_TAG);
        TOY.setKey(key);
    }

    suite("Frames that are not ours, or not well formed");
    {
        TOY.setKey(key);
        uint8_t good[CANNED_FRAME_LEN], f[CANNED_FRAME_LEN + 4];
        sealCanned(TOY, me, 7, 1, good, sizeof good);
        uint32_t c;
        uint8_t line;
        memcpy(f, good, sizeof good); f[0] = 'X';
        ck("another magic is not ours", openCanned(TOY, me, f, sizeof good, c, line) == Open::NOT_OURS);
        memcpy(f, good, sizeof good); f[3] = '1';
        ck("SquachMesh's own advert magic is not ours",
           openCanned(TOY, me, f, sizeof good, c, line) == Open::NOT_OURS);
        ck("three bytes is not ours", openCanned(TOY, me, good, 3, c, line) == Open::NOT_OURS);
        memcpy(f, good, sizeof good); f[4] = 2;
        ck("an unknown version is refused", openCanned(TOY, me, f, sizeof good, c, line) == Open::BAD_FORMAT);
        memcpy(f, good, sizeof good); f[5] = 9;
        ck("an unknown kind is refused", openCanned(TOY, me, f, sizeof good, c, line) == Open::BAD_FORMAT);
        ck("a truncated frame is refused",
           openCanned(TOY, me, good, sizeof good - 1, c, line) == Open::BAD_FORMAT);
        memcpy(f, good, sizeof good); f[sizeof good] = 0;
        ck("a trailing byte is refused",
           openCanned(TOY, me, f, sizeof good + 1, c, line) == Open::BAD_FORMAT);
    }

    suite("A line this firmware does not know yet");
    {
        // A newer sender may have more lines than this build. Its frame is
        // authentic; it just cannot be read here -- reported as that, rather
        // than refused as forged or shown as garbage.
        TOY.setKey(key);
        uint8_t f[CANNED_FRAME_LEN];
        memcpy(f, MAGIC, 4);
        f[4] = VERSION; f[5] = KIND_CANNED;
        f[6] = 9; f[7] = f[8] = f[9] = 0;
        uint8_t nonce[NONCE_LEN];
        nonceFor(me, 9, nonce);
        const uint8_t pt = CANNED_N;            // one past this build's last line
        TOY.seal(nonce, f, HDR_LEN, &pt, 1, f + HDR_LEN, f + HDR_LEN + 1);
        uint32_t c;
        uint8_t line;
        ck("it authenticates and is flagged unknown",
           openCanned(TOY, me, f, sizeof f, c, line) == Open::UNKNOWN_LINE);
        ck("and sealCanned will not build one",
           sealCanned(TOY, me, 9, CANNED_N, f, sizeof f) == 0);
    }

    suite("Each counter is delivered once per sender");
    {
        Replay r;
        ck("a sender never heard from is fresh", r.fresh(me, 5));
        r.record(me, 5);
        ck("the same counter again is not", !r.fresh(me, 5));
        ck("an older counter is not", !r.fresh(me, 4));
        ck("a newer counter is", r.fresh(me, 6));
        ck("another sender is judged separately", r.fresh(you, 1));

        // Five senders into four slots: the one heard from longest ago goes.
        uint8_t m[5][6];
        for (int i = 0; i < 5; i++) { memcpy(m[i], me, 6); m[i][5] = (uint8_t)(0x10 + i); }
        Replay q;
        for (int i = 0; i < 5; i++) q.record(m[i], 100);
        ck("the four most recent are remembered", !q.fresh(m[4], 100) && !q.fresh(m[1], 100));
        ck("the oldest was dropped, so it is fresh again", q.fresh(m[0], 100));
    }

    suite("The counter never repeats, even across a crash");
    {
        uint32_t nvs = 0;                       // what flash holds
        uint32_t highest = 0;
        bool any = false, repeat = false;
        uint32_t lc = 777;
        Counter ctr;
        for (int step = 0; step < 1000; step++) {
            lc = lc * 1103515245u + 12345u;
            if ((lc >> 16) % 37 == 0) ctr = Counter();          // crash: RAM gone, flash kept
            if (ctr.needsReserve()) nvs = ctr.reserve(nvs);      // persisted BEFORE use
            const uint32_t v = ctr.take();
            if (any && v <= highest) repeat = true;
            highest = v;
            any = true;
        }
        ck("strictly increasing across a thousand sends and many crashes", !repeat);

        Counter fresh;
        ck("a fresh counter reserves before its first value", fresh.needsReserve());
        const uint32_t hw = fresh.reserve(1000);
        ck("the reservation is one block past what flash held", hw == 1000 + Counter::BLOCK);
        ck("and the first value is what flash held", fresh.take() == 1000);
    }

    return report();
}
