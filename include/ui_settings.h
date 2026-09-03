// SquachWatch-CYD — settings screen (theme, background, invert,
// brightness, alert confidence filter, calibration entry, reset stats)
#pragma once
#include <TFT_eSPI.h>
#include "detection.h"

enum class SettingsRow : uint8_t {
    THEME = 0,
    BACKGROUND,
    BACKGROUND_LOCK,
    BRIGHTNESS,
    INVERT,
    RGB_SWAP,
    ROTATION_LOCK,
    BORING_MODE,
    CONFIDENCE,
    CALIBRATE,
    CHECK_COLORS,
    DIAGNOSTICS,
    REPLAY_INTRO,
    NICKNAME,
    SHADES_COLOR,
    OUTFIT,
    VIEW_DIARY,
    RESET_STATS,
    BACK,
    COUNT,
    NONE = 255
};

void uiSettingsInit(TFT_eSPI& t);
void uiSettingsTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng);
void uiSettingsScroll(int delta);     // positive = scroll down

// Row layout matches whatever uiSettingsTick just drew (same geometry
// function underneath), so call this only against a screen that's
// already showing the settings screen. Needs a live TFT_eSPI& (not
// just the screen dimensions) since row height now depends on actual
// font metrics -- same reasoning as ui_rawscan.cpp's uiRawScanRowAt().
SettingsRow uiSettingsHitTest(TFT_eSPI& t, int x, int y, int screenW, int screenH);
