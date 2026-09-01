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
// confirmPending/confirmLabel: draws a modal "WATCH THIS TARGET?"
// panel with OK/CANCEL over everything else instead of the normal
// list -- see uiRawScanHitConfirm() below. confirmLabel is ignored
// when confirmPending is false.
void uiRawScanTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng,
                    bool isBle, bool done, bool confirmPending, const char* confirmLabel);
void uiRawScanScroll(int delta);      // positive = scroll down (older)

// This screen shows two buttons in place of the normal three-button
// bar: BACK, and the opposite scan mode (WIFI while looking at BLE
// results, BLE while looking at WiFi results) so you can jump straight
// into the other sweep without detouring back through CLEAR's picker.
enum class RawScanTap { NONE, BACK, SWITCH };
RawScanTap uiRawScanHitTest(int x, int y, int screenW, int screenH);

// Row index (0 = topmost visible, already adjusted for scroll) a tap
// at (x,y) falls within, or -1 if it's outside the list entirely --
// used by main.cpp to long-press-select a device/network to watch
// (see DetectionEngine::watchBle/watchWifi). Needs a live TFT_eSPI&
// (not just the screen dimensions) since row height depends on actual
// font metrics.
int uiRawScanRowAt(TFT_eSPI& t, int x, int y, int screenW, int screenH);

// Hit test for the confirm panel's OK/CANCEL buttons -- only
// meaningful while uiRawScanTick() is being called with
// confirmPending true; main.cpp owns that flag, not this module.
enum class RawScanConfirmTap { NONE, OK, CANCEL };
RawScanConfirmTap uiRawScanHitConfirm(int x, int y, int screenW, int screenH);
