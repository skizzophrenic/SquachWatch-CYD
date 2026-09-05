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

void uiOutfitUnlockTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng);

// True once the popup has been on screen long enough to be dismissable —
// the reveal animation is short, and letting a touch already in flight
// when it opened close it immediately would look like it never appeared.
bool uiOutfitUnlockDismissable(uint32_t now);
