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
    DETECTION_FILTER,
    IGNORED_DEVICES,
    POWER_SAVER,
    CALIBRATE,
    CHECK_COLORS,
    DIAGNOSTICS,
    REPLAY_INTRO,
    SHOW_OFF,
    NICKNAME,
    SHADES_COLOR,
    SQUACHY_SIZE,
    OUTFIT,
    PET,
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

// ---- confirmation panel ---------------------------------------------------
// Two rows here cannot be taken back by tapping them again: CALIBRATE TOUCH
// throws away a working calibration before it knows the new one is any good,
// and RESET STATS wipes a lifetime count that also gates most of the outfits.
// Both now put a panel up first. It is drawn over the list by uiSettingsTick()
// and hit-tested the same way the log screen's confirm panel is, except that
// the pending row lives in this module rather than in main.cpp -- there is
// nothing main.cpp needs it for between the tap that sets it and the tap that
// answers it.
enum class SettingsConfirmTap { NONE, CONFIRM, CANCEL };

// SettingsRow::NONE clears it. Anything without its own panel text is ignored.
void               uiSettingsSetConfirm(SettingsRow r);
SettingsRow        uiSettingsConfirmRow();
SettingsConfirmTap uiSettingsHitConfirm(int x, int y, int screenW, int screenH);
