// SquachWatch-CYD -- HOME SYNC: hand the SD card's logs to a Tab5 base station
// over the home WiFi, so nobody carries cards between boards.
//
// Every so often, while the board sits on its main screen, it borrows the
// radio the way a WiFi update does (DetectionEngine::startUpdateRadio()),
// looks for its saved network for a couple of seconds, and if it is home,
// joins it, finds the base station (squachbase.local), asks how much of each
// day's log it already holds and sends the rest -- whole rows, from where the
// base station's copy ends. Then the radio goes back to sniffing. Away from
// home the whole visit is the two-second look.
//
// Builds opt in with -DSQW_HOME_SYNC=1. The base station's side is
// src/tab5_base/tab5_base.cpp (HOME SYNC there); docs/TAB5-BASE.md.
#pragma once
#include <stdint.h>

class DetectionEngine;

namespace HomeSync {

// From loop(). idle: the board is on its main screen, unlocked, with no
// update running -- the only time a sync may borrow the radio.
void tick(uint32_t now, bool idle, DetectionEngine& engine);

bool enabled();                 // on by default; kept in NVS
void setEnabled(bool on);
void syncSoon();                // the next idle tick tries, whatever the interval
void printStatus();             // SYNC on the console

}  // namespace HomeSync
