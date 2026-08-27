// SquachWatch-CYD — log (list) screen
#pragma once
#include <TFT_eSPI.h>
#include <stdint.h>
#include "detection.h"

void uiLogInit(TFT_eSPI& t);
void uiLogTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng,
               int scrollOffset);
void uiLogScroll(int delta);          // positive = scroll down (older)
