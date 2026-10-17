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
// `advance` is false on the second of the 3.5"'s two band passes -- the
// same frame drawn again -- so anything that steps by the call rather
// than by the clock must sit still for it. Other boards draw once.
void uiMeshPhraseTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng, bool advance = true);
void uiMeshPhraseTouch(int x, int y);
// True once BACK is pressed on the first page.
bool uiMeshPhraseDone();
// A physical keyboard on the picker: a letter picks that letter (on either
// step), '\b' is BACK. Words are still chosen with the cursor.
// Everything a press would act on right now, as rectangles, for a board
// steered by a cursor. Returns how many were written.
uint8_t uiMeshPhraseStops(int16_t* xs, int16_t* ys, int16_t* ws, int16_t* hs, uint8_t cap);
bool uiMeshPhrasePicking();
void uiMeshPhraseTypeChar(char c);
// Emulator only: 0 shows the current phrase, 1 a freshly rolled one, 2 the
// picker two words in with a letter chosen. Nothing on the device calls it.
void uiMeshPhraseDemo(uint8_t mode);
#endif
