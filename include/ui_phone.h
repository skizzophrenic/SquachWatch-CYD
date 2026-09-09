// The payphone -- name entry for SquachMesh. See src/ui_phone.cpp.
#pragma once
#if SQUACH_MESH
#include <TFT_eSPI.h>
#include "detection.h"

void uiPhoneInit(TFT_eSPI& t);
void uiPhoneTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng);
// Screen coordinates of a tap, plus the frame's `now` -- multi-tap needs to
// know how long ago the previous key went down.
void uiPhoneTouch(int x, int y, uint32_t now);
// True once OK has been pressed; the caller returns to wherever it came from.
bool uiPhoneDone();
#endif
