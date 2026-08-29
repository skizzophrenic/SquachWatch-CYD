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
void uiClearTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng, bool advance = true);
