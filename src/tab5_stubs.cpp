// SquachWatch-CYD — what the Tab5 build does not have yet, stood in for.
//
// The ESP32-P4 has no radio of its own; the Tab5's ESP32-C6 hangs off SDIO
// and src/tab5_radio.cpp drives its WiFi sniffer and its Bluetooth
// controller into the real detection engine. Three things are still not driven and their files are out
// of [env:tab5] (its build_src_filter): the SD card (sd_log.cpp), a
// Bluetooth update (ota_ble.cpp) and a WiFi connection for an update
// (ota_wifi.cpp). This file supplies what they would have, the way the
// emulator's sim/detection_sim.cpp and sim/ota_sim.cpp do.
//
// When the C6's WiFi connection is driven, OtaWifi's stubs go; when a
// Bluetooth update is, OtaBle's.
#if defined(TAB5)

#include "detection.h"
#include "ota_ble.h"
#include "ota_wifi.h"
#include <Arduino.h>
#include <string.h>
#include <Preferences.h>

// ---- SdLog: no card driven on this board yet ------------------------------
bool SdLog::begin() { _ready = false; return false; }
void SdLog::logEvent(const Detection&) {}
void SdLog::tick() {}
void SdLog::wipe() {}
void SdLog::openDaily() {}

// ---- OtaBle: no Bluetooth on this board yet --------------------------------
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
const char* failureText()   { return "No Bluetooth update on this board yet."; }
const char* deviceName()    { return "SquachWatch"; }
uint32_t    pairingCode()   { return 0; }
}  // namespace OtaBle

