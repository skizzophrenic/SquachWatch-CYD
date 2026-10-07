// SquachWatch-CYD — full-screen ALERT screen
#pragma once
#include <TFT_eSPI.h>
#include <stdint.h>
#include "state.h"

class DetectionEngine;

void uiAlertInit(TFT_eSPI& t, const Detection& d);
// The first of this type this board has ever caught: the card says so.
void uiAlertSetFirst(bool first);
// Caught between eleven at night and five in the morning, by the real
// clock: the card says so. False whenever the clock is not set.
void uiAlertSetNight(bool night);
// This device has just used the last of its AUTO SNOOZE allowance: the card
// says so, on the alert it is spending. Told HERE rather than the first time
// one is held back, because this is the moment you are looking at that
// device -- and a board that goes quieter without saying so is the failure
// this whole thing exists to avoid.
void uiAlertSetLastFree(bool lastFree);
// This alert is a spam flood's one announcement (see spam_watch.h): the card
// says how many fakes so far, and that the rest of the flood stays quiet.
void uiAlertSetSpam(bool spam, uint16_t fakes);
// SNOOZE (this device, until restart), bottom centre between HUNT and MORE INFO.
bool uiAlertHitSnooze(int x, int y, int screenW, int screenH);
// While the device is locked with ALERTS WHEN LOCKED at TYPE ONLY: the type
// and the signal still show, the device's name, label and address do not.
void uiAlertSetRedacted(bool redacted);
#if defined(CYD_GPS)
// Where the board was when it caught this, from a fresh GPS fix; fake marks a
// bench fix. Cleared by uiAlertInit, and never drawn on a redacted card.
void uiAlertSetPlace(bool has, int32_t lat7, int32_t lon7, bool fake);
#endif
// infoPending/infoTypeName/infoText: drawn on top of the normal ALERT
// screen (which keeps rendering underneath, same as LOG's confirm/info
// panels over its list) via Theme::drawInfoPanel() -- see its own
// comment. Opened by tapping the MORE INFO button (see
// uiAlertHitMoreInfo() below); both strings are ignored when
// infoPending is false. main.cpp owns all three, same pattern as
// ui_log.h's uiLogTick().
// eng is only needed so the player's selected background can animate
// behind the alert -- SPECTRUM is the one background that reads live
// radio state. See ALERT_SHOW_BACKGROUND in the implementation.
void uiAlertTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng,
                 bool infoPending, const char* infoTypeName, const char* infoText);
bool uiAlertTouched();   // any touch since uiAlertInit

// Hit test for the MORE INFO button -- meaningful any time ALERT is
// showing (it has no other modal to be mutually exclusive with, unlike
// LOG's confirm panel); main.cpp only needs to check it when infoPending
// is false, since the info panel itself owns the touch once it's up
// (see Theme::infoPanelHitDismiss()).
bool uiAlertHitMoreInfo(int x, int y, int screenW, int screenH);

// Hit test for the HUNT button in the opposite (bottom-left) corner.
// Same "only meaningful while the info panel is down" caveat as
// uiAlertHitMoreInfo() above -- once the panel is up it owns the touch.
bool uiAlertHitHunt(int x, int y, int screenW, int screenH);

// Hit test for the IGNORE button, top-right. Same 70x48 footprint as HUNT
// and MORE INFO so the three read as one set of controls, and in the one
// corner the rest of this screen's centred layout leaves clear at every
// rotation. Same "only meaningful while the info panel is down" caveat.
bool uiAlertHitIgnore(int x, int y, int screenW, int screenH);

// Hit test for the IGNORE button, top-right. Same 70x48 footprint as HUNT
// and MORE INFO so the three read as one set of controls, and in the one
// corner the rest of this screen's centred layout leaves clear at every
// rotation. Same "only meaningful while the info panel is down" caveat.
bool uiAlertHitIgnore(int x, int y, int screenW, int screenH);
