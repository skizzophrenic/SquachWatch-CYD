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

MeshMsg::Counter s_ctr;
MeshMsg::Replay  s_replay;

// What is being broadcast right now, and until when. Thirty seconds is about
// twenty adverts: long enough that a pocket or a wall does not cost the
// message, short enough that the device stops announcing it has something to
// say.
constexpr uint32_t SEND_MS = 30000;
uint8_t  s_out[MeshMsg::CANNED_FRAME_LEN];
size_t   s_outLen   = 0;
uint32_t s_outUntil = 0;
uint32_t s_outGen   = 0;

Message  s_inbox = {};

// BLE task -> loop task. Single producer, single consumer, so two indices and
// acquire/release ordering are the whole synchronisation -- no lock, and
// nothing the BLE task can block on.
struct Slot { uint8_t mac[6]; uint8_t len; uint8_t data[27]; char name[13]; };
constexpr uint32_t RING = 4;
Slot     s_ring[RING];
uint32_t s_head = 0;       // written only by the BLE task
uint32_t s_tail = 0;       // written only by the loop task
// The producer's own memory of what it last queued. A sender repeats its frame
// every advert, so without this the ring would see the same frame every 1.5
// seconds for half a minute.
uint8_t  s_lastQMac[6] = { 0 };
uint8_t  s_lastQ[27]   = { 0 };
uint8_t  s_lastQLen    = 0;

void deliver(const Slot& s, uint32_t now) {
    uint32_t ctr = 0;
    uint8_t kind = 0;
    if (!MeshMsg::parseHeader(s.data, s.len, ctr, kind)) return;
    // Cheap check first: a counter this sender has already used cannot be a
    // new message, so it never costs a decryption.
    if (!s_replay.fresh(s.mac, ctr)) return;
    uint8_t line = 0;
    const MeshMsg::Open r = MeshMsg::openCanned(MeshCrypto::impl(), s.mac,
                                                s.data, s.len, ctr, line);
    // Anything else is another group's message, or a forgery, or a frame
    // this build cannot read. None of them is recorded, which is what stops
    // a forger poisoning the replay table.
    if (r != MeshMsg::Open::OK && r != MeshMsg::Open::UNKNOWN_LINE) return;
    s_replay.record(s.mac, ctr);

    s_inbox.have        = true;
    s_inbox.unread      = true;
    s_inbox.unknownLine = (r == MeshMsg::Open::UNKNOWN_LINE);
    s_inbox.canned      = line;
    s_inbox.at          = now;
    memcpy(s_inbox.mac, s.mac, 6);
    const char* from = s.name[0] ? s.name : "SOMEONE";
    size_t i = 0;
    for (; i < sizeof s_inbox.from - 1 && from[i]; i++) s_inbox.from[i] = from[i];
    s_inbox.from[i] = '\0';
    Serial.printf("[meshtalk] message from %s: %s\n", s_inbox.from, lineText(s_inbox));
}

uint32_t hwRandom() { return esp_random(); }

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
    // The stored key, not a re-derived one: stretching takes about a second
    // and boot has better things to do with it.
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
    // The number ITERS gets tuned against. See the note on it in meshmsg.h.
    Serial.printf("[meshtalk] key stretched in %lu ms (%lu rounds)\n",
                  (unsigned long)s_deriveMs, (unsigned long)MeshMsg::ITERS);
    if (!MeshCrypto::impl().setKey(key)) return false;

    s_prefs.putString("phrase", text);
    s_prefs.putBytes("key", key, sizeof key);
    memcpy(s_phrase, text, n + 1);
    s_havePhrase = true;
    // A different group: counters heard under the old key mean nothing now.
    s_replay = MeshMsg::Replay();
    return true;
}

void clearPhrase() {
    s_prefs.remove("phrase");
    s_prefs.remove("key");
    memset(s_phrase, 0, sizeof s_phrase);
    s_havePhrase = false;
    s_outLen = 0;
    s_outGen++;                // the radio drops the scan response on its next tick
}

bool ready() {
    return Settings::messagesOn() && s_havePhrase && s_selfTestOk;
}

Send send(uint8_t canned, uint32_t now) {
    if (!ready()) return Send::NOT_READY;
    // Replying needs the radio on. The consent gate lives in meshTransmit(),
    // so a device that never agreed to transmit cannot send a message either.
    if (!Settings::meshTransmit()) return Send::TRANSMIT_OFF;
    if (!s_macSet) return Send::FAILED;          // radio not up yet

    // Persist the reservation BEFORE using the counter from it. If the write
    // fails, the reservation is thrown away rather than used unrecorded --
    // a counter that NVS does not know was handed out is one a reboot can
    // hand out again.
    if (s_ctr.needsReserve()) {
        const uint32_t hw = s_ctr.reserve(s_prefs.getUInt("ctr", 0));
        if (s_prefs.putUInt("ctr", hw) == 0) { s_ctr = MeshMsg::Counter(); return Send::FAILED; }
    }
    const uint32_t c = s_ctr.take();
    const size_t n = MeshMsg::sealCanned(MeshCrypto::impl(), s_ownMac, c, canned,
                                         s_out, sizeof s_out);
    if (n == 0) return Send::FAILED;
    s_outLen   = n;
    s_outUntil = now + SEND_MS;
    s_outGen++;
    Serial.printf("[meshtalk] sending #%lu: %s\n", (unsigned long)c, MeshMsg::CANNED[canned]);
    return Send::OK;
}

const uint8_t* outgoing(uint32_t now, size_t& len, uint32_t& gen) {
    if (s_outLen && (int32_t)(now - s_outUntil) < 0) {
        len = s_outLen;
        gen = s_outGen;
        return s_out;
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
    if (m.unknownLine) return "(a line this build doesn't know)";
    return MeshMsg::CANNED[m.canned < MeshMsg::CANNED_N ? m.canned : 0];
}

} // namespace MeshTalk
#endif // SQUACH_MESH
