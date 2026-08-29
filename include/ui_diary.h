// SquachWatch-CYD — Squachy's diary (companion stats) screen
#pragma once
#include <TFT_eSPI.h>
#include "detection.h"

void uiDiaryInit(TFT_eSPI& t);

// Tap anywhere to go back to CLEAR — no button bar, this is a simple
// read-only info panel.
void uiDiaryTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng);
