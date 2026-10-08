#include <cstdlib>
// SquachWatch-Sim — pretend firmware updates, so the UPDATE FIRMWARE screen
// can be walked through in the emulator. No radio, no flash: each transport
// plays a scripted run on a timer.
//
// Bluetooth:  0-4 s WAITING, 4-7 s CONNECTED (code accepted at 5.5 s),
//             7-19 s RECEIVING, then VERIFYING, DONE, and back to the menu.
// WiFi:       1.5 s SCANNING, then a list of four networks. Joining takes
//             2 s, checking 2 s, then READY with v1.7.2 on offer. INSTALL
//             downloads for 8 s, verifies, and finishes.
//             A network called "WrongPassword" fails to join, to show TRY AGAIN.
#include "ota_core.h"
#include "ota_ble.h"
#include "ota_wifi.h"
#include <Arduino.h>
#include <string.h>

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "sim"
#endif

static const uint32_t SIM_SIZE = 1416816;

// ---- OtaCore --------------------------------------------------------------------
namespace OtaCore {

static bool     s_restart   = false;
static uint32_t s_restartAt = 0;

const char* failWords(Fail f) {
    switch (f) {
        case Fail::WIFI_PASSWORD: return "Couldn't join that WiFi network. Check the password and try again.";
        case Fail::CANCELLED:     return "Cancelled. Nothing was changed.";
        // The reasons a fetch from the site can fail, as the board words them.
        case Fail::NO_SITE:       return "Joined WiFi, but couldn't connect to squachwatch.com. Is this network online?";
        case Fail::SIGN_IN:       return "This WiFi wants you to sign in first. Do that on your phone, then try again.";
        case Fail::NET_BLOCKED:   return "This network blocks squachwatch.com. Try a phone hotspot or the USB flasher.";
        case Fail::SITE_SLOW:     return "squachwatch.com didn't answer in time. Try again in a minute.";
        case Fail::SITE_DOWN:     return "squachwatch.com is having trouble. Try again later.";
        case Fail::HTTPS_ONLY:    return "squachwatch.com wants a secure link this board can't make. Use the USB flasher.";
        default:                  return "Something went wrong. Nothing was changed.";
    }
}
bool        available()                        { return true; }
void        boot()                             {}
void        tick(uint32_t now)                 { if (s_restart && now >= s_restartAt) s_restart = false; }
const char* takeBootNote(const char**, bool*)  { return nullptr; }
const char* runningSlot()                      { return "app0"; }
const char* runningVersion()                   { return "v1.7.1"; }   // reads naturally in the release clip
static char s_avail[16] = "", s_availFrom[13] = "";
static bool s_availSaid = true;
void noteAvailable(const char* v, const char* who) {
    snprintf(s_avail, sizeof s_avail, "%s", (v && (*v == 'v')) ? v + 1 : (v ? v : ""));
    snprintf(s_availFrom, sizeof s_availFrom, "%s", who ? who : "");
    s_availSaid = false;
}
static char    s_relName[20] = "";
static char    s_news[NEWS_MAX][40];
static uint8_t s_newsN = 0;
void noteRelease(const char* name, const char* const* lines, uint8_t n) {
    snprintf(s_relName, sizeof s_relName, "%s", name ? name : "");
    s_newsN = n > NEWS_MAX ? NEWS_MAX : n;
    for (uint8_t i = 0; i < s_newsN; i++) snprintf(s_news[i], sizeof s_news[i], "%s", lines[i] ? lines[i] : "");
}
const char* releaseName()        { return s_relName; }
uint8_t     newsCount()          { return s_newsN; }
const char* newsAt(uint8_t i)    { return i < s_newsN ? s_news[i] : ""; }
const char* availableVersion()   { return s_avail; }
const char* availableFrom()      { return s_availFrom; }
bool        takeAvailableNotice(){ if (s_availSaid || !s_avail[0]) return false; s_availSaid = true; return true; }
const char* buildName()                        { return "sim"; }
uint32_t    maxImageSize()                     { return 1966080; }
void        refreshOther()                     {}
const char* otherVersion()                     { return "v1.7.0"; }
Fail        switchToOther()                    { restartSoon(3000); return Fail::NONE; }
void        restartSoon(uint32_t ms)           { s_restart = true; s_restartAt = millis() + ms; }
bool        restartPending()                   { return s_restart; }
Fail        begin(uint32_t, const uint8_t*, uint8_t) { return Fail::NONE; }
bool        write(const uint8_t*, size_t)      { return true; }
uint32_t    written()                          { return 0; }
Fail        finish()                           { return Fail::NONE; }
void        abort()                            {}

}  // namespace OtaCore

