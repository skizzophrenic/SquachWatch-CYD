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
// confirmPending/confirmLabel: draws a modal "TRACK THIS TARGET?"
// panel with WATCH/HUNT/CANCEL over everything else instead of the
// normal list -- see uiRawScanHitConfirm() below. confirmLabel is
// ignored when confirmPending is false.
// confirmWatched: whether the device the panel is asking about is the one
// already being watched -- the address itself lives in main.cpp, so the
// comparison happens there and only the answer comes through here. Turns the
// panel's WATCH button into UNWATCH; see drawConfirmPanel().
void uiRawScanTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng,
                    bool isBle, bool done, bool confirmPending, const char* confirmLabel,
                    bool confirmWatched, bool confirmHunted, bool confirmIgnored,
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
// used by main.cpp to long-press-select a device/network to watch
// (see DetectionEngine::watchBle/watchWifi). Needs a live TFT_eSPI&
// (not just the screen dimensions) since row height depends on actual
// font metrics.
int uiRawScanRowAt(TFT_eSPI& t, int x, int y, int screenW, int screenH);

// Hit test for the confirm panel's WATCH/HUNT/CANCEL buttons -- only
// meaningful while uiRawScanTick() is being called with
// confirmPending true; main.cpp owns that flag, not this module. WATCH
// sets the target and returns to CLEAR (fires the full-screen
// WATCH_ALERT later, passively, whenever it's next seen); HUNT sets
// the same target but goes straight into the live-tracking HUNT screen
// instead.
enum class RawScanConfirmTap { NONE, WATCH, HUNT, IGNORE, CANCEL };
RawScanConfirmTap uiRawScanHitConfirm(int x, int y, int screenW, int screenH);

// For a board steered by a cursor rather than a finger: every target on the
// screen as a rect, with a kind -- 0 a button, 1 scroll down, 2 scroll up,
// 3 a result row (held, not tapped: a hold is what picks one). In reading
// order. `count` is the number of results; `confirm` whether the WATCH/HUNT
// panel is up, which then owns the screen. Returns how many were written.
uint8_t uiRawScanStops(TFT_eSPI& t, int screenW, int screenH, int count, bool confirm,
                       int16_t* xs, int16_t* ys, int16_t* ws, int16_t* hs, uint8_t* kinds, uint8_t cap);
// How many result rows the list shows at once: a page, for the arrows.
int uiRawScanPageRows(TFT_eSPI& t, int screenW, int screenH);
