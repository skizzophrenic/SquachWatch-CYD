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
