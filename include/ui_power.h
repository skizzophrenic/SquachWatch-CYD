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

// Forward-declared rather than including detection.h: this header only
// passes the engine through to the backdrop, and pulling the whole engine
// in would drag Preferences and the SD log into every UI translation unit.
class DetectionEngine;

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
// Takes the engine now, and only for the backdrop. THE GIBSON is the one
// background made of real data -- the skyline reacts to the log and the
// trace across the bottom is live per-channel activity -- so it cannot be
// drawn without one. This screen had no engine, so its background switch
// simply had no SPECTRUM case, and the default arm quietly served digital
// rain instead. Picking THE GIBSON and opening this screen changed the
// background, which is a strange thing for a settings screen to do.
void uiPowerTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng);
void uiPowerScroll(int delta);          // positive = scroll down

// Row layout matches whatever uiPowerTick just drew (shared geometry), so
// only call this against a screen already showing this list. Needs a live
// TFT_eSPI& because row height depends on real font metrics -- same
// reasoning as ui_settings.cpp and ui_detfilter.cpp.
PowerRow uiPowerHitTest(TFT_eSPI& t, int x, int y, int screenW, int screenH);
