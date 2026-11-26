// SquachWatch-CM0 -- the firmware's update API (ota_core.h, ota_ble.h,
// ota_wifi.h), answered honestly for a board that cannot update itself yet.
//
// This replaces the emulator's sim/ota_sim.cpp, which this build used to
// link. That file plays a SCRIPTED, FAKE update for the browser emulator's
// release clips: it reports v1.7.1 running and a v1.7.0 to switch back to,
// "installs" v1.7.2 over a pretend WiFi or a pretend Bluetooth pairing, and
// lists three made-up saved networks ("SquachNet" with "hunter2"...). On a
// real device every one of those is a lie -- and runningVersion() is not
// only on screen: the squad protocol sends it to squadmates and compares
// versions with it (meshtalk.cpp, the update nudge).
//
// Here:
//  - runningVersion() is FIRMWARE_VERSION, which the Makefile stamps from
//    the firmware's git tag (`git describe`), as the firmware's own
//    build stamps it from its git tag.
//  - No other slot to switch to, no Bluetooth update server (the screen
//    drops those buttons), no boot-time check.
//  - Starting a WiFi update goes straight to FAILED with "FEATURE COMING
//    SOON": the update screen's own failure page ("UPDATE STOPPED", the
//    words, OK) is the modal. Signed OTA for the CM0 is Phase 6.
//  - Saved WiFi networks: none to begin with; the WIFI NETWORKS screen can
//    still add and remove them for this run (in memory only -- nothing on
//    this board joins WiFi through them; Linux owns wlan0).
#include "ota_core.h"
#include "ota_ble.h"
#include "ota_wifi.h"
#include <Arduino.h>
#include <string.h>

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "unknown"
#endif

static const char COMING_SOON[] =
    "FEATURE COMING SOON. Updates are not on the Cardputer Zero yet. Nothing was changed.";

// ---- OtaCore --------------------------------------------------------------------
namespace OtaCore {

const char* failWords(Fail f) {
    switch (f) {
        case Fail::CANCELLED: return "Cancelled. Nothing was changed.";
        default:              return COMING_SOON;
    }
}
bool        available()                        { return true; }   // the rows stay: they explain themselves
void        boot()                             {}
void        tick(uint32_t)                     {}
const char* takeBootNote(const char**, bool*)  { return nullptr; }
const char* runningSlot()                      { return "app"; }
const char* runningVersion()                   { return FIRMWARE_VERSION; }
static char s_avail[16] = "", s_availFrom[13] = "";
static bool s_availSaid = true;
// A squadmate's newer version, heard over the mesh: kept, so the nudge can
// say so -- installing it is what is not here yet.
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
const char* releaseName()         { return s_relName; }
uint8_t     newsCount()           { return s_newsN; }
const char* newsAt(uint8_t i)     { return i < s_newsN ? s_news[i] : ""; }
const char* availableVersion()    { return s_avail; }
const char* availableFrom()       { return s_availFrom; }
bool        takeAvailableNotice() { if (s_availSaid || !s_avail[0]) return false; s_availSaid = true; return true; }
const char* buildName()           { return "cardputer-zero"; }
uint32_t    maxImageSize()        { return 0; }
void        refreshOther()        {}
const char* otherVersion()        { return nullptr; }   // no second slot: no SWITCH button
Fail        switchToOther()       { return Fail::CANCELLED; }
void        restartSoon(uint32_t) {}
bool        restartPending()      { return false; }
Fail        begin(uint32_t, const uint8_t*, uint8_t) { return Fail::CANCELLED; }
bool        write(const uint8_t*, size_t) { return false; }
uint32_t    written()             { return 0; }
Fail        finish()              { return Fail::CANCELLED; }
void        abort()               {}

}  // namespace OtaCore

// ---- OtaBle: no Bluetooth update server on this board ------------------------------
namespace OtaBle {
bool        available()     { return false; }
bool        begin()         { return false; }
void        end()           {}
void        tick(uint32_t)  {}
State       state()         { return State::OFF; }
bool        codeAccepted()  { return false; }
uint8_t     percent()       { return 0; }
uint32_t    bytesReceived() { return 0; }
uint32_t    bytesExpected() { return 0; }
const char* failureText()   { return COMING_SOON; }
const char* deviceName()    { return ""; }
uint32_t    pairingCode()   { return 0; }
}  // namespace OtaBle

// ---- OtaWifi: every attempt is "coming soon" -------------------------------------------
namespace OtaWifi {

static State s_state = State::OFF;
static char  s_net[33] = "";

bool begin()        { s_state = State::FAILED; return true; }
bool end()          { s_state = State::OFF; return false; }
void tick(uint32_t) {}
void rescan()       { s_state = State::FAILED; }
State       state()           { return s_state; }
uint8_t     netCount()        { return 0; }
const Net*  net(uint8_t)      { return nullptr; }

struct Saved { char ssid[33]; };
static Saved   s_list[SAVED_MAX];
static uint8_t s_n = 0, s_use = 0;
bool        hasSaved()                 { return s_n > 0; }
bool        bootCheck(uint32_t)        { return false; }
bool        savedPassAt(uint8_t, char* out, size_t cap) { if (cap) out[0] = 0; return false; }
void        forget()                   { s_n = 0; s_use = 0; }
uint8_t     savedCount()               { return s_n; }
const char* savedSsidAt(uint8_t i)     { return i < s_n ? s_list[i].ssid : ""; }
uint8_t     savedUse()                 { return s_use; }
int8_t      savedIndexOf(const char* ssid) {
    for (uint8_t i = 0; i < s_n; i++) if (!strcmp(s_list[i].ssid, ssid)) return (int8_t)i;
    return -1;
}
SavedResult savedResult(uint8_t)       { return SavedResult::UNTRIED; }
bool        saveNetwork(const char* ssid, const char*) {
    if (!ssid || !*ssid || savedIndexOf(ssid) >= 0) return ssid && *ssid;
    if (s_n >= SAVED_MAX) return false;
    snprintf(s_list[s_n].ssid, sizeof s_list[s_n].ssid, "%s", ssid);
    s_n++;
    return true;
}
void        removeSaved(uint8_t i) {
    if (i >= s_n) return;
    for (uint8_t j = i; j + 1 < s_n; j++) s_list[j] = s_list[j + 1];
    s_n--;
    if (s_use == i) s_use = 0; else if (s_use > i) s_use--;
}
void        useSaved(uint8_t i)        { if (i < s_n) s_use = i; }
void        printSaved()               {}
void        connectSavedAt(uint8_t)    { s_state = State::FAILED; }
void        connect(const char* ssid, const char*, bool) {
    snprintf(s_net, sizeof s_net, "%s", ssid ? ssid : "");
    s_state = State::FAILED;
}
void        connectSaved()             { s_state = State::FAILED; }
const char* network()                  { return s_net; }
const char* latestVersion()            { return ""; }
bool        upToDate()                 { return true; }
void        install()                  {}
bool        canTryAgain()              { return false; }   // OK only: trying again would say the same
void        tryAgain()                 {}
uint8_t     percent()                  { return 0; }
uint32_t    bytesReceived()            { return 0; }
uint32_t    bytesExpected()            { return 0; }
const char* failureText()              { return COMING_SOON; }

}  // namespace OtaWifi
