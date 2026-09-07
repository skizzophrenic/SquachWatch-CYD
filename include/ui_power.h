// SquachWatch-CYD — POWER SAVER screen: every battery-related setting on
// one list, each one chosen and adjusted by hand.
//
// Deliberately NOT a single "eco mode" preset. What actually costs power on
// this board is very different depending on how it is being used -- a device
// sat on a desk wants the screen to time out, a device in a bag wants the
// radio and the core clock -- and a preset would be wrong for most of them.
// The master switch gates the lot without discarding the individual choices.
#pragma once
#include <TFT_eSPI.h>

enum class PowerRow : uint8_t {
    ENABLED = 0,
    SCREEN_TIMEOUT,
    DIM_LEVEL,
    IDLE_FPS,
    IDLE_AFTER,
    CPU_CLOCK,
    WAKE_ON_ALERT,
    COUNT,
    NONE = 255
};

void uiPowerInit(TFT_eSPI& t);
void uiPowerTick(TFT_eSPI& t, uint32_t now);
void uiPowerScroll(int delta);          // positive = scroll down

// Row layout matches whatever uiPowerTick just drew (shared geometry), so
// only call this against a screen already showing this list. Needs a live
// TFT_eSPI& because row height depends on real font metrics -- same
// reasoning as ui_settings.cpp and ui_detfilter.cpp.
PowerRow uiPowerHitTest(TFT_eSPI& t, int x, int y, int screenW, int screenH);
