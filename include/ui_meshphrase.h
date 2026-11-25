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
// The boards with buttons for a finger: every target drawn this frame, as
// rectangles, for the cursor to step through. Only what is live.
uint8_t uiMeshPhraseStopRects(int16_t* rx, int16_t* ry, int16_t* rw, int16_t* rh, uint8_t cap);
bool    uiMeshPhraseBackRect(int16_t& x, int16_t& y, int16_t& w, int16_t& h);
// True once BACK is pressed on the first page.
bool uiMeshPhraseDone();
// Emulator only: 0 shows the current phrase, 1 a freshly rolled one, 2 the
// picker two words in with a letter chosen. Nothing on the device calls it.
void uiMeshPhraseDemo(uint8_t mode);
#endif
