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
bool        begin()               { s_state = State::FAILED; return false; }
bool        end()                 { s_state = State::OFF; return false; }
void        tick(uint32_t)        {}
void        rescan()              {}
State       state()               { return s_state; }
uint8_t     netCount()            { return 0; }
const Net*  net(uint8_t)          { return nullptr; }
bool        hasSaved()            { return false; }
bool        savedPassAt(uint8_t, char* out, size_t cap) { if (out && cap) out[0] = 0; return false; }
void        forget()              {}
uint8_t     savedCount()          { return 0; }
const char* savedSsidAt(uint8_t)  { return ""; }
uint8_t     savedUse()            { return 0; }
int8_t      savedIndexOf(const char*) { return -1; }
SavedResult savedResult(uint8_t)  { return SavedResult::UNTRIED; }
bool        saveNetwork(const char*, const char*) { return false; }
void        removeSaved(uint8_t)  {}
void        useSaved(uint8_t)     {}
void        connectSavedAt(uint8_t) {}
void        printSaved()          { Serial.println("[wifi] no WiFi connection on this board yet"); }
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
