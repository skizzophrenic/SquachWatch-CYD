// SquachWatch-CYD — full-screen ALERT screen
#pragma once
#include <TFT_eSPI.h>
#include <stdint.h>
#include "state.h"

void uiAlertInit(TFT_eSPI& t, const Detection& d);
// infoPending/infoTypeName/infoText: drawn on top of the normal ALERT
// screen (which keeps rendering underneath, same as LOG's confirm/info
// panels over its list) via Theme::drawInfoPanel() -- see its own
// comment. Opened by tapping the MORE INFO button (see
// uiAlertHitMoreInfo() below); both strings are ignored when
// infoPending is false. main.cpp owns all three, same pattern as
// ui_log.h's uiLogTick().
void uiAlertTick(TFT_eSPI& t, uint32_t now,
                 bool infoPending, const char* infoTypeName, const char* infoText);
bool uiAlertTouched();   // any touch since uiAlertInit

// Hit test for the MORE INFO button -- meaningful any time ALERT is
// showing (it has no other modal to be mutually exclusive with, unlike
// LOG's confirm panel); main.cpp only needs to check it when infoPending
// is false, since the info panel itself owns the touch once it's up
// (see Theme::infoPanelHitDismiss()).
bool uiAlertHitMoreInfo(int x, int y, int screenW, int screenH);
