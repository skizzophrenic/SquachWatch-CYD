// SquachWatch-CYD — the SQUAD screen: every SquachWatch in range, and the inbox.
// Or, opened from the SquachMesh menu, the roster: everybody who has ever
// held the phrase, here or not, with FORGET in place of ADD TO SQUAD.
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

// SEND opens the message screen to write to the squad, without needing a
// message in the inbox to reply to. On the in-range page only.
enum class SquadHit : uint8_t { NONE, BACK, INVITED, REPLY, ADD, HUNT, SEND };
// Whether the roster is showing rather than the boards in range: BACK goes
// to a different screen in each case.
bool uiSquadRosterMode();
// The board the carousel is showing, for ADD TO SQUAD.
const uint8_t* uiSquadSelectedMac();
const char*    uiSquadSelectedName();

void     uiSquadInit(TFT_eSPI& t, bool roster = false);
// The engine is read for the backdrop and for whether the board showing is
// already the hunt target.
// `advance` is false on the second of the 3.5"'s two band passes -- the
// same frame drawn again -- so anything that steps by the call rather
// than by the clock must sit still for it. Other boards draw once.
void     uiSquadTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng, bool advance = true);
SquadHit uiSquadTouch(int x, int y, uint32_t now);
// Everything a press would act on right now, as rectangles, for a board
// steered by a cursor. Returns how many were written.
uint8_t  uiSquadStops(int16_t* xs, int16_t* ys, int16_t* ws, int16_t* hs, uint8_t cap);
#endif
