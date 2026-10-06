// Minimal one-button scanner UI for small SPI displays.
#pragma once
#if defined(SQW_MINI)
#include <TFT_eSPI.h>
#include "detection.h"
namespace MiniScanner {
void begin(TFT_eSPI& display, DetectionEngine& engine);
void tick(TFT_eSPI& display, DetectionEngine& engine);
}
#endif
