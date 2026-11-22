#pragma once
#if defined(M5_STOPWATCH)
#include <TFT_eSPI.h>
#include "detection.h"
namespace StopWatchRound {
// Native full-panel backgrounds; foreground layouts share the same coordinates.
void background(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng,
                bool advance, int floor, int textTop);
void overlay(TFT_eSPI& t, uint32_t now);
const uint8_t* backdrop();
bool backgroundTap(int x, int y, uint32_t now);
}
#endif
