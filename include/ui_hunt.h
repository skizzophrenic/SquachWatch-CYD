// SquachWatch-CYD — HUNT MODE: live signal-strength gauge for the
// watched target (see DetectionEngine::watchBle/watchWifi and its
// watchRssiCount()/watchRssiAt() history). No magnetometer on this
// hardware, so this deliberately isn't a self-orienting compass -- it's
// a strength meter you sweep by hand/body the way real fox-hunting
// works with a plain omnidirectional receiver: rotate to find where
// the signal fades, walk toward where it doesn't.
#pragma once
#include <TFT_eSPI.h>
#include "detection.h"

void uiHuntInit(TFT_eSPI& t);
void uiHuntTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng);
bool uiHuntHitBack(int x, int y, int screenW, int screenH);
