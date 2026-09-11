// SquachWatch-CYD — the PIN lock. See include/security.h.
#include "security.h"
#include "ignore_list.h"
#include <Preferences.h>
#include <esp_system.h>   // esp_random(), as meshtalk.cpp uses it
#include <string.h>

namespace {

// ---- a self-contained SHA-256 -------------------------------------------
// So this file needs no crypto library and compiles the same on the device,
// the emulator and the host test. A PIN's whole space is brute-forceable
// offline whatever the hash, so this exists only to keep the digits from
// sitting in flash in the clear, not to resist an attacker with the salt.
struct Sha256 {
    uint32_t s[8];
    uint64_t len;
    uint8_t  buf[64];
    uint8_t  n;
};
inline uint32_t rotr(uint32_t x, uint32_t r) { return (x >> r) | (x << (32 - r)); }
void shaInit(Sha256& c) {
    static const uint32_t iv[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    memcpy(c.s, iv, sizeof iv);
    c.len = 0; c.n = 0;
}
void shaBlock(Sha256& c, const uint8_t* p) {
    static const uint32_t k[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2 };
    uint32_t w[64];
    for (int i = 0; i < 16; i++)
        w[i] = ((uint32_t)p[i*4] << 24) | ((uint32_t)p[i*4+1] << 16) |
               ((uint32_t)p[i*4+2] << 8) | (uint32_t)p[i*4+3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = rotr(w[i-15],7) ^ rotr(w[i-15],18) ^ (w[i-15] >> 3);
        uint32_t s1 = rotr(w[i-2],17) ^ rotr(w[i-2],19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    uint32_t a=c.s[0],b=c.s[1],cc=c.s[2],d=c.s[3],e=c.s[4],f=c.s[5],g=c.s[6],h=c.s[7];
    for (int i = 0; i < 64; i++) {
        uint32_t S1 = rotr(e,6) ^ rotr(e,11) ^ rotr(e,25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = h + S1 + ch + k[i] + w[i];
        uint32_t S0 = rotr(a,2) ^ rotr(a,13) ^ rotr(a,22);
        uint32_t mj = (a & b) ^ (a & cc) ^ (b & cc);
        uint32_t t2 = S0 + mj;
        h=g; g=f; f=e; e=d+t1; d=cc; cc=b; b=a; a=t1+t2;
    }
    c.s[0]+=a; c.s[1]+=b; c.s[2]+=cc; c.s[3]+=d; c.s[4]+=e; c.s[5]+=f; c.s[6]+=g; c.s[7]+=h;
}
void shaUpdate(Sha256& c, const uint8_t* p, size_t len) {
    c.len += len;
    while (len) {
        c.buf[c.n++] = *p++; len--;
        if (c.n == 64) { shaBlock(c, c.buf); c.n = 0; }
    }
}
void shaFinal(Sha256& c, uint8_t out[32]) {
    uint64_t bits = c.len * 8;
    uint8_t pad = 0x80;
    shaUpdate(c, &pad, 1);
    uint8_t z = 0;
    while (c.n != 56) shaUpdate(c, &z, 1);
    uint8_t lenb[8];
    for (int i = 0; i < 8; i++) lenb[i] = (uint8_t)(bits >> (56 - i*8));
    shaUpdate(c, lenb, 8);
    for (int i = 0; i < 8; i++) {
        out[i*4]   = (uint8_t)(c.s[i] >> 24);
        out[i*4+1] = (uint8_t)(c.s[i] >> 16);
        out[i*4+2] = (uint8_t)(c.s[i] >> 8);
        out[i*4+3] = (uint8_t)(c.s[i]);
    }
}
void hashPin(const uint8_t salt[8], const char* digits, uint8_t out[32]) {
    Sha256 c;
    shaInit(c);
    shaUpdate(c, salt, 8);
    shaUpdate(c, (const uint8_t*)digits, strlen(digits));
    shaFinal(c, out);
}

// ---- state --------------------------------------------------------------
const char* NS = "security";
Preferences s_prefs;

bool     s_enabled   = false;
uint8_t  s_len       = 4;
uint8_t  s_salt[8]   = { 0 };
uint8_t  s_hash[32]  = { 0 };
bool     s_hasDuress = false;
uint8_t  s_dsalt[8]  = { 0 };
uint8_t  s_dhash[32] = { 0 };
bool     s_lockAtBoot = false;
uint8_t  s_autoLock  = (uint8_t)Security::AutoLock::OFF;
bool     s_wipeOnFail = false;
uint8_t  s_lockAlerts = (uint8_t)Security::LockAlerts::TYPE_ONLY;

bool     s_locked    = false;
uint8_t  s_fails     = 0;
uint32_t s_waitStart = 0;      // millis() the current backoff began; RAM only

const uint8_t  FAIL_FREE   = 5;    // this many misses before any wait
const uint32_t FAIL_BASE   = 30000;
const uint32_t FAIL_MAX    = 30UL * 60UL * 1000UL;   // half an hour, capped
const uint8_t  WIPE_AT     = 10;

void freshSalt(uint8_t out[8]) {
    for (int i = 0; i < 8; i += 4) {
        uint32_t r = esp_random();
        out[i] = (uint8_t)r; out[i+1] = (uint8_t)(r>>8);
        out[i+2] = (uint8_t)(r>>16); out[i+3] = (uint8_t)(r>>24);
    }
}

uint32_t requiredWaitMs() {
    if (s_fails < FAIL_FREE) return 0;
    uint32_t w = FAIL_BASE;
    for (uint8_t i = FAIL_FREE; i < s_fails && w < FAIL_MAX; i++) w <<= 1;
    return w > FAIL_MAX ? FAIL_MAX : w;
}

void clearNamespace(const char* ns) {
    Preferences p;
    p.begin(ns, false);
    p.clear();
    p.end();
}

}  // namespace

namespace Security {

void begin() {
    s_prefs.begin(NS, false);
    s_locked = false;          // decided afresh below, from what was stored
    s_enabled = s_prefs.getBool("on", false);
    s_len     = s_prefs.getUChar("len", 4);
    if (s_len != 4 && s_len != 6 && s_len != 8) s_len = 4;
    if (s_prefs.getBytes("salt", s_salt, 8) != 8) s_enabled = false;
    if (s_prefs.getBytes("hash", s_hash, 32) != 32) s_enabled = false;
    s_hasDuress = s_prefs.getBool("don", false) &&
                  s_prefs.getBytes("dsalt", s_dsalt, 8) == 8 &&
                  s_prefs.getBytes("dhash", s_dhash, 32) == 32;
    s_lockAtBoot = s_prefs.getBool("boot", false);
    s_autoLock   = s_prefs.getUChar("auto", (uint8_t)AutoLock::OFF);
    if (s_autoLock > (uint8_t)AutoLock::MIN_30) s_autoLock = (uint8_t)AutoLock::OFF;
    s_wipeOnFail = s_prefs.getBool("wipe", false);
    s_lockAlerts = s_prefs.getUChar("alerts", (uint8_t)LockAlerts::TYPE_ONLY);
    if (s_lockAlerts > (uint8_t)LockAlerts::NONE) s_lockAlerts = (uint8_t)LockAlerts::TYPE_ONLY;
    s_fails      = s_prefs.getUChar("fails", 0);

    // Lock at boot when asked; and a device that was already in backoff when it
    // lost power comes back locked and still waiting, timed from now -- the
    // count survived, so pulling the plug bought nothing.
    if (s_enabled && (s_lockAtBoot || s_fails >= FAIL_FREE)) s_locked = true;
    s_waitStart = 0;   // set on the next real millis() in lockoutRemainingMs()
}

bool    enabled()   { return s_enabled; }
uint8_t pinLength() { return s_len; }
PinLen  pinLen()    { return (PinLen)s_len; }
bool    locked()    { return s_enabled && s_locked; }
void    lock()      { if (s_enabled) s_locked = true; }

void setPinLength(PinLen n) {
    if (s_enabled) return;                 // length is fixed once a PIN exists
    s_len = (uint8_t)n;
    s_prefs.putUChar("len", s_len);
}

bool setPin(const char* digits) {
    if (!digits || strlen(digits) != s_len) return false;
    freshSalt(s_salt);
    hashPin(s_salt, digits, s_hash);
    s_prefs.putBytes("salt", s_salt, 8);
    s_prefs.putBytes("hash", s_hash, 32);
    s_prefs.putBool("on", true);
    s_enabled = true;
    s_fails = 0; s_prefs.putUChar("fails", 0);
    return true;
}

void disable() {
    s_prefs.remove("on"); s_prefs.remove("salt"); s_prefs.remove("hash");
    s_prefs.remove("don"); s_prefs.remove("dsalt"); s_prefs.remove("dhash");
    s_prefs.remove("fails");
    s_enabled = false; s_hasDuress = false; s_locked = false; s_fails = 0;
}

bool hasDuress() { return s_hasDuress; }

bool setDuress(const char* digits) {
    if (!s_enabled || !digits || strlen(digits) != s_len) return false;
    // Must not be the real PIN -- otherwise the everyday unlock wipes.
    uint8_t h[32];
    hashPin(s_salt, digits, h);
    if (memcmp(h, s_hash, 32) == 0) return false;
    freshSalt(s_dsalt);
    hashPin(s_dsalt, digits, s_dhash);
    s_prefs.putBytes("dsalt", s_dsalt, 8);
    s_prefs.putBytes("dhash", s_dhash, 32);
    s_prefs.putBool("don", true);
    s_hasDuress = true;
    return true;
}

void clearDuress() {
    s_prefs.remove("don"); s_prefs.remove("dsalt"); s_prefs.remove("dhash");
    s_hasDuress = false;
}

bool lockAtBoot()        { return s_lockAtBoot; }
void setLockAtBoot(bool v) { s_lockAtBoot = v; s_prefs.putBool("boot", v); }

AutoLock autoLock() { return (AutoLock)s_autoLock; }
void cycleAutoLock() {
    s_autoLock = (uint8_t)((s_autoLock + 1) % ((uint8_t)AutoLock::MIN_30 + 1));
    s_prefs.putUChar("auto", s_autoLock);
}
const char* autoLockLabel() {
    switch ((AutoLock)s_autoLock) {
        case AutoLock::OFF:      return "OFF";
        case AutoLock::ON_SLEEP: return "ON SLEEP";
        case AutoLock::MIN_1:    return "1 MIN";
        case AutoLock::MIN_5:    return "5 MIN";
        case AutoLock::MIN_15:   return "15 MIN";
        case AutoLock::MIN_30:   return "30 MIN";
    }
    return "OFF";
}
uint32_t autoLockIdleMs() {
    switch ((AutoLock)s_autoLock) {
        case AutoLock::MIN_1:  return 60UL * 1000UL;
        case AutoLock::MIN_5:  return 5UL * 60UL * 1000UL;
        case AutoLock::MIN_15: return 15UL * 60UL * 1000UL;
        case AutoLock::MIN_30: return 30UL * 60UL * 1000UL;
        default: return 0;
    }
}
bool autoLockOnSleep() { return (AutoLock)s_autoLock == AutoLock::ON_SLEEP; }

bool wipeOnFail()        { return s_wipeOnFail; }
void setWipeOnFail(bool v) { s_wipeOnFail = v; s_prefs.putBool("wipe", v); }

LockAlerts lockAlerts() { return (LockAlerts)s_lockAlerts; }
void cycleLockAlerts() {
    s_lockAlerts = (uint8_t)((s_lockAlerts + 1) % ((uint8_t)LockAlerts::NONE + 1));
    s_prefs.putUChar("alerts", s_lockAlerts);
}
const char* lockAlertsLabel() {
    switch ((LockAlerts)s_lockAlerts) {
        case LockAlerts::FULL:      return "FULL";
        case LockAlerts::TYPE_ONLY: return "TYPE ONLY";
        case LockAlerts::NONE:      return "NONE";
    }
    return "TYPE ONLY";
}

void wipeSecrets() {
    clearNamespace("meshtalk");
    IgnoreList::clear();            // empties RAM and its NVS blob
}

Check check(const char* digits, uint32_t now) {
    if (!s_enabled || !digits) return Check::WRONG;
    if (lockoutRemainingMs(now) > 0) return Check::WRONG;

    if (s_hasDuress) {
        uint8_t h[32];
        hashPin(s_dsalt, digits, h);
        if (memcmp(h, s_dhash, 32) == 0) {
            // Wipe, then behave exactly like a correct unlock: no message, no
            // delay. Whoever forced this sees an ordinary, empty SquachWatch.
            wipeSecrets();
            clearDuress();
            s_fails = 0; s_prefs.putUChar("fails", 0);
            s_locked = false;
            return Check::DURESS;
        }
    }

    uint8_t h[32];
    hashPin(s_salt, digits, h);
    if (memcmp(h, s_hash, 32) == 0) {
        s_fails = 0; s_prefs.putUChar("fails", 0);
        s_locked = false;
        return Check::OK;
    }

    // Wrong. Count it (persisted, so a reboot does not reset the backoff), then
    // start the wait and, at the tenth, wipe if that was asked for.
    if (s_fails < 255) s_fails++;
    s_prefs.putUChar("fails", s_fails);
    s_waitStart = now ? now : 1;
    if (s_wipeOnFail && s_fails >= WIPE_AT) {
        wipeSecrets();
        // Still locked -- guessing ten times is not a way in -- but with the
        // count cleared, or the owner would come back to a half-hour wait for
        // a device that no longer holds anything worth waiting for.
        s_fails = 0; s_prefs.putUChar("fails", 0);
        return Check::WIPED;
    }
    return Check::WRONG;
}

uint32_t lockoutRemainingMs(uint32_t now) {
    const uint32_t need = requiredWaitMs();
    if (!need) return 0;
    // First call after a boot that came up already in backoff: start the clock
    // now, so the wait is served from boot rather than skipped.
    if (s_waitStart == 0) s_waitStart = now ? now : 1;
    const uint32_t waited = now - s_waitStart;
    return waited >= need ? 0 : need - waited;
}

bool verify(const char* digits) {
    if (!s_enabled || !digits || strlen(digits) != s_len) return false;
    uint8_t h[32];
    hashPin(s_salt, digits, h);
    return memcmp(h, s_hash, 32) == 0;
}

bool isDuress(const char* digits) {
    if (!s_hasDuress || !digits || strlen(digits) != s_len) return false;
    uint8_t h[32];
    hashPin(s_dsalt, digits, h);
    return memcmp(h, s_dhash, 32) == 0;
}

void forceUnlock() { s_locked = false; }

uint8_t failCount() { return s_fails; }

}  // namespace Security
