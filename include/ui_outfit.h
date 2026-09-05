// SquachWatch-CYD — outfit picker screen: a live Squachy preview with
// left/right arrows to cycle through his currently-unlocked outfits.
// Reuses Squachy::tick() as-is (same call the CLEAR screen makes) —
// this screen is just a thin frame around it.
#pragma once
#include <TFT_eSPI.h>

class DetectionEngine;

void uiOutfitInit(TFT_eSPI& t);
// eng is only needed so the player's chosen background can animate behind
// the preview -- SPECTRUM is the one background that reads live radio
// state. Trying a costume against a flat black rectangle tells you nothing
// about how it will actually look.
void uiOutfitTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng);

// Returns true (and applies the cycle) if the tap landed on the left
// or right arrow. False for a tap anywhere else, so the caller's own
// back-button handling still runs untouched.
bool uiOutfitTapArrow(int x, int y, int screenW, int screenH);
