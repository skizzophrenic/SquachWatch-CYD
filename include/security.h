// SquachWatch-CYD — the optional PIN lock.
//
// What it protects, stated plainly because the UI says it too: people at the
// table, not someone with a USB cable. A 4-8 digit PIN is a space of 10^4 to
// 10^8, brute-forceable offline in seconds, and the message phrase and key sit
// in flash that a cable can read. So this stops a snoop picking the device up,
// and nothing more -- the lock screen says as much when the PIN is first set.
//
// The digits are never stored: a random 8-byte salt and SHA-256(salt||pin) are,
// and the same for the optional duress PIN. All of this is pure arithmetic over
// NVS and a hash, so it builds and is tested on a desktop with no radio and no
// display -- test/security_test.cpp covers the backoff, the duress path and
// which state a wipe leaves behind.
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace Security {

// The lengths a PIN may be. The duress PIN, when set, is the same length as
// the real one -- a shorter one would show fewer dots and give itself away.
enum class PinLen : uint8_t { FOUR = 4, SIX = 6, EIGHT = 8 };

// When the screen locks itself. SLEEP is "whenever the power saver dims the
// screen"; the timed values are idle minutes.
enum class AutoLock : uint8_t { OFF, ON_SLEEP, MIN_1, MIN_5, MIN_15, MIN_30 };

// What an alert may show while locked. Detection keeps running regardless;
// this is only how much of it is put on the glass to a stranger.
enum class LockAlerts : uint8_t { FULL, TYPE_ONLY, NONE };

// The result of trying a PIN. DURESS and the tenth wrong guess both wipe --
// the caller finishes the parts NVS cannot reach (the RAM log, the SD files).
enum class Check : uint8_t { WRONG, OK, DURESS, WIPED };

void begin();               // load from NVS; call once at boot

bool     enabled();         // a real PIN is set
uint8_t  pinLength();       // the chosen length as a number (4/6/8)
PinLen   pinLen();

// The lock state. locked() is false until lock() is called -- at boot when
// lockAtBoot(), by the padlock icon, or by auto-lock.
bool locked();
void lock();
// Only Check::OK / DURESS / WIPED from check() clear this; the UI never sets
// it directly.

// ---- setup, all guarded by the UI behind the current PIN ----
void setPinLength(PinLen n);        // only meaningful before a PIN is set
bool setPin(const char* digits);    // hashes, stores, enables; false if wrong length
void disable();                     // clears the PIN and duress; unlocks

bool hasDuress();
bool setDuress(const char* digits); // same length as the real PIN, and != it
void clearDuress();

bool lockAtBoot();
void setLockAtBoot(bool v);

AutoLock    autoLock();
void        cycleAutoLock();
const char* autoLockLabel();
// Idle milliseconds before auto-lock, or 0 for OFF/ON_SLEEP (which are not
// idle-timed). The caller adds the ON_SLEEP case against its own dim state.
uint32_t    autoLockIdleMs();
bool        autoLockOnSleep();

bool wipeOnFail();                  // wipe after ten wrong guesses
void setWipeOnFail(bool v);

LockAlerts  lockAlerts();
void        cycleLockAlerts();
const char* lockAlertsLabel();

// ---- unlocking ----
// Check a full PIN. Records the attempt, drives the backoff, and on the tenth
// wrong guess wipes if wipeOnFail(). now is millis().
Check check(const char* digits, uint32_t now);

// A side-effect-free test of the real PIN only -- no attempt counted, no
// backoff, no duress, no wipe. This is what the settings screen uses to gate
// changing or removing the PIN, where a mistype should cost nothing.
bool verify(const char* digits);
// Whether `digits` is the duress PIN -- side-effect-free, like verify(). The
// PIN flows use it so a new PIN can never be set equal to the duress one.
bool isDuress(const char* digits);

// After a duress wipe the board restarts, and comes back unlocked whatever
// LOCK AT BOOT says: the unlock has to look like an unlock. main.cpp calls this
// from setup() when the wipe left word in RTC memory that it should.
void forceUnlock();

// While this is non-zero the lock screen must refuse input and show the wait.
// Survives a reboot in effect: the fail count is persisted, so even a fresh
// boot faces the full delay, timed from boot.
uint32_t lockoutRemainingMs(uint32_t now);
uint8_t  failCount();

// Erase the secrets held in NVS: the message phrase, key and replay table, and
// the ignore list. Does NOT touch settings or Squachy. This is the LOGICAL wipe
// -- NVS only marks an entry erased -- so on the device main.cpp follows it
// with a physical erase of the whole store and a restart (performWipe()); the
// RAM log and the SD files are the caller's too. Called for you by a duress
// PIN and by the tenth-wrong-guess wipe; exposed for the test.
void wipeSecrets();

}
