// SquachWatch-CYD — per-type detection filter screen implementation
#include "ui_detfilter.h"
#include "theme.h"
#include "settings.h"
#include <Arduino.h>

static const int TOP_MARGIN = 16;
static int g_scroll = 0;

// Real types only (1..COUNT-1) -- UNKNOWN is the "matched something but
// not a specific brand" fallback, not a type someone would toggle off.
static uint8_t rowCount() { return (uint8_t)DetectionType::COUNT - 1; }
static DetectionType rowType(uint8_t i) { return (DetectionType)(i + 1); }

// Same fixed-height-at-size-2 approach Settings uses -- needs a live
// TFT_eSPI& since it depends on actual font metrics, shared by drawing
// and hit-testing so they can't drift apart.
static void computeGeom(TFT_eSPI& t, int screenH, int& top, int& bodyBottom, int& rowH) {
    top = TOP_MARGIN;
    bodyBottom = screenH - 4;
    t.setTextSize(2);
    rowH = t.fontHeight() + 8;
}

void uiDetFilterInit(TFT_eSPI& t) {
    g_scroll = 0;
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
}

void uiDetFilterScroll(int delta) {
    g_scroll += delta;
    if (g_scroll < 0) g_scroll = 0;
}

static void drawRow(TFT_eSPI& t, int w, int y, int hgt, DetectionType type) {
    bool on = Settings::typeEnabled(type);
    t.setTextSize(2);
    t.setTextColor(Theme::CYAN, Theme::BG);
    t.setCursor(8, y + (hgt - t.fontHeight()) / 2);
    t.print(detectionTypeName(type));

    const char* value = on ? "ON" : "OFF";
    t.setTextColor(on ? Theme::WHITE : Theme::RED, Theme::BG);
    int vw = t.textWidth(value);
    t.setCursor(w - 18 - vw, y + (hgt - t.fontHeight()) / 2);
    t.print(value);

    t.drawFastHLine(4, y + hgt - 1, w - 8, Theme::PURPLE);
}

void uiDetFilterTick(TFT_eSPI& t, uint32_t now) {
    int w = t.width(), h = t.height();

    int top, bodyBottom, rowH;
    computeGeom(t, h, top, bodyBottom, rowH);

    // Same dimmed-background-behind-the-list treatment Settings uses,
    // for the same reason: a plain fill would just be a flat rectangle,
    // this way the screen still feels alive underneath the rows. No
    // spectrum background here -- it wants a live DetectionEngine& this
    // screen doesn't have and isn't worth threading through just for a
    // toggle list's backdrop.
    Theme::Palette saved = Theme::dimPaletteForOverlay(179);
    switch (Settings::background()) {
        case Settings::Background::STARFIELD: Theme::drawStarfield(t, now, top, bodyBottom); break;
        case Settings::Background::TOASTERS:   Theme::drawFlyingToasters(t, now, top, bodyBottom); break;
        case Settings::Background::AQUARIUM:   Theme::drawAquarium(t, now, top, bodyBottom); break;
        case Settings::Background::TERMINAL:   Theme::drawTerminalLog(t, now, top, bodyBottom); break;
        case Settings::Background::FIREFLIES:  Theme::drawFireflies(t, now, top, bodyBottom); break;
        case Settings::Background::FIRE:       Theme::drawFire(t, now, top, bodyBottom); break;
        case Settings::Background::SNOWFALL:   Theme::drawSnowfall(t, now, top, bodyBottom); break;
        case Settings::Background::TUNNEL:     Theme::drawWireframeTunnel(t, now, top, bodyBottom); break;
        default:                               Theme::drawMatrixRain(t, now, top, bodyBottom, true); break;
    }
    Theme::restorePalette(saved);

    Theme::drawTitleBar(t, ">> DETECTION FILTER <<");

    uint8_t n = rowCount();
    int y = top;
    int idx = g_scroll;
    int visibleCount = 0;
    while (idx < n) {
        if (y + rowH > bodyBottom) break;
        drawRow(t, w, y, rowH, rowType((uint8_t)idx));
        y += rowH;
        idx++;
        visibleCount++;
    }

    Theme::drawScrollbar(t, w - 4, top, bodyBottom - top, n, visibleCount, g_scroll);
}

DetectionType uiDetFilterHitTest(TFT_eSPI& t, int x, int y, int screenW, int screenH) {
    (void)x;
    int top, bodyBottom, rowH;
    computeGeom(t, screenH, top, bodyBottom, rowH);
    (void)screenW;

    uint8_t n = rowCount();
    int cy = top;
    int idx = g_scroll;
    while (idx < n) {
        if (cy + rowH > bodyBottom) break;
        if (y >= cy && y < cy + rowH) return rowType((uint8_t)idx);
        cy += rowH;
        idx++;
    }
    return DetectionType::COUNT;
}
