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

// Forward-declared rather than including detection.h: this header only
// passes the engine through to the backdrop, and pulling the whole engine
// in would drag Preferences and the SD log into every UI translation unit.
class DetectionEngine;

void uiDetFilterInit(TFT_eSPI& t);
// Takes the engine now, and only for the backdrop. THE GIBSON is the one
// background made of real data -- the skyline reacts to the log and the
// trace across the bottom is live per-channel activity -- so it cannot be
// drawn without one. This screen had no engine, so its background switch
// simply had no SPECTRUM case, and the default arm quietly served digital
// rain instead. Picking THE GIBSON and opening this screen changed the
// background, which is a strange thing for a settings screen to do.
void uiDetFilterTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng);
void uiDetFilterScroll(int delta);   // positive = scroll down

// The DetectionType whose row (x,y) falls within, or DetectionType::COUNT
// if the tap landed outside every row (a gap, or past the last one).
// Needs a live TFT_eSPI& since row height depends on actual font
// metrics -- same reasoning as every other row-list hit-test in this
// codebase.
DetectionType uiDetFilterHitTest(TFT_eSPI& t, int x, int y, int screenW, int screenH);
