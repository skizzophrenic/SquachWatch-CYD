// SquachWatch-CYD — SquachMesh messages at runtime. See include/meshtalk.h.
#include "meshtalk.h"

#if SQUACH_MESH
#include "meshcrypto.h"
#include "settings.h"
#include <Arduino.h>
#include <Preferences.h>
#include <esp_system.h>
#include <string.h>

namespace MeshTalk {
namespace {

Preferences s_prefs;
bool     s_selfTestOk = false;
bool     s_havePhrase = false;
char     s_phrase[MeshMsg::PHRASE_TEXT_MAX] = { 0 };
uint32_t s_deriveMs   = 0;

uint8_t  s_ownMac[6] = { 0 };
bool     s_macSet    = false;

MeshMsg::Counter  s_ctr;
MeshMsg::Replay   s_replay;
MeshMsg::Assembly s_asm;

// What is being broadcast right now, and until when. Thirty seconds is about
// twenty adverts: long enough that a pocket or a wall does not cost the
// message, short enough that the device stops announcing it has something to
// say.
constexpr uint32_t SEND_MS = 30000;
// How long each part of a typed message holds the scan response before the
// next takes over. Just over one advert interval (1500 ms), so every part is
// on the air for at least one advert per turn; three parts come round every
// 4.8 s, six times in the thirty.
constexpr uint32_t PART_MS = 1600;
uint8_t  s_out[MeshMsg::TEXT_PARTS_MAX][MeshMsg::FRAME_MAX];
uint8_t  s_outLen[MeshMsg::TEXT_PARTS_MAX] = { 0 };
uint8_t  s_outN     = 0;
uint32_t s_outStart = 0;
uint32_t s_outUntil = 0;
uint32_t s_outGen   = 0;

Message  s_inbox = {};

// BLE task -> loop task. Single producer, single consumer, so two indices and
// acquire/release ordering are the whole synchronisation -- no lock, and
// nothing the BLE task can block on.
struct Slot { uint8_t mac[6]; uint8_t len; uint8_t data[MeshMsg::FRAME_MAX]; char name[13]; };
constexpr uint32_t RING = 4;
Slot     s_ring[RING];
uint32_t s_head = 0;       // written only by the BLE task
uint32_t s_tail = 0;       // written only by the loop task
// The producer's own memory of what it last queued. A sender repeats its frame
// every advert, so without this the ring would see the same frame every 1.5
// seconds for half a minute.
uint8_t  s_lastQMac[6] = { 0 };
uint8_t  s_lastQ[MeshMsg::FRAME_MAX] = { 0 };
uint8_t  s_lastQLen    = 0;

void arrived(const Slot& s, uint32_t now) {
    s_inbox.have   = true;
    s_inbox.unread = true;
    s_inbox.at     = now;
    memcpy(s_inbox.mac, s.mac, 6);
    const char* from = s.name[0] ? s.name : "SOMEONE";
    size_t i = 0;
    for (; i < sizeof s_inbox.from - 1 && from[i]; i++) s_inbox.from[i] = from[i];
    s_inbox.from[i] = '\0';
    Serial.printf("[meshtalk] message from %s: %s\n", s_inbox.from, lineText(s_inbox));
}

void deliver(const Slot& s, uint32_t now) {
    uint32_t ctr = 0;
    uint8_t kind = 0;
    if (!MeshMsg::parseHeader(s.data, s.len, ctr, kind)) return;
    // Cheap check first: a counter this sender has already used cannot be a
    // new message, so it never costs a decryption.
    if (!s_replay.fresh(s.mac, ctr)) return;

    if (kind == MeshMsg::KIND_CANNED) {
        uint8_t line = 0;
        const MeshMsg::Open r = MeshMsg::openCanned(MeshCrypto::impl(), s.mac,
                                                    s.data, s.len, ctr, line);
        // Anything else is another group's message, or a forgery, or a frame
        // this build cannot read. None of them is recorded, which is what
        // stops a forger poisoning the replay table.
        if (r != MeshMsg::Open::OK && r != MeshMsg::Open::UNKNOWN_LINE) return;
        s_replay.record(s.mac, ctr);
        s_inbox.text        = false;
        s_inbox.unknownLine = (r == MeshMsg::Open::UNKNOWN_LINE);
        s_inbox.canned      = line;
        arrived(s, now);
        return;
    }

    if (kind == MeshMsg::KIND_TEXT) {
        uint8_t part = 0, total = 0;
        char chars[MeshMsg::TEXT_PART_CHARS + 1];
        if (MeshMsg::openTextPart(MeshCrypto::impl(), s.mac, s.data, s.len,
                                  ctr, part, total, chars) != MeshMsg::Open::OK) return;
        char body[MeshMsg::TEXT_MAX + 1];
        uint32_t base = 0;
        // Parts wait here, each already authenticated, until the last one
        // lands. Only then is the message recorded -- by its LAST counter, so
        // its own parts coming round again are stale from then on.
        if (!s_asm.add(s.mac, ctr, part, total, chars, body, base)) return;
        s_replay.record(s.mac, base + total - 1);
        s_inbox.text        = true;
        s_inbox.unknownLine = false;
        memcpy(s_inbox.body, body, sizeof s_inbox.body);
        arrived(s, now);
    }
}

uint32_t hwRandom() { return esp_random(); }

// `n` consecutive counters -- the parts of one message must be, so a receiver
// can find its first -- with the reservation persisted BEFORE any is used. If
// the write fails, the reservation is thrown away rather than used unrecorded:
// a counter NVS does not know was handed out is one a reboot can hand out again.
bool takeCounters(uint8_t n, uint32_t& base) {
    if (s_ctr.needsReserve() || s_ctr.limit - s_ctr.next < n) {
        const uint32_t hw = s_ctr.reserve(s_prefs.getUInt("ctr", 0));
        if (s_prefs.putUInt("ctr", hw) == 0) { s_ctr = MeshMsg::Counter(); return false; }
    }
    base = s_ctr.next;
    // Three bytes on the air. Refuse rather than wrap: a wrapped counter is a
    // reused nonce.
    if (base + n - 1 > MeshMsg::COUNTER_MAX) return false;
    for (uint8_t i = 0; i < n; i++) s_ctr.take();
    return true;
}

Send checks() {
    if (!ready()) return Send::NOT_READY;
    // Replying needs the radio on. The consent gate lives in meshTransmit(),
    // so a device that never agreed to transmit cannot send a message either.
    if (!Settings::meshTransmit()) return Send::TRANSMIT_OFF;
    if (!s_macSet) return Send::FAILED;          // radio not up yet
    return Send::OK;
}

void onAir(uint8_t n, uint32_t now) {
    s_outN     = n;
    s_outStart = now;
    s_outUntil = now + SEND_MS;
    s_outGen++;
}

} // namespace

void begin() {
    s_prefs.begin("meshtalk", false);

    // Before any key is loaded -- the self-test keys the cipher with its own
    // test key and leaves it unkeyed afterwards.
    s_selfTestOk = MeshCrypto::selfTest();
    Serial.printf("[meshtalk] crypto self-test %s\n", s_selfTestOk ? "PASS" : "FAIL");

    memset(s_phrase, 0, sizeof s_phrase);
    s_prefs.getString("phrase", s_phrase, sizeof s_phrase);
    s_phrase[sizeof s_phrase - 1] = '\0';
    uint8_t key[MeshMsg::KEY_LEN];
    const size_t kl = s_prefs.getBytes("key", key, sizeof key);
    // The stored key, not a re-derived one: stretching takes seconds and boot
    // has better things to do with them.
    s_havePhrase = s_selfTestOk && s_phrase[0] && kl == sizeof key &&
                   MeshCrypto::impl().setKey(key);
}

bool        selfTestOk()   { return s_selfTestOk; }
bool        havePhrase()   { return s_havePhrase; }
const char* phrase()       { return s_havePhrase ? s_phrase : ""; }
uint32_t    lastDeriveMs() { return s_deriveMs; }

void rollPhrase(uint16_t out[MeshMsg::PHRASE_WORDS]) { MeshMsg::roll(hwRandom, out); }

bool setPhrase(const char* text) {
    if (!s_selfTestOk || !text || !text[0]) return false;
    const size_t n = strlen(text);
    if (n >= sizeof s_phrase) return false;

    uint8_t key[MeshMsg::KEY_LEN];
    const uint32_t t0 = millis();
    if (!MeshCrypto::impl().derive(text, n, (const uint8_t*)MeshMsg::SALT,
                                   strlen(MeshMsg::SALT), MeshMsg::ITERS, key)) return false;
    s_deriveMs = millis() - t0;
    Serial.printf("[meshtalk] key stretched in %lu ms (%lu rounds)\n",
                  (unsigned long)s_deriveMs, (unsigned long)MeshMsg::ITERS);
    if (!MeshCrypto::impl().setKey(key)) return false;

    s_prefs.putString("phrase", text);
    s_prefs.putBytes("key", key, sizeof key);
    memcpy(s_phrase, text, n + 1);
    s_havePhrase = true;
    // A different group: counters and half-heard messages under the old key
    // mean nothing now.
    s_replay = MeshMsg::Replay();
    s_asm    = MeshMsg::Assembly();
    return true;
}

void clearPhrase() {
    s_prefs.remove("phrase");
    s_prefs.remove("key");
    memset(s_phrase, 0, sizeof s_phrase);
    s_havePhrase = false;
    s_outN = 0;
    s_outGen++;                // the radio drops the scan response on its next tick
}

bool ready() {
    return Settings::messagesOn() && s_havePhrase && s_selfTestOk;
}

Send send(uint8_t canned, uint32_t now) {
    const Send ok = checks();
    if (ok != Send::OK) return ok;
    uint32_t c = 0;
    if (!takeCounters(1, c)) return Send::FAILED;
    const size_t n = MeshMsg::sealCanned(MeshCrypto::impl(), s_ownMac, c, canned,
                                         s_out[0], sizeof s_out[0]);
    if (n == 0) return Send::FAILED;
    s_outLen[0] = (uint8_t)n;
    onAir(1, now);
    Serial.printf("[meshtalk] sending #%lu: %s\n", (unsigned long)c, MeshMsg::CANNED[canned]);
    return Send::OK;
}

Send sendText(const char* text, uint32_t now) {
    const Send ok = checks();
    if (ok != Send::OK) return ok;
    const uint8_t total = MeshMsg::textParts(text);
    if (total == 0) return Send::FAILED;
    uint32_t base = 0;
    if (!takeCounters(total, base)) return Send::FAILED;
    for (uint8_t p = 0; p < total; p++) {
        const size_t n = MeshMsg::sealTextPart(MeshCrypto::impl(), s_ownMac, base + p, text,
                                               p, total, s_out[p], sizeof s_out[p]);
        if (n == 0) { s_outN = 0; return Send::FAILED; }
        s_outLen[p] = (uint8_t)n;
    }
    onAir(total, now);
    Serial.printf("[meshtalk] sending #%lu (%u parts): %s\n",
                  (unsigned long)base, (unsigned)total, text);
    return Send::OK;
}

const uint8_t* outgoing(uint32_t now, size_t& len, uint32_t& gen) {
    if (s_outN && (int32_t)(now - s_outUntil) < 0) {
        const uint8_t p = (uint8_t)(((now - s_outStart) / PART_MS) % s_outN);
        len = s_outLen[p];
        // Never 0 while something is on the air: s_outGen is at least 1 by
        // the time anything has been sent.
        gen = (s_outGen << 2) | p;
        return s_out[p];
    }
    len = 0;
    gen = 0;
    return nullptr;
}

bool sending(uint32_t now) {
    size_t l;
    uint32_t g;
    return outgoing(now, l, g) != nullptr;
}

void setOwnMac(const uint8_t mac[6]) {
    memcpy(s_ownMac, mac, 6);
    s_macSet = true;
}

void onFrame(const uint8_t mac[6], const uint8_t* d, size_t len, const char* name) {
    if (len == 0 || len > sizeof(Slot::data)) return;
    if (len == s_lastQLen && memcmp(mac, s_lastQMac, 6) == 0 && memcmp(d, s_lastQ, len) == 0) return;
    const uint32_t h = __atomic_load_n(&s_head, __ATOMIC_RELAXED);
    const uint32_t t = __atomic_load_n(&s_tail, __ATOMIC_ACQUIRE);
    if (h - t >= RING) return;          // full -- the sender repeats, it will be back
    Slot& s = s_ring[h % RING];
    memcpy(s.mac, mac, 6);
    s.len = (uint8_t)len;
    memcpy(s.data, d, len);
    size_t i = 0;
    if (name) for (; i < sizeof s.name - 1 && name[i]; i++) s.name[i] = name[i];
    s.name[i] = '\0';
    __atomic_store_n(&s_head, h + 1, __ATOMIC_RELEASE);
    memcpy(s_lastQMac, mac, 6);
    memcpy(s_lastQ, d, len);
    s_lastQLen = (uint8_t)len;
}

void tick(uint32_t now) {
    uint32_t t = __atomic_load_n(&s_tail, __ATOMIC_RELAXED);
    const uint32_t h = __atomic_load_n(&s_head, __ATOMIC_ACQUIRE);
    while (t != h) {
        // Dropped unread when messages are off or there is no key -- the
        // ring still drains, so switching on later does not replay a backlog.
        if (ready()) deliver(s_ring[t % RING], now);
        t++;
        __atomic_store_n(&s_tail, t, __ATOMIC_RELEASE);
    }
}

const Message& inbox() { return s_inbox; }
void markRead() { s_inbox.unread = false; }

const char* lineText(const Message& m) {
    if (m.text) return m.body;
    if (m.unknownLine) return "(a line this build doesn't know)";
    return MeshMsg::CANNED[m.canned < MeshMsg::CANNED_N ? m.canned : 0];
}

} // namespace MeshTalk
#endif // SQUACH_MESH
