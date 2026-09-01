// SquachWatch-CYD — "clear" (idle) screen
#pragma once
#include <TFT_eSPI.h>
#include <stdint.h>
#include "detection.h"

void uiClearInit(TFT_eSPI& t);
// advance: see Squachy::tick()'s header comment -- gates state
// mutation for boards that render in multiple physical bands per
// logical frame. Defaults to true (unchanged behavior for single-pass
// boards).
// scanMenu: true while the SCAN button's BLE/WIFI picker is open --
// swaps the bottom bar to Theme::ButtonBarMode::SCAN_PICKER. Nothing
// else on this screen changes; the caller (main.cpp) is what actually
// interprets a tap on the relabeled slots differently.
void uiClearTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng,
                  bool advance = true, bool scanMenu = false);
