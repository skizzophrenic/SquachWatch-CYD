// The payphone -- name entry for SquachMesh. See src/ui_phone.cpp.
#pragma once
#if SQUACH_MESH
#include <TFT_eSPI.h>
#include "detection.h"

void uiPhoneInit(TFT_eSPI& t);
void uiPhoneTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng);
// Which edge of a touch this is. The payphone keypad acts on DOWN and
// ignores the rest, which is its behaviour unchanged. The QWERTY board needs
// all three: it previews on DOWN, follows the finger on MOVE, and only types
// on UP -- which is what lets a near miss be corrected by sliding instead of
// deleted afterwards.
enum class PhoneTouch : uint8_t { DOWN, MOVE, UP };

// Screen coordinates, the frame's `now` (multi-tap needs to know how long ago
// the previous key went down) and the edge. On UP there is no coordinate --
// the finger has gone -- so x and y are ignored and the last good position is
// what counts.
void uiPhoneTouch(int x, int y, uint32_t now, PhoneTouch phase);
// True once OK has been pressed; the caller returns to wherever it came from.
bool uiPhoneDone();
#endif
