// SquachWatch-CYD — settings screen (theme, background, invert,
// brightness, alert confidence filter, calibration entry, reset stats)
#pragma once
#include <TFT_eSPI.h>
#include "detection.h"

enum class SettingsRow : uint8_t {
    THEME = 0,
    BACKGROUND,
    BRIGHTNESS,
    INVERT,
    BORING_MODE,
    CONFIDENCE,
    CALIBRATE,
    REPLAY_INTRO,
    NICKNAME,
    SHADES_COLOR,
    VIEW_DIARY,
    RESET_STATS,
    BACK,
    COUNT,
    NONE = 255
};

void uiSettingsInit(TFT_eSPI& t);
void uiSettingsTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng);

// Row layout matches whatever uiSettingsTick just drew (same geometry
// function underneath), so call this only against a screen that's
// already showing the settings screen.
SettingsRow uiSettingsHitTest(int x, int y, int screenW, int screenH);
