// SquachWatch-CYD — "clear" (idle) screen
#pragma once
#include <TFT_eSPI.h>
#include <stdint.h>
#include "detection.h"

void uiClearInit(TFT_eSPI& t);
void uiClearTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng);
