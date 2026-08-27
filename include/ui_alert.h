// SquachWatch-CYD — full-screen ALERT screen
#pragma once
#include <TFT_eSPI.h>
#include <stdint.h>
#include "state.h"

void uiAlertInit(TFT_eSPI& t, const Detection& d);
void uiAlertTick(TFT_eSPI& t, uint32_t now);
bool uiAlertTouched();   // any touch since uiAlertInit
