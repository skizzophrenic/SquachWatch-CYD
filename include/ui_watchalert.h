// SquachWatch-CYD — dedicated alert screen for a watched target
// ("stalker tracker") coming back into range. Deliberately different
// from the normal ALERT screen: Squachy runs around behind outlined
// headline text instead of a per-type ambient scene.
#pragma once
#include <TFT_eSPI.h>
#include <stdint.h>
#include "detection.h"

void uiWatchAlertInit(TFT_eSPI& t);
// advance: see Squachy::tick()'s header comment -- gates state
// mutation for boards that render in multiple physical bands per
// logical frame. Defaults to true (unchanged behavior for single-pass
// boards).
void uiWatchAlertTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng, bool advance = true);

// True when (x,y) falls on the REMOVE FROM WATCH LIST button along the bottom
// of this screen. Until this existed a watch could only be ended by a reboot
// or the security wipe -- clearWatch() had no caller in any screen. The alert
// is the one place the target is ever named on screen, so it is the one place
// you can act on it without finding it in a scan again.
bool uiWatchAlertHitRemove(TFT_eSPI& t, int x, int y);
