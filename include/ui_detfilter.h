// SquachWatch-CYD — per-type detection filter screen, reached via
// Settings' "DETECTION FILTER" row. A flat scrollable list, one row per
// real DetectionType (UNKNOWN excluded — see Settings::typeEnabled()'s
// comment), label ON/OFF, tap to toggle. Same row-list shape as the
// Settings screen itself, just without its grouped headers -- 14 rows
// of the same kind don't need sectioning.
#pragma once
#include <TFT_eSPI.h>
#include <stdint.h>
#include "state.h"

void uiDetFilterInit(TFT_eSPI& t);
void uiDetFilterTick(TFT_eSPI& t, uint32_t now);
void uiDetFilterScroll(int delta);   // positive = scroll down

// The DetectionType whose row (x,y) falls within, or DetectionType::COUNT
// if the tap landed outside every row (a gap, or past the last one).
// Needs a live TFT_eSPI& since row height depends on actual font
// metrics -- same reasoning as every other row-list hit-test in this
// codebase.
DetectionType uiDetFilterHitTest(TFT_eSPI& t, int x, int y, int screenW, int screenH);
