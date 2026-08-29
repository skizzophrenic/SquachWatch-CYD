// SquachWatch-CYD — settings screen implementation
#include "ui_settings.h"
#include "theme.h"
#include "settings.h"
#include <Arduino.h>

static const uint8_t ROW_COUNT = (uint8_t)SettingsRow::COUNT;
static const int      TOP_MARGIN = 16;

// Shared by draw and hit-test so tapped rows always match what's on
// screen, whatever the current rotation's height is.
static void rowGeom(int screenH, int& top, int& rowH) {
    top  = TOP_MARGIN;
    rowH = (screenH - top - 4) / ROW_COUNT;
}

void uiSettingsInit(TFT_eSPI& t) {
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
}

static void drawRow(TFT_eSPI& t, int w, int y, int h, const char* label,
                    const char* value, bool danger) {
    // Full clear every tick, not just on screen-entry — a value whose
    // text is shorter than what it replaces (e.g. cycling THEME names
    // of different lengths, or BRIGHTNESS's "100%" -> "32%") would
    // otherwise leave the tail end of the old string on screen.
    t.fillRect(0, y, w, h, Theme::BG);
    t.setTextSize(1);
    t.setTextColor(danger ? Theme::RED : Theme::CYAN, Theme::BG);
    t.setCursor(8, y + (h - t.fontHeight(1)) / 2);
    t.print(label);
    if (value) {
        t.setTextColor(Theme::WHITE, Theme::BG);
        int vw = t.textWidth(value);
        t.setCursor(w - 8 - vw, y + (h - t.fontHeight(1)) / 2);
        t.print(value);
    }
    t.drawFastHLine(4, y + h - 1, w - 8, Theme::PURPLE);
}

void uiSettingsTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng) {
    (void)now;
    int w = t.width(), h = t.height();
    Theme::drawTitleBar(t, ">> SETTINGS <<");

    int top, rowH;
    rowGeom(h, top, rowH);

    char buf[24];

    drawRow(t, w, top + 0 * rowH, rowH, "THEME",
            Theme::kPalettes[Settings::paletteIndex()].name, false);
    drawRow(t, w, top + 1 * rowH, rowH, "BACKGROUND",
            Settings::backgroundName(Settings::background()), false);
    snprintf(buf, sizeof(buf), "%u%%", (unsigned)(Settings::brightness() * 100 / 255));
    drawRow(t, w, top + 2 * rowH, rowH, "BRIGHTNESS   -  +", buf, false);
    drawRow(t, w, top + 3 * rowH, rowH, "INVERT COLORS",
            Settings::inverted() ? "ON" : "OFF", false);
    drawRow(t, w, top + 4 * rowH, rowH, "ALERT FILTER",
            Settings::minConfidenceLabel(), false);
    drawRow(t, w, top + 5 * rowH, rowH, "CALIBRATE TOUCH", nullptr, false);
    snprintf(buf, sizeof(buf), "%lu total", (unsigned long)eng.lifetimeTotal());
    drawRow(t, w, top + 6 * rowH, rowH, "RESET STATS", buf, true);
    drawRow(t, w, top + 7 * rowH, rowH, "< BACK", nullptr, false);
}

SettingsRow uiSettingsHitTest(int x, int y, int screenW, int screenH) {
    (void)x; (void)screenW;
    int top, rowH;
    rowGeom(screenH, top, rowH);
    if (y < top) return SettingsRow::NONE;
    int idx = (y - top) / rowH;
    if (idx < 0 || idx >= ROW_COUNT) return SettingsRow::NONE;
    return (SettingsRow)idx;
}
