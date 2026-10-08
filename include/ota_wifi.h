// SquachWatch-CYD — firmware updates over WiFi: the transport only.
//
// The board finds nearby networks, you pick yours and type the password on
// the board, and it downloads the latest release for its own build straight
// from squachwatch.com. No computer, no browser, so it works for anybody --
// iPhone owners included. Everything that makes the install itself safe is in
// OtaCore (ota_core.h); this file only gets the bytes there.
//
// WHAT AN UPDATE COSTS NOW. Nothing that cannot be given back. Until v1.10.2
// the download came over TLS, whose handshake wants 40 KB or more of
// CONTIGUOUS heap -- far more than a running board has -- so Bluetooth was
// shut down and its memory handed back, and the screen's 77 KB frame buffer
// went with it. NimBLE cannot be brought back up afterwards, so leaving
// update mode meant restarting the board. Plain HTTP needs neither: the
// screen keeps animating, detection comes straight back when an update is
// cancelled or fails, and only the scan is paused while the radio is busy.
//
// THE PASSWORD. Saved only if the network joins, in its own NVS namespace
// ("otawifi"), which the duress PIN erases along with the other secrets.
//
// OTA_WIFI_BASE overrides where it downloads from, for a bench test against a
// local server: PLATFORMIO_BUILD_FLAGS='-DOTA_WIFI_BASE=\"http://192.168.4.42:8767/\"'.
// Plain http is accepted there and nowhere else by default.
// -DOTA_TEST_NO_DNS makes every name lookup "fail", to prove the fallback to
// squachwatch.com's fixed addresses (see connectSite() in ota_wifi.cpp).
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "ota_core.h"

namespace OtaWifi {

enum class State : uint8_t {
    OFF = 0,
    SCANNING,       // looking for networks
    PICK,           // a list to choose from
    CONNECTING,     // joining the chosen network
    CHECKING,       // fetching the latest version number and its signature
    READY,          // latestVersion() is known; waiting for INSTALL
    DOWNLOADING,
    VERIFYING,
    DONE,           // installed; the board restarts shortly
    FAILED,         // see failureText()
};

struct Net {
    char   ssid[33];
    int8_t rssi;
    bool   open;
};
static const uint8_t NET_MAX = 12;

// Enter update mode and start a scan. The caller pauses detection first (see
// DetectionEngine::startUpdateRadio). Refuses while the device is locked.
bool begin();
// Leave update mode. Always false now -- the caller resumes detection itself.
// (It used to answer true when the board had to restart to get Bluetooth
// back; nothing is released any more, so nothing has to restart.)
bool end();
void tick(uint32_t now);

void rescan();

State       state();
uint8_t     netCount();
const Net*  net(uint8_t i);

bool        hasSaved();
// A saved network's password by index, for the squad nudge to share: the one
// it shares is whichever saved network is actually in the room, not whichever
// is first. Into the caller's buffer, which the caller wipes.
bool        savedPassAt(uint8_t i, char* out, size_t cap);
void        forget();

// The list behind those: up to SAVED_MAX networks, managed on the WIFI
// NETWORKS screen. The one marked USE is the one the boot check tries first
// when it is in range. A network added on the board is not checked by joining
// there and then -- that would stop detection mid-screen -- so its password is
// tried at the next boot check, and savedResult() says how that went.
static const uint8_t SAVED_MAX = 6;
enum class SavedResult : uint8_t { UNTRIED = 0, JOINED, BAD_PASSWORD, NOT_FOUND };
uint8_t     savedCount();
const char* savedSsidAt(uint8_t i);
uint8_t     savedUse();
int8_t      savedIndexOf(const char* ssid);   // -1 when it is not saved
SavedResult savedResult(uint8_t i);
// Add a network, or replace the password of one already saved. False when
// the list is full.
bool        saveNetwork(const char* ssid, const char* pass);
void        removeSaved(uint8_t i);
void        useSaved(uint8_t i);
// Join saved network i. connectSaved() below joins the best saved network in
// the last scan -- the one marked USE if it is there, else the strongest --
// and the one marked USE blind when the scan showed none.
void        connectSavedAt(uint8_t i);
// The list on serial, for the bench: WIFI on the console.
void        printSaved();

// Join a network and check for the latest release. `save` keeps the password
// if the network joins.
void connect(const char* ssid, const char* pass, bool save);
void connectSaved();
// The boot check. Joins the saved network, reads the site's manifest for
// this build, hands anything newer to OtaCore::noteAvailable, and shuts
// WiFi down again. Blocking, time-boxed by `budgetMs`, and meant for boot,
// before there is a screen to hold up: on a running board the same job is a
// whole mode, so detection is paused properly rather than stalled.
// False when there is no saved network, or nothing came back in time.
bool bootCheck(uint32_t budgetMs);

const char* network();          // the one being joined or used
const char* latestVersion();    // meaningful from READY on
bool        upToDate();
void        install();          // READY -> DOWNLOADING

// From FAILED, before anything was downloaded: back to the network list.
bool        canTryAgain();
void        tryAgain();

uint8_t     percent();
uint32_t    bytesReceived();
uint32_t    bytesExpected();
const char* failureText();

}  // namespace OtaWifi
