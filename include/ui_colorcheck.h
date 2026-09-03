// SquachWatch-CYD — first-boot color-order sanity check
// Some CYD panel batches render colors wrong (RGB/BGR swapped, or
// polarity-inverted) -- this puts pure RED/GREEN/BLUE on screen right
// after the boot splash so it's obvious immediately, with the two
// fixes (INVERT, COLOR ORDER) one tap away instead of a Settings dig.
// Runs once automatically on a device's first-ever boot; Settings'
// "CHECK COLORS" row can re-enter it any time after that.
#pragma once
#include <TFT_eSPI.h>

void uiColorCheckInit(TFT_eSPI& t);
void uiColorCheckTick(TFT_eSPI& t, uint32_t now);

enum class ColorCheckTap { NONE, INVERT, ORDER, DONE };
ColorCheckTap uiColorCheckHitTest(int x, int y, int screenW, int screenH);
