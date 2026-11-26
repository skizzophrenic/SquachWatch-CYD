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
// Back from a card opened on a result: a clean screen, the scan, the list and
// where it was scrolled all kept.
void uiRawScanResume(TFT_eSPI& t);

// done: pass DetectionEngine::rawBleScanDone()/rawWifiScanDone() for
// whichever mode isBle selects -- draws a "SCANNING..." state until
// then, the results list after.
// Holding a result opens its alert card (main.cpp); there is no panel here.
void uiRawScanTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng,
                    bool isBle, bool done,
                    // false on the second of the 3.5"'s band passes: the same
                    // frame again, so the mascot must not step twice.
                    bool advance = true);
void uiRawScanScroll(int delta);      // positive = scroll down (older)

// This screen shows two buttons in place of the normal three-button
// bar: BACK, and the opposite scan mode (WIFI while looking at BLE
// results, BLE while looking at WiFi results) so you can jump straight
// into the other sweep without detouring back through CLEAR's picker.
enum class RawScanTap { NONE, BACK, SWITCH };
RawScanTap uiRawScanHitTest(int x, int y, int screenW, int screenH);

// Row index (0 = topmost visible, already adjusted for scroll) a tap
// at (x,y) falls within, or -1 if it's outside the list entirely --
// used by main.cpp to long-press a device/network and open its card. Needs a live TFT_eSPI&
// (not just the screen dimensions) since row height depends on actual
// font metrics.
int uiRawScanRowAt(TFT_eSPI& t, int x, int y, int screenW, int screenH);

