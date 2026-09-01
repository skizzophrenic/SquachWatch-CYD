// SquachWatch-CYD — manual raw BLE/WiFi scanner screen
// Reached from CLEAR's SCAN button picker. Styled like the log screen
// (title bar + scrollable list + bottom bar), plus a mini Squachy
// running around in a header strip while the scan is in progress and
// after.
#pragma once
#include <TFT_eSPI.h>
#include <stdint.h>
#include "detection.h"

void uiRawScanInit(TFT_eSPI& t, bool isBle);

// done: pass DetectionEngine::rawBleScanDone()/rawWifiScanDone() for
// whichever mode isBle selects -- draws a "SCANNING..." state until
// then, the results list after.
void uiRawScanTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng,
                    bool isBle, bool done);
void uiRawScanScroll(int delta);      // positive = scroll down (older)

// Hit test for the single [ BACK ] button this screen shows in place
// of the normal three-button bar -- there's only one meaningful action
// here, so it doesn't reuse Theme::hitTestButtonBar's three-slot grid.
bool uiRawScanHitBack(int x, int y, int screenW, int screenH);
