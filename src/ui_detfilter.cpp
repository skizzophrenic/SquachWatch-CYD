// SquachWatch-CYD — per-type detection filter screen implementation
#include "ui_detfilter.h"
#include "ui_scroll.h"
#include "theme.h"
#include "settings.h"
#include <Arduino.h>

static const int TOP_MARGIN = 16;
static int g_scroll = 0;

// Real types only (1..COUNT-1) -- UNKNOWN is the "matched something but
// not a specific brand" fallback, not a type someone would toggle off.
static uint8_t rowCount() { return CATALOGUE_N; }   // MY ALERTS is switched by its own list
static DetectionType rowType(uint8_t i) { return catalogueAt(i); }

// Same fixed-height-at-size-2 approach Settings uses -- needs a live
// TFT_eSPI& since it depends on actual font metrics, shared by drawing
// and hit-testing so they can't drift apart.
static void computeGeom(TFT_eSPI& t, int screenH, int& top, int& bodyBottom, int& rowH) {
    top = TOP_MARGIN + Theme::listHeadingH();
    bodyBottom = screenH - Theme::pinnedBackH(t.width()) - 2;
    t.setTextSize(Theme::uiMenuTextSize(t));
    // Two pixels taller than the text strictly needs on each side: a 24 px
    // row was a near miss for a thumb, 26 is not, and seven of them still
    // fit above the BACK strip in landscape.
    rowH = t.fontHeight() + Theme::listRowPad();
}

void uiDetFilterInit(TFT_eSPI& t, bool keepScroll) {
    if (!keepScroll) g_scroll = 0;
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
}

void uiDetFilterScroll(int delta) {
    g_scroll += delta;
    if (g_scroll < 0) g_scroll = 0;
}

static void drawRow(TFT_eSPI& t, int w, int y, int hgt, DetectionType type) {
    bool on = Settings::typeEnabled(type);
    Theme::drawListRowPanel(t, w, y, hgt);
    Theme::menuFontOn(t, false);   // FONTS > MENUS
    t.setTextColor(Theme::AMBER, Theme::BG);
    t.setCursor(8, y + (hgt - t.fontHeight()) / 2);
    t.print(detectionTypeName(type));

    const char* value = on ? "ON" : "OFF";
    t.setTextColor(on ? Theme::WHITE : Theme::RED, Theme::BG);
    int vw = t.textWidth(value);
    t.setCursor(w - 18 - vw, y + (hgt - t.fontHeight()) / 2);
    t.print(value);
    t.setTextFont(1);
}

void uiDetFilterTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng) {
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
        // The background starts at the TOP OF THE SCREEN, not at the body's own
    // top. Those first sixteen rows used to be the title bar's; nothing owns
    // them now except the two corner buttons, which draw their own opaque
    // boxes over whatever is behind them. Leaving the animation to start
    // below them left a flat dead strip across the top of this screen --
    // the same relic CLEAR had, and the same fix.
    //
    // Only the BACKGROUND moves. Everything else on this screen still
    // begins where it did, so no content shifts.
    const int bgTop = 0;
if (!Theme::stillBackdrop(t)) switch (Settings::background()) {
        case Settings::Background::STARFIELD: Theme::drawStarfield(t, now, bgTop, bodyBottom); break;
        case Settings::Background::TOASTERS:   Theme::drawFlyingToasters(t, now, bgTop, bodyBottom); break;
        case Settings::Background::AQUARIUM:   Theme::drawAquarium(t, now, bgTop, bodyBottom); break;
        case Settings::Background::TERMINAL:   Theme::drawTerminalLog(t, now, bgTop, bodyBottom); break;
        case Settings::Background::FIREFLIES:  Theme::drawFireflies(t, now, bgTop, bodyBottom); break;
        case Settings::Background::FIRE:       Theme::drawFire(t, now, bgTop, bodyBottom); break;
        case Settings::Background::SNOWFALL:   Theme::drawSnowfall(t, now, bgTop, bodyBottom); break;
        case Settings::Background::SPECTRUM:   Theme::drawGibson(t, now, bgTop, bodyBottom, eng); break;
        case Settings::Background::SYNTHWAVE: Theme::drawSynthwave(t, now, bgTop, bodyBottom); break;
        // Fills rather than skips -- see the note in drawActiveBackground.
        case Settings::Background::BLACK:      t.fillRect(0, bgTop, t.width(), bodyBottom - bgTop, Theme::BG); break;
        default:                               Theme::drawDigitalRain(t, now, bgTop, bodyBottom, true); break;
    }
    Theme::restorePalette(saved);

    Theme::drawTitleBar(t, ">> DETECTION FILTER <<");
    Theme::drawListHeading(t, "TYPE FILTER", Theme::AMBER);

    uint8_t n = rowCount();
    uiClampScroll(g_scroll, n, bodyBottom - top, rowH);
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
    Theme::drawPinnedBack(t, "[ BACK ]");
}

DetectionType uiDetFilterHitTest(TFT_eSPI& t, int x, int y, int screenW, int screenH) {
    (void)x;
    int top, bodyBottom, rowH;
    computeGeom(t, screenH, top, bodyBottom, rowH);
    (void)screenW;

    uint8_t n = rowCount();
    uiClampScroll(g_scroll, n, bodyBottom - top, rowH);
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
