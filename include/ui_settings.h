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
    SECURITY,
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
    SQUACHY_NAME,   // opens the payphone; SquachMesh builds only
    SQUACHMESH,     // announce ourselves to other SquachWatches
    APPEARANCE,     // opens the APPEARANCE page: the display rows, and the hat
    TOP_HAT,        // on the APPEARANCE page, once he is a Legend
    SYSTEM,         // opens the SYSTEM page: calibrate, colours, diagnostics, reset
    WATCH_TARGET,   // "WATCHING: <name>", only while a watch is set. Taps clear it.
    HUNT_TARGET,    // "HUNTING: <name>", same deal
    BACK,
    COUNT,
    NONE = 255
};

void uiSettingsInit(TFT_eSPI& t);
void uiSettingsTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng);
void uiSettingsScroll(int delta);     // positive = scroll down

// Sub-pages are this same screen with a different list on it, so their rows
// draw and hit-test exactly as they always did. uiSettingsInit() always comes
// back to the main page.
//
// Each page keeps its OWN scroll position, so leaving a page and coming back
// puts you where you were instead of at the top -- the list is long enough
// that losing your place was the most-felt annoyance on this screen.
enum class SettingsPage : uint8_t { MAIN = 0, APPEARANCE = 1, SYSTEM = 2 };
void         uiSettingsOpenPage(SettingsPage p);
SettingsPage uiSettingsCurrentPage();

// Kept so existing callers read the same. APPEARANCE only.
void uiSettingsOpenAppearance(bool open);
bool uiSettingsInAppearance();

// A tap on a group heading folds that group away, turning a list that runs
// four screens deep into a short menu. Returns true if (x,y) hit a heading and
// the fold was toggled, in which case the tap is spent -- call this BEFORE
// uiSettingsHitTest().
bool uiSettingsTapHeader(TFT_eSPI& t, int x, int y, int screenW, int screenH);

// True when a mode has switched this row off (boring mode, today). A tap on
// one says why rather than silently doing nothing.
bool        uiSettingsRowIsOff(SettingsRow r);

// BACK is pinned to the bottom edge rather than living at the end of the list,
// so leaving never means scrolling to find the way out. True when (x,y) is on
// it -- checked before the row hit test, which cannot see it.
bool uiSettingsTapPinnedBack(TFT_eSPI& t, int x, int y, int screenW, int screenH);

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
