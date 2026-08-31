// SquachWatch-CYD — settings screen implementation
#include "ui_settings.h"
#include "theme.h"
#include "settings.h"
#include "squachy.h"
#include <Arduino.h>

static const int TOP_MARGIN = 16;

// Fixed display order. Squachy-only rows (customizing a character
// "boring mode" has turned off) are filtered out of this by
// visibleRows() below rather than removed here, so their SettingsRow
// values stay stable regardless of which mode is active.
static const SettingsRow ALL_ROWS[] = {
    SettingsRow::THEME, SettingsRow::BACKGROUND, SettingsRow::BRIGHTNESS,
    SettingsRow::INVERT, SettingsRow::BORING_MODE, SettingsRow::CONFIDENCE,
    SettingsRow::CALIBRATE, SettingsRow::REPLAY_INTRO, SettingsRow::NICKNAME,
    SettingsRow::SHADES_COLOR, SettingsRow::OUTFIT, SettingsRow::VIEW_DIARY,
    SettingsRow::RESET_STATS, SettingsRow::BACK,
};
static const uint8_t ALL_ROWS_N = sizeof(ALL_ROWS) / sizeof(ALL_ROWS[0]);

static bool isSquachyOnlyRow(SettingsRow r) {
    return r == SettingsRow::REPLAY_INTRO || r == SettingsRow::NICKNAME ||
           r == SettingsRow::SHADES_COLOR || r == SettingsRow::OUTFIT;
}

// Builds the actual visible list for the current mode into out[] (must
// hold at least ALL_ROWS_N entries) and returns how many. Shared by
// draw and hit-test so a tap always lands on whatever's actually drawn
// — in boring mode this list is shorter, which conveniently also
// un-cramps the row height for people who chose the simpler screen.
static uint8_t visibleRows(SettingsRow* out) {
    uint8_t n = 0;
    bool boring = Settings::boringMode();
    for (uint8_t i = 0; i < ALL_ROWS_N; i++) {
        if (boring && isSquachyOnlyRow(ALL_ROWS[i])) continue;
        out[n++] = ALL_ROWS[i];
    }
    return n;
}

static void rowGeom(int screenH, uint8_t rowCount, int& top, int& rowH) {
    top  = TOP_MARGIN;
    rowH = (screenH - top - 4) / rowCount;
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

// Fills in what a row actually shows. valBuf is scratch space for
// rows that need to format a number — only valid for the duration of
// the caller's own loop iteration, not held onto afterward.
static void rowContent(SettingsRow r, const DetectionEngine& eng, char* valBuf, size_t valBufN,
                       const char*& label, const char*& value, bool& danger) {
    danger = false;
    value  = nullptr;
    switch (r) {
        case SettingsRow::THEME:
            label = "THEME"; value = Theme::kPalettes[Settings::paletteIndex()].name;
            break;
        case SettingsRow::BACKGROUND:
            label = "BACKGROUND"; value = Settings::backgroundName(Settings::background());
            break;
        case SettingsRow::BRIGHTNESS:
            label = "BRIGHTNESS   -  +";
            snprintf(valBuf, valBufN, "%u%%", (unsigned)(Settings::brightness() * 100 / 255));
            value = valBuf;
            break;
        case SettingsRow::INVERT:
            label = "INVERT COLORS"; value = Settings::inverted() ? "ON" : "OFF";
            break;
        case SettingsRow::BORING_MODE:
            label = "BORING MODE"; value = Settings::boringMode() ? "ON" : "OFF";
            break;
        case SettingsRow::CONFIDENCE:
            label = "ALERT FILTER"; value = Settings::minConfidenceLabel();
            break;
        case SettingsRow::CALIBRATE:
            label = "CALIBRATE TOUCH";
            break;
        case SettingsRow::REPLAY_INTRO:
            label = "REPLAY INTRO";
            break;
        case SettingsRow::NICKNAME:
            label = "NICKNAME"; value = Squachy::nickname();
            break;
        case SettingsRow::SHADES_COLOR:
            label = "SHADES COLOR"; value = Squachy::shadesColorName();
            break;
        case SettingsRow::OUTFIT:
            label = "OUTFIT";
            snprintf(valBuf, valBufN, "%s (%u/%u)", Squachy::outfitName(),
                     (unsigned)Squachy::unlockedOutfitCount(), (unsigned)Squachy::outfitCount());
            value = valBuf;
            break;
        case SettingsRow::VIEW_DIARY:
            label = "SQUACHY'S DIARY";
            break;
        case SettingsRow::RESET_STATS:
            label = "RESET STATS";
            snprintf(valBuf, valBufN, "%lu total", (unsigned long)eng.lifetimeTotal());
            value = valBuf;
            danger = true;
            break;
        case SettingsRow::BACK:
            label = "< BACK";
            break;
        default:
            label = "?";
            break;
    }
}

void uiSettingsTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng) {
    (void)now;
    int w = t.width(), h = t.height();
    Theme::drawTitleBar(t, ">> SETTINGS <<");

    SettingsRow rows[ALL_ROWS_N];
    uint8_t n = visibleRows(rows);

    int top, rowH;
    rowGeom(h, n, top, rowH);

    for (uint8_t i = 0; i < n; i++) {
        char valBuf[24];
        const char* label;
        const char* value;
        bool danger;
        rowContent(rows[i], eng, valBuf, sizeof(valBuf), label, value, danger);
        drawRow(t, w, top + i * rowH, rowH, label, value, danger);
    }
}

SettingsRow uiSettingsHitTest(int x, int y, int screenW, int screenH) {
    (void)x; (void)screenW;
    SettingsRow rows[ALL_ROWS_N];
    uint8_t n = visibleRows(rows);

    int top, rowH;
    rowGeom(screenH, n, top, rowH);
    if (y < top) return SettingsRow::NONE;
    int idx = (y - top) / rowH;
    if (idx < 0 || idx >= n) return SettingsRow::NONE;
    return rows[idx];
}
