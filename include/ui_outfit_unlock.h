// SquachWatch-CYD — OUTFIT UNLOCKED celebration popup
#pragma once
#include <TFT_eSPI.h>
#include <stdint.h>
#include "state.h"

class DetectionEngine;

// Shown once whenever an outfit becomes available, whether that was
// crossing a lifetime-detection threshold or summoning the werewolf.
// main.cpp drains Squachy::consumeOutfitUnlock() and calls this with the
// index it got back; the queue means two outfits earned by the same
// detection are celebrated one after the other rather than one of them
// being silently swallowed.
void uiOutfitUnlockInit(TFT_eSPI& t, uint8_t outfitIdx);

// The same card, for the companion. It lives in here rather than in a file
// of its own because everything except the headline, the stage and the
// footer is identical -- the glitch burst, the dimmed backdrop, the panel
// that scales open, the per-letter rainbow reveal, the dismiss guard.
//
// It exists at all because the pet was the one unlock with no card. A hat
// got a celebration; a whole companion got a single line in his bubble --
// and unlocking the pet also makes a settings row APPEAR that was hidden
// until then, which nothing told you about. The footer is that fix.
void uiPetUnlockInit(TFT_eSPI& t);

void uiOutfitUnlockTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng, bool advance = true);

// True once the popup has been on screen long enough to be dismissable —
// the reveal animation is short, and letting a touch already in flight
// when it opened close it immediately would look like it never appeared.
bool uiOutfitUnlockDismissable(uint32_t now);
