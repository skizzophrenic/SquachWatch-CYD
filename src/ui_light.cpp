// SquachWatch-CYD — STATUS LIGHT screen implementation. A close copy of the
// POWER SAVER screen: same geometry, same drag-to-scroll, same greyed rows
// under a master switch.
#include "ui_light.h"
#include "ui_scroll.h"
#include "theme.h"
#include "settings.h"
#include "status_light.h"
#include <Arduino.h>

static const int TOP_MARGIN = 16;
static int g_scroll = 0;

static uint8_t rowCount() { return (uint8_t)LightRow::COUNT; }
static LightRow rowAt(uint8_t i) { return (LightRow)i; }

static void computeGeom(TFT_eSPI& t, int screenH, int& top, int& bodyBottom, int& rowH) {
    top = TOP_MARGIN + Theme::listHeadingH();
    bodyBottom = screenH - Theme::pinnedBackH(t.width()) - 2;
    t.setTextSize(Theme::uiMenuTextSize(t));
    // Two pixels taller than the text strictly needs on each side: a 24 px
    // row was a near miss for a thumb, 26 is not, and seven of them still
    // fit above the BACK strip in landscape.
    rowH = t.fontHeight() + Theme::listRowPad();
}

void uiLightInit(TFT_eSPI& t) {
    g_scroll = 0;
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
}

void uiLightScroll(int delta) {
    g_scroll += delta;
    if (g_scroll < 0) g_scroll = 0;
}

static void rowContent(LightRow r, char* valBuf, size_t valBufN,
                       const char*& label, const char*& value, bool& dimmed) {
    // Everything under the master switch greys out while it is off, and stays
    // tappable: setting the light up before turning it on is a perfectly
    // reasonable order to work in.
    dimmed = !Settings::lightOn();
    value = nullptr;
    switch (r) {
        case LightRow::ENABLED:
            label = "LIGHT"; value = Settings::lightOn() ? "ON" : "OFF";
            dimmed = false;
            break;
        case LightRow::ALERTS:
            label = "ALERTS"; value = Settings::lightAlerts() ? "ON" : "OFF";
            break;
        case LightRow::MESSAGES:
            label = "MESSAGES"; value = Settings::lightMessages() ? "ON" : "OFF";
            break;
        case LightRow::IDLE:
            label = "IDLE"; value = Settings::lightIdleName();
            break;
        case LightRow::IDLE_COLOR:
            label = "IDLE COLOR"; value = Settings::lightColorName();
            break;
        case LightRow::BRIGHTNESS:
            label = "BRIGHTNESS";
            snprintf(valBuf, valBufN, "%u/7", (unsigned)Settings::lightBrightness());
            value = valBuf;
            break;
        case LightRow::TEST:
            label = "TEST"; value = "PLAY";
            break;
        default:
            label = "?";
            break;
    }
}

static void drawRow(TFT_eSPI& t, int w, int y, int hgt, LightRow r, bool compact) {
    char buf[16];
    const char* label = "";
    const char* value = nullptr;
    bool dimmed = false;
    rowContent(r, buf, sizeof(buf), label, value, dimmed);

    Theme::drawListRowPanel(t, w, y, hgt);

    Theme::subListTextSize(t, compact, label, value, w);   // FONTS > MENUS
    const uint16_t lab = dimmed ? Theme::blend(Theme::BG, Theme::CYAN, 110) : Theme::CYAN;
    t.setTextColor(lab, Theme::BG);
    t.setCursor(8, y + (hgt - t.fontHeight()) / 2);
    t.print(label);

    if (value) {
        t.setTextColor(dimmed ? Theme::blend(Theme::BG, Theme::WHITE, 110) : Theme::WHITE,
                       Theme::BG);
        int vw = t.textWidth(value);
        t.setCursor(w - 18 - vw, y + (hgt - t.fontHeight()) / 2);
        t.print(value);
    }
    t.setTextFont(1);
}

void uiLightTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng) {
    int w = t.width(), h = t.height();
    int top, bodyBottom, rowH;
    computeGeom(t, h, top, bodyBottom, rowH);

    Theme::Palette saved = Theme::dimPaletteForOverlay(179);
    const int bgTop = 0;
    if (!Theme::stillBackdrop(t)) switch (Settings::background()) {
        case Settings::Background::STARFIELD: Theme::drawStarfield(t, now, bgTop, bodyBottom); break;
        case Settings::Background::TOASTERS:  Theme::drawFlyingToasters(t, now, bgTop, bodyBottom); break;
        case Settings::Background::AQUARIUM:  Theme::drawAquarium(t, now, bgTop, bodyBottom); break;
        case Settings::Background::TERMINAL:  Theme::drawTerminalLog(t, now, bgTop, bodyBottom); break;
        case Settings::Background::FIREFLIES: Theme::drawFireflies(t, now, bgTop, bodyBottom); break;
        case Settings::Background::FIRE:      Theme::drawFire(t, now, bgTop, bodyBottom); break;
        case Settings::Background::SNOWFALL:  Theme::drawSnowfall(t, now, bgTop, bodyBottom); break;
        case Settings::Background::SPECTRUM:  Theme::drawGibson(t, now, bgTop, bodyBottom, eng); break;
        case Settings::Background::SYNTHWAVE: Theme::drawSynthwave(t, now, bgTop, bodyBottom); break;
        case Settings::Background::BLACK:      t.fillRect(0, bgTop, t.width(), bodyBottom - bgTop, Theme::BG); break;
        default:                              Theme::drawDigitalRain(t, now, bgTop, bodyBottom, true); break;
    }
    Theme::restorePalette(saved);

    Theme::drawTitleBar(t, ">> STATUS LIGHT <<");
    Theme::drawListHeading(t, "STATUS LIGHT", Theme::CYAN);

    const bool compact = (w < 300);

    uint8_t n = rowCount();
    uiClampScroll(g_scroll, n, bodyBottom - top, rowH);
    int y = top;
    int idx = g_scroll;
    int visibleCount = 0;
    while (idx < n) {
        if (y + rowH > bodyBottom) break;
        drawRow(t, w, y, rowH, rowAt((uint8_t)idx), compact);
        y += rowH;
        idx++;
        visibleCount++;
    }

    // On a board with no known LED the list still opens -- the settings are
    // real and travel with the owner's preferences -- but it says so, in the
    // space under the last row, rather than letting somebody tap TEST and
    // wonder what they missed.
    // Where the rows leave no room under them (the large panels' taller
    // heading, in landscape), it goes at the far end of the heading's line.
    Theme::bodyFontOn(t);
    if (!StatusLight::available()) {
        const char* note = "No LED known on this board yet.";
        t.setTextColor(Theme::blend(Theme::BG, Theme::WHITE, 150), Theme::BG);
        if (y + t.fontHeight() + 4 <= bodyBottom) {
            t.setCursor(8, y + 4);
            t.print(note);
        } else if (Theme::large()) {
            t.setCursor(w - 8 - t.textWidth(note), Theme::listHeadingY(t));
            t.print(note);
        }
    }
    Theme::bodyFontOff(t);

    Theme::drawScrollbar(t, w - 4, top, bodyBottom - top, n, visibleCount, g_scroll);
    Theme::drawPinnedBack(t, "[ BACK ]");
}

LightRow uiLightHitTest(TFT_eSPI& t, int x, int y, int screenW, int screenH) {
    (void)x; (void)screenW;
    int top, bodyBottom, rowH;
    computeGeom(t, screenH, top, bodyBottom, rowH);

    uint8_t n = rowCount();
    uiClampScroll(g_scroll, n, bodyBottom - top, rowH);
    int cy = top;
    int idx = g_scroll;
    while (idx < n) {
        if (cy + rowH > bodyBottom) break;
        if (y >= cy && y < cy + rowH) return rowAt((uint8_t)idx);
        cy += rowH;
        idx++;
    }
    return LightRow::NONE;
}
