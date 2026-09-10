// SquachWatch-CYD — the SquachMesh phrase: roll a new one, look at the one you
// have, or enter a friend's.
//
// A phrase is five words from the list in meshwords.cpp, and the device rolls
// them -- nobody chooses them. That is where nearly all of a phrase's strength
// comes from, and it is why "enter" is a picker rather than a keyboard: you are
// copying five words somebody else's device rolled, not making any up.
//
// Picking is two taps a word: the first letter, then the word. The list is
// built so each letter's words fit one screen at either rotation, which is what
// makes that possible without scrolling.
#pragma once
#if SQUACH_MESH
#include <TFT_eSPI.h>
#include <stdint.h>

class DetectionEngine;

void uiMeshPhraseInit(TFT_eSPI& t);
void uiMeshPhraseTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng);
void uiMeshPhraseTouch(int x, int y);
// True once BACK is pressed on the first page.
bool uiMeshPhraseDone();
// Emulator only: 0 shows the current phrase, 1 a freshly rolled one, 2 the
// picker two words in with a letter chosen. Nothing on the device calls it.
void uiMeshPhraseDemo(uint8_t mode);
#endif