// ---- OtaBle ---------------------------------------------------------------------
namespace OtaBle {

static State    s_state = State::OFF;
static uint32_t s_t0    = 0;

// SQUACHSIM_NO_BT: render as a CYD does, with no Bluetooth update server
// compiled in (see nimble_flags_cyd in platformio.ini) -- the update screen
// then has no Bluetooth button. Unset, the sim shows the watch's layout.
bool available() { return getenv("SQUACHSIM_NO_BT") == nullptr; }
bool begin() { s_state = State::WAITING; s_t0 = millis(); return true; }
void end()   { s_state = State::OFF; }

void tick(uint32_t now) {
    if (s_state == State::OFF || s_state == State::FAILED) return;
    const uint32_t e = now - s_t0;
    if      (e <  4000) s_state = State::WAITING;
    else if (e <  7000) s_state = State::CONNECTED;
    else if (e < 19000) s_state = State::RECEIVING;
    else if (e < 20500) s_state = State::VERIFYING;
    else if (e < 24000) s_state = State::DONE;
    else                s_state = State::OFF;
}

State    state()         { return s_state; }
bool     codeAccepted()  { return s_state == State::CONNECTED && millis() - s_t0 > 5500; }
uint32_t bytesExpected() { return s_state >= State::RECEIVING ? SIM_SIZE : 0; }
uint32_t bytesReceived() {
    if (s_state != State::RECEIVING) return s_state >= State::VERIFYING ? SIM_SIZE : 0;
    return (uint32_t)((uint64_t)SIM_SIZE * (millis() - s_t0 - 7000) / 12000);
}
uint8_t percent() {
    const uint32_t x = bytesExpected();
    return x ? (uint8_t)((uint64_t)bytesReceived() * 100 / x) : 0;
}
const char* failureText() { return "Cancelled. Nothing was changed."; }
const char* deviceName()  { return "SquachWatch-E5E6"; }
uint32_t    pairingCode() { return 482913; }

}  // namespace OtaBle

