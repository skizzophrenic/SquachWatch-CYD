// SquachWatch-CYD — the SQUAD screen: every SquachWatch in range, and the inbox.
//
// Opened from the small "+N" beside a visitor, which only appears when more
// than one board is near. The outfit chooser's shape, turned outward: one
// Squachy at a time, drawn in his own outfit and shades, arrows to step
// through the rest, and INVITE to make the one showing your visitor -- the
// guest already there says goodbye and walks off, and this one walks in.
//
// The inbox sits beside him: the last few messages rather than only the
// latest, newest first, and a tap on one opens the reply screen. RAM only,
// like every message this device has ever held.
#pragma once
#if SQUACH_MESH
#include <TFT_eSPI.h>
#include <stdint.h>

class DetectionEngine;

enum class SquadHit : uint8_t { NONE, BACK, INVITED, REPLY };

void     uiSquadInit(TFT_eSPI& t);
void     uiSquadTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng);
SquadHit uiSquadTouch(int x, int y, uint32_t now);
#endif
