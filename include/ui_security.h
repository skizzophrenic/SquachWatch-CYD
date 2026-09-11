// SquachWatch-CYD — the SECURITY submenu: the PIN lock and everything around
// it, on one list in the same shape as POWER SAVER. The rows that need a PIN
// typed (turning the lock on or off, changing it, setting a duress PIN) hand
// back to main.cpp, which drives the payphone; the plain toggles are applied
// here. See ui_power.h for why the geometry is shared between draw and hit test.
#pragma once
#include <TFT_eSPI.h>

class DetectionEngine;

enum class SecurityRow : uint8_t {
    PIN_LOCK = 0,      // ON/OFF -- turning on sets a PIN, turning off needs it
    PIN_LENGTH,        // 4/6/8, only while no PIN is set
    CHANGE_PIN,
    DURESS_PIN,        // ON sets one, OFF clears it
    AUTO_LOCK,
    LOCK_AT_BOOT,
    WIPE_ON_FAIL,
    LOCK_ALERTS,
    COUNT,
    NONE = 255
};

void uiSecurityInit(TFT_eSPI& t);
void uiSecurityTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng);
void uiSecurityScroll(int delta);
SecurityRow uiSecurityHitTest(TFT_eSPI& t, int x, int y, int screenW, int screenH);
