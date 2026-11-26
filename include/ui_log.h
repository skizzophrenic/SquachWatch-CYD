// SquachWatch-CYD — log (list) screen
#pragma once
#include <TFT_eSPI.h>
#include <stdint.h>
#include "detection.h"

void uiLogInit(TFT_eSPI& t);
void uiLogLeave();                   // from loop() whenever the LOG is not up: lets its index go
// confirmPending/confirmLabel: draws a modal "TRACK THIS TARGET?"
// panel with WATCH/HUNT/MORE INFO/CANCEL over everything else instead
// of the normal list -- see uiLogHitConfirm() below. confirmLabel is
// ignored when confirmPending is false. Mirrors ui_rawscan.cpp's
// WATCH/HUNT/CANCEL panel as its own copy rather than a shared helper
// (that screen doesn't get the INFO option -- see its own comment for
// why), consistent with this screen already keeping its own geometry/
// scrollbar code instead of reaching into that module.
//
// infoPending/infoTypeName/infoText: drawn INSTEAD of the confirm panel
// (the two are mutually exclusive -- confirmPending is only ever
// checked when infoPending is false) via Theme::drawInfoPanel() -- see
// its own comment (ALERT's MORE INFO button shares that same panel).
// Both strings are ignored when infoPending is false; main.cpp decides
// what they actually are (infoTypeName is null during the one-time
// RSSI/confidence primer page, which isn't about any one type -- see
// DetectionInfo::explain()/rssiConfidencePrimer()), this module just
// hands them through to Theme:: unchanged. Hit-testing its dismiss
// button is Theme::infoPanelHitDismiss(), not owned here.
// confirmWatched: see ui_rawscan.h's copy -- the panel's WATCH button becomes
// UNWATCH when the device it is asking about is the one already being watched,
// and IGNORE becomes UN-IGNORE when it is on the ignore list.
void uiLogTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng,
               int scrollOffset, bool confirmPending, const char* confirmLabel,
               bool infoPending, const char* infoTypeName, const char* infoText,
               bool confirmWatched, bool confirmHunted, bool confirmIgnored);
void uiLogScroll(int delta);          // positive = scroll down (older)

// One row of the list, newest first, and how many rows there are. The first
// LOG_CAP of them are the engine's ring; past that they come from the black
// box, which is where the board's older sightings live now. A row read from
// flash is carried in a buffer of this screen's, so the pointer is good until
// the next call -- draw it or copy it, do not keep it.
const Detection* uiLogRow(const DetectionEngine& eng, int idx);
// Where the long-pressed device was last seen, from the black box (a position
// a GPS or a phone gave it). Shown on the confirm panel in place of its
// question; `has` false clears it. Set when the panel opens.
void uiLogSetConfirmWhere(bool has, int32_t lat7, int32_t lon7);
uint16_t         uiLogRowCount(const DetectionEngine& eng);

// Row index (0 = topmost visible, already adjusted for scroll) a tap
// at (x,y) falls within, or -1 if outside the list entirely -- used by
// main.cpp to long-press-select an entry to watch/hunt (see
// DetectionEngine::watchBle/watchWifi/huntBle/huntWifi). Needs a live
// TFT_eSPI& since row height depends on actual font metrics.
int uiLogRowAt(TFT_eSPI& t, int x, int y, int screenW, int screenH);

// Hit test for the confirm panel's WATCH/HUNT/INFO/CANCEL buttons --
// only meaningful while uiLogTick() is being called with confirmPending
// true; main.cpp owns that flag, not this module.
enum class LogConfirmTap { NONE, WATCH, HUNT, INFO, IGNORE, CANCEL, RING };
LogConfirmTap uiLogHitConfirm(int x, int y, int screenW, int screenH);
// RING on the panel: shown for a tracker on the boards built with it (set
// when the panel opens), and held down while a ring is under way.
void uiLogSetConfirmRing(bool show);
void uiLogSetRinging(bool on);
