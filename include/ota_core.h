// SquachWatch-CYD — the part of a firmware update that does not care how the
// bytes arrive.
//
// Bluetooth (ota_ble.h) and WiFi (ota_wifi.h) are only transports. Both hand
// their bytes to this installer, and everything that makes an update safe
// lives here, once:
//
//   1. The image streams into the spare app slot while the current one keeps
//      running, hashed as it goes.
//   2. The hash must match a signature made with the project's private key (a
//      GitHub Actions secret); the public key is compiled in, see
//      ota_pubkey.h. The signed message includes this board's build name, so a
//      correctly signed image for a DIFFERENT board is refused too -- a wrong
//      display driver is a white screen, and a white screen cannot be
//      navigated to switch back.
//   3. The ESP32's own rollback. A freshly installed image boots on probation
//      and only confirms itself after running for CONFIRM_MS. A crash or a
//      power cut before then and the bootloader returns to the old slot.
//
// Signed message = SHA-256("SQWOTA1\n" + build name + "\n" + image), ECDSA
// P-256, DER. tools/sign_firmware.py makes them.
//
// It also owns the two-slot bookkeeping the UPDATE FIRMWARE screen shows:
// which version is in each slot, and switching to the other one.
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace OtaCore {

enum class Fail : uint8_t {
    NONE = 0,
    CANCELLED,
    LOST_CONNECTION,
    BAD_CODE,
    TOO_BIG,
    NOT_FIRMWARE,
    WRITE_ERROR,
    BAD_SIGNATURE,
    DAMAGED,
    TIMEOUT,
    LOCKED,
    RADIO_BUSY,
    WIFI_NOT_FOUND,
    WIFI_PASSWORD,
    NO_SITE,
    NOT_SIGNED,
    LOW_MEMORY,
    TOO_OLD,
    // What a WiFi update ran into on the way to squachwatch.com (2026-10-06).
    // NO_SITE above is now only "no connection at all".
    SIGN_IN,        // a hotel/school/guest network's sign-in page answered instead
    NET_BLOCKED,    // the network refused, or answered with something not ours
    SITE_SLOW,      // connected, but no reply in time
    SITE_DOWN,      // the site answered with a server error
    HTTPS_ONLY,     // the site now redirects to https, which this board cannot follow
};

// What to tell a person, in words they can act on.
const char* failWords(Fail f);

// How long a freshly installed or switched-to image must run before it
// confirms itself. Long enough to get past boot, the radios starting and the
// first screens; short enough that nobody gives up waiting.
static const uint32_t CONFIRM_MS = 30000;

// True on builds with a second app slot to install into.
bool available();

// Once in setup(), after NVS is usable. Records which version lives in this
// slot, and works out whether this boot is an update on probation or the
// aftermath of one that was rolled back.
void boot();

// Every loop: confirms a probationary image once it has run for CONFIRM_MS,
// and performs a restart somebody asked for with restartSoon().
void tick(uint32_t now);

// The result of the last update or switch, for a toast -- returned once, then
// cleared. nullptr when there is nothing to say.
const char* takeBootNote(const char** sub, bool* good);

const char* runningSlot();      // "app0" / "app1"
const char* runningVersion();
// A newer release this board has heard of: from the boot check over WiFi, or
// from a squad member's hello over the mesh. `who` is the member's name, or
// empty for the site. Ignored unless newer than what is running. RAM only:
// the boot check runs every boot anyway.
void        noteAvailable(const char* version, const char* who);
const char* availableVersion();   // "" when nothing newer is known
const char* availableFrom();      // the member's name, or ""
// What the site's manifest said about that release: its name, and the few
// lines it wrote for a board's screen. Both empty for a version heard from a
// squad member, whose hello carries a number and nothing else. RAM only, and
// only ever about the version in availableVersion().
void        noteRelease(const char* name, const char* const* lines, uint8_t n);
const char* releaseName();        // "" when none came with it
uint8_t     newsCount();          // 0..NEWS_MAX
const char* newsAt(uint8_t i);
constexpr uint8_t NEWS_MAX = 4;
// True once per newer version heard of: the moment to open the update window.
bool        takeAvailableNotice();
const char* buildName();        // the PlatformIO environment, e.g. "cyd-fast"
uint32_t    maxImageSize();

// Refreshes otherVersion(). Reads flash, so call it when a screen opens.
void        refreshOther();
// The version in the other slot, or nullptr when there is nothing there this
// board can safely boot: empty, never finished, rolled back, or built for a
// different board.
const char* otherVersion();

// Points the bootloader at the other slot and restarts shortly after. Same
// probation as an update. Fail::DAMAGED if the bootloader would not take it.
Fail switchToOther();

void restartSoon(uint32_t ms);
bool restartPending();

// ---- the installer ---------------------------------------------------------
// One image at a time. begin() erases exactly the sectors the image needs, so
// it takes a few seconds; call it from a task that can afford to wait.
Fail     begin(uint32_t size, const uint8_t* sig, uint8_t sigLen);
// Bytes in order. Safe to call from a different task than begin()/finish().
// False on a flash error; the caller fails the transfer.
bool     write(const uint8_t* data, size_t len);
uint32_t written();
// Checks the signature and the image, then points the bootloader at it. On
// success the caller restarts the board (restartSoon) when it has said so.
Fail     finish();
void     abort();
// Bench builds only: run the version marker scanner over a made-up image and
// say what the installer would do with it. VERTEST <version> on the console.
const char* testVersionDecision(const char* version);
#ifdef BENCH_TOOLS
// Bench builds only: check a real release's signature, and three spoiled
// copies of it, against the compiled-in key. SIGTEST on the console.
const char* testSignature();
#endif

}  // namespace OtaCore
