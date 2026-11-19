// SquachWatch-CYD — boot splash screen
#pragma once
#include <TFT_eSPI.h>
#include <stdint.h>

void uiBootInit(TFT_eSPI& t);
void uiBootTick(TFT_eSPI& t, uint32_t now);
bool uiBootDone(uint32_t startMs, uint32_t holdMs = 3000);
#if defined(M5_STOPWATCH)
// Early boot runs before the main sprite exists; keep text on the round glass.
void uiBootStatus(TFT_eSPI& t, const char* title, const char* line1,
                  const char* line2, uint16_t color);
#endif