// ---- OtaWifi --------------------------------------------------------------------
namespace OtaWifi {

static State    s_state = State::OFF;
static uint32_t s_t0    = 0;
static char     s_net[33] = "";
static bool     s_badPass = false;
static bool     s_saved   = true;
static const Net NETS[] = {
    { "SquachNet",     -48, false },
    { "Neighbours5G",  -63, false },
    { "WrongPassword", -70, false },
    { "CoffeeShop",    -81, true  },
};

bool begin() {
    if (s_saved) { s_state = State::PICK; connect("SquachNet", "", false); return true; }
    s_state = State::SCANNING; s_t0 = millis(); return true;
}
bool end()   { s_state = State::OFF; return false; }
void rescan() { s_state = State::SCANNING; s_t0 = millis(); }

void tick(uint32_t now) {
    const uint32_t e = now - s_t0;
    switch (s_state) {
        case State::SCANNING:    if (e > 1500) s_state = State::PICK; break;
        case State::CONNECTING:  if (e > 2000) { if (s_badPass) s_state = State::FAILED; else { s_state = State::CHECKING; s_t0 = now; } } break;
        // SQUACHSIM_OTAFAIL=N: the check fails with Fail N (see ota_core.h),
        // for filming the new failure screens.
        case State::CHECKING:    if (e > 2000) s_state = getenv("SQUACHSIM_OTAFAIL") ? State::FAILED : State::READY; break;
        case State::DOWNLOADING: if (e > 8000) { s_state = State::VERIFYING; s_t0 = now; } break;
        case State::VERIFYING:   if (e > 1500) { s_state = State::DONE; s_t0 = now; } break;
        case State::DONE:        if (e > 3000) s_state = State::OFF; break;
        default: break;
    }
}

State      state()    { return s_state; }
uint8_t    netCount() { return sizeof NETS / sizeof NETS[0]; }
const Net* net(uint8_t i) { return i < netCount() ? &NETS[i] : nullptr; }
struct SimSaved { char ssid[33]; SavedResult result; };
static SimSaved s_list[SAVED_MAX] = {
    { "SquachNet",   SavedResult::JOINED },
    { "NORTH GATE",  SavedResult::NOT_FOUND },
    { "Coffee Shop", SavedResult::UNTRIED },
};
static uint8_t s_n = 3, s_use = 0;
bool        hasSaved()  { return s_saved && s_n > 0; }
bool        bootCheck(uint32_t) { return false; }
bool        savedPassAt(uint8_t i, char* out, size_t cap) { if (i >= savedCount() || !cap) return false; snprintf(out, cap, "hunter2"); return true; }
void        forget()    { s_saved = false; s_n = 0; }
uint8_t     savedCount()           { return s_saved ? s_n : 0; }
const char* savedSsidAt(uint8_t i) { return i < savedCount() ? s_list[i].ssid : ""; }
uint8_t     savedUse()             { return s_use; }
int8_t      savedIndexOf(const char* ssid) {
    for (uint8_t i = 0; i < savedCount(); i++) if (!strcmp(s_list[i].ssid, ssid)) return (int8_t)i;
    return -1;
}
SavedResult savedResult(uint8_t i) { return i < savedCount() ? s_list[i].result : SavedResult::UNTRIED; }
bool        saveNetwork(const char* ssid, const char*) {
    const int8_t k = savedIndexOf(ssid);
    if (k >= 0) { s_list[k].result = SavedResult::UNTRIED; return true; }
    if (s_n >= SAVED_MAX) return false;
    snprintf(s_list[s_n].ssid, sizeof s_list[s_n].ssid, "%s", ssid);
    s_list[s_n].result = SavedResult::UNTRIED;
    s_n++;
    s_saved = true;
    return true;
}
void        removeSaved(uint8_t i) {
    if (i >= s_n) return;
    for (uint8_t j = i; j + 1 < s_n; j++) s_list[j] = s_list[j + 1];
    s_n--;
    if (s_use == i) s_use = 0; else if (s_use > i) s_use--;
}
void        useSaved(uint8_t i) { if (i < s_n) s_use = i; }
void        printSaved() {}
void        connectSavedAt(uint8_t i) { connect(i < s_n ? s_list[i].ssid : "SquachNet", "", false); }

void connect(const char* ssid, const char*, bool) {
    strncpy(s_net, ssid, sizeof s_net - 1);
    s_badPass = !strcmp(ssid, "WrongPassword");
    s_state = State::CONNECTING;
    s_t0 = millis();
}
void connectSaved() { connect("SquachNet", "", false); }
const char* network()       { return s_net; }
const char* latestVersion() { return "v1.7.2"; }
bool        upToDate()      { return false; }
void        install()       { if (s_state == State::READY) { s_state = State::DOWNLOADING; s_t0 = millis(); } }
bool        canTryAgain()   { return s_state == State::FAILED; }
void        tryAgain()      { rescan(); }

uint32_t bytesExpected() { return s_state >= State::DOWNLOADING ? SIM_SIZE : 0; }
uint32_t bytesReceived() {
    if (s_state != State::DOWNLOADING) return s_state > State::DOWNLOADING ? SIM_SIZE : 0;
    return (uint32_t)((uint64_t)SIM_SIZE * (millis() - s_t0) / 8000);
}
uint8_t percent() {
    const uint32_t x = bytesExpected();
    return x ? (uint8_t)((uint64_t)bytesReceived() * 100 / x) : 0;
}
const char* failureText() {
    static char buf[160];
    if (const char* f = getenv("SQUACHSIM_OTAFAIL")) {
        // The detail the board appends, in brackets, as ota_wifi.cpp does.
        const char* why = getenv("SQUACHSIM_OTAWHY");
        snprintf(buf, sizeof buf, "%s%s%s%s", OtaCore::failWords((OtaCore::Fail)atoi(f)), why ? " (" : "", why ? why : "", why ? ")" : "");
        return buf;
    }
    return OtaCore::failWords(OtaCore::Fail::WIFI_PASSWORD);
}

}  // namespace OtaWifi