// ---- OtaWifi: no WiFi connection on this board yet -------------------------
namespace OtaWifi {
static State s_state = State::OFF;
// The saved networks are real: the same NVS list (namespace "otawifi": n,
// use, s<i>, p<i>, r<i>) ota_wifi.cpp keeps on every other board, read and
// written here so HUMAN SENSING can join one (src/sense_cyd/sense_tab5.cpp).
// The Tab5 has no WIFI NETWORKS screen yet: the console's WIFISAVE adds one
// (tools/tab5_wifi.py asks for the password without showing it).
namespace {
const uint8_t SAVED_N = 6;
char    s_names[SAVED_N][33];
uint8_t s_n = 0, s_use = 0;
bool    s_read = false;
void readList() {
    if (s_read) return;
    s_read = true;
    Preferences p;
    if (!p.begin("otawifi", true)) return;
    s_n = p.getUChar("n", 0); if (s_n > SAVED_N) s_n = SAVED_N;
    s_use = p.getUChar("use", 0); if (s_use >= s_n) s_use = 0;
    for (uint8_t i = 0; i < s_n; i++) {
        char k[4]; snprintf(k, sizeof k, "s%u", (unsigned)i);
        strncpy(s_names[i], p.getString(k, "").c_str(), 32); s_names[i][32] = 0;
    }
    p.end();
}
}  // namespace
bool        hasSaved()            { readList(); return s_n > 0; }
uint8_t     savedCount()          { readList(); return s_n; }
const char* savedSsidAt(uint8_t i) { readList(); return i < s_n ? s_names[i] : ""; }
uint8_t     savedUse()            { readList(); return s_use; }
int8_t      savedIndexOf(const char* ssid) { readList(); for (uint8_t i = 0; i < s_n; i++) if (ssid && !strcmp(s_names[i], ssid)) return (int8_t)i; return -1; }
SavedResult savedResult(uint8_t i) {
    Preferences p; if (i >= savedCount() || !p.begin("otawifi", true)) return SavedResult::UNTRIED;
    char k[4]; snprintf(k, sizeof k, "r%u", (unsigned)i);
    const SavedResult r = (SavedResult)p.getUChar(k, 0); p.end(); return r;
}
bool savedPassAt(uint8_t i, char* out, size_t cap) {
    if (!out || !cap) return false;
    out[0] = 0;
    if (i >= savedCount()) return false;
    Preferences p; if (!p.begin("otawifi", true)) return false;
    char k[4]; snprintf(k, sizeof k, "p%u", (unsigned)i);
    strncpy(out, p.getString(k, "").c_str(), cap - 1); out[cap - 1] = 0;
    p.end();
    return true;
}
// The same name replaces its own slot, a new one takes the next (the last
// when full); either becomes USE.
bool saveNetwork(const char* ssid, const char* pass) {
    if (!ssid || !*ssid || strlen(ssid) > 32 || (pass && strlen(pass) > 64)) return false;
    readList();
    int slot = savedIndexOf(ssid);
    if (slot < 0) slot = s_n < SAVED_N ? s_n++ : SAVED_N - 1;
    Preferences p; if (!p.begin("otawifi", false)) return false;
    char k[4];
    snprintf(k, sizeof k, "s%u", (unsigned)slot); p.putString(k, ssid);
    snprintf(k, sizeof k, "p%u", (unsigned)slot); p.putString(k, pass ? pass : "");
    snprintf(k, sizeof k, "r%u", (unsigned)slot); p.putUChar(k, 0);
    p.putUChar("n", s_n); p.putUChar("use", (uint8_t)slot);
    p.end();
    strncpy(s_names[slot], ssid, 32); s_names[slot][32] = 0; s_use = (uint8_t)slot;
    return true;
}
void removeSaved(uint8_t i) {
    readList();
    if (i >= s_n) return;
    Preferences p; if (!p.begin("otawifi", false)) return;
    char a[4], b[4];
    for (uint8_t j = i; j + 1 < s_n; j++) {
        snprintf(a, sizeof a, "s%u", (unsigned)j); snprintf(b, sizeof b, "s%u", (unsigned)(j + 1)); p.putString(a, p.getString(b, ""));
        snprintf(a, sizeof a, "p%u", (unsigned)j); snprintf(b, sizeof b, "p%u", (unsigned)(j + 1)); p.putString(a, p.getString(b, ""));
        snprintf(a, sizeof a, "r%u", (unsigned)j); snprintf(b, sizeof b, "r%u", (unsigned)(j + 1)); p.putUChar(a, p.getUChar(b, 0));
        memcpy(s_names[j], s_names[j + 1], 33);
    }
    s_n--;
    snprintf(a, sizeof a, "s%u", (unsigned)s_n); p.remove(a);
    snprintf(a, sizeof a, "p%u", (unsigned)s_n); p.remove(a);
    snprintf(a, sizeof a, "r%u", (unsigned)s_n); p.remove(a);
    if (s_use >= s_n) s_use = 0;
    p.putUChar("n", s_n); p.putUChar("use", s_use);
    p.end();
}
void useSaved(uint8_t i) {
    readList();
    if (i >= s_n) return;
    Preferences p; if (!p.begin("otawifi", false)) return;
    p.putUChar("use", i); p.end(); s_use = i;
}
void forget() { readList(); if (s_n) removeSaved(s_use); }
// Names stay off the console (a bench log is often shared): the count only.
void printSaved() { Serial.printf("[wifi] %u saved network%s; WIFISAVE adds one (tools/tab5_wifi.py)\n", (unsigned)savedCount(), savedCount() == 1 ? "" : "s"); }
bool        begin()               { s_state = State::FAILED; return false; }
bool        end()                 { s_state = State::OFF; return false; }
void        tick(uint32_t)        {}
void        rescan()              {}
State       state()               { return s_state; }
uint8_t     netCount()            { return 0; }
const Net*  net(uint8_t)          { return nullptr; }
void        connectSavedAt(uint8_t) {}
void        connect(const char*, const char*, bool) { s_state = State::FAILED; }
void        connectSaved()        { s_state = State::FAILED; }
bool        bootCheck(uint32_t)   { return false; }
const char* network()             { return ""; }
const char* latestVersion()       { return ""; }
bool        upToDate()            { return true; }
void        install()             {}
bool        canTryAgain()         { return false; }
void        tryAgain()            {}
uint8_t     percent()             { return 0; }
uint32_t    bytesReceived()       { return 0; }
uint32_t    bytesExpected()       { return 0; }
const char* failureText()         { return "No WiFi update on this board yet: the Tab5's radio sniffs, it does not connect."; }
}  // namespace OtaWifi

#endif  // TAB5
