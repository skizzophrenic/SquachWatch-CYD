// SquachWatch-CYD — log (list) screen
#pragma once
#include <TFT_eSPI.h>
#include <stdint.h>
#include "detection.h"

void uiLogInit(TFT_eSPI& t);
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
void uiLogTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng,
               int scrollOffset, bool confirmPending, const char* confirmLabel,
               bool infoPending, const char* infoTypeName, const char* infoText);
void uiLogScroll(int delta);          // positive = scroll down (older)

// Row index (0 = topmost visible, already adjusted for scroll) a tap
// at (x,y) falls within, or -1 if outside the list entirely -- used by
// main.cpp to long-press-select an entry to watch/hunt (see
// DetectionEngine::watchBle/watchWifi/huntBle/huntWifi). Needs a live
// TFT_eSPI& since row height depends on actual font metrics.
int uiLogRowAt(TFT_eSPI& t, int x, int y, int screenW, int screenH);

// Hit test for the confirm panel's WATCH/HUNT/INFO/CANCEL buttons --
// only meaningful while uiLogTick() is being called with confirmPending
// true; main.cpp owns that flag, not this module.
enum class LogConfirmTap { NONE, WATCH, HUNT, INFO, CANCEL };
LogConfirmTap uiLogHitConfirm(int x, int y, int screenW, int screenH);
