// SquachWatch-CYD -- MY ALERTS, from Settings: the owner's own WiFi names.
//
// Lists the patterns (see my_alerts.h) with their labels. ADD types a
// pattern and then a label on the WiFi password keyboard, LABEL changes the
// selected one's label, REMOVE drops it. main.cpp routes the keyboard's
// result back here, the way WIFI NETWORKS does.
#pragma once
#include <TFT_eSPI.h>
#include <stdint.h>

enum class MyAlertsHit : uint8_t { NONE, ROW, ADD, LABEL, REMOVE, BACK };

void        uiMyAlertsInit(TFT_eSPI& t);
void        uiMyAlertsTick(TFT_eSPI& t, uint32_t now);
// The slot (not the row) comes back in *slot for ROW.
MyAlertsHit uiMyAlertsHit(TFT_eSPI& t, int x, int y, int* slot);
void        uiMyAlertsSelect(int slot);   // -1 clears the highlight
int         uiMyAlertsSelected();
