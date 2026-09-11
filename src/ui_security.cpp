// SquachWatch-CYD — SECURITY submenu. See include/ui_security.h.
#include "ui_security.h"
#include "theme.h"
#include "settings.h"
#include "security.h"
#include <Arduino.h>

static const int TOP_MARGIN = 16;
static int g_scroll = 0;

static uint8_t rowCount() { return (uint8_t)SecurityRow::COUNT; }
static SecurityRow rowAt(uint8_t i) { return (SecurityRow)i; }

static void computeGeom(TFT_eSPI& t, int screenH, int& top, int& bodyBottom, int& rowH) {
    top = TOP_MARGIN;
    bodyBottom = screenH - 4;
    t.setTextSize(2);
    rowH = t.fontHeight() + 8;
}

void uiSecurityInit(TFT_eSPI& t) {
    g_scroll = 0;
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
}

void uiSecurityScroll(int delta) {
    g_scroll += delta;
    if (g_scroll < 0) g_scroll = 0;
}

// A row's label, its value, and whether it is greyed out. Everything past the
// switch is dim until a PIN exists; PIN LENGTH is dim once one does, because
// the length is baked into the stored hash and cannot change under it.
static void rowContent(SecurityRow r, char* buf, size_t bufN,
                       const char*& label, const char*& value, bool& dimmed) {
    const bool on = Security::enabled();
    dimmed = !on;
    value = nullptr;
    switch (r) {
        case SecurityRow::PIN_LOCK:
            label = "PIN LOCK"; value = on ? "ON" : "OFF"; dimmed = false;
            break;
        case SecurityRow::PIN_LENGTH:
            label = "PIN LENGTH";
            snprintf(buf, bufN, "%u", (unsigned)Security::pinLength());
            value = buf;
            dimmed = on;    // fixed once a PIN is set
            break;
        case SecurityRow::CHANGE_PIN:
            label = "CHANGE PIN"; value = on ? ">" : "--";
            break;
        case SecurityRow::DURESS_PIN:
            label = "DURESS PIN"; value = Security::hasDuress() ? "ON" : "OFF";
            break;
        case SecurityRow::AUTO_LOCK:
            label = "AUTO-LOCK"; value = Security::autoLockLabel();
            break;
        case SecurityRow::LOCK_AT_BOOT:
            label = "LOCK AT BOOT"; value = Security::lockAtBoot() ? "ON" : "OFF";
            break;
        case SecurityRow::WIPE_ON_FAIL:
            label = "WIPE AFTER 10"; value = Security::wipeOnFail() ? "ON" : "OFF";
            break;
        case SecurityRow::LOCK_ALERTS:
            label = "ALERTS LOCKED"; value = Security::lockAlertsLabel();
            break;
        default: label = "?"; break;
    }
}

static void drawRow(TFT_eSPI& t, int w, int y, int hgt, SecurityRow r, bool compact) {
    char buf[12];
    const char* label = "";
    const char* value = nullptr;
    bool dimmed = false;
    rowContent(r, buf, sizeof(buf), label, value, dimmed);

    t.setTextSize(compact ? 1 : 2);
    const uint16_t lab = dimmed ? Theme::blend(Theme::BG, Theme::CYAN, 110)
                                : (r == SecurityRow::PIN_LOCK ? Theme::AMBER : Theme::CYAN);
    t.setTextColor(lab, Theme::BG);
    t.setCursor(8, y + (hgt - t.fontHeight()) / 2);
    t.print(label);

    if (value) {
        uint16_t vc = dimmed ? Theme::blend(Theme::BG, Theme::WHITE, 110) : Theme::WHITE;
        // DURESS ON and WIPE ON are the two that erase; flag them red so a
        // glance down the list shows which switches bite.
        if (!dimmed &&
            ((r == SecurityRow::DURESS_PIN && Security::hasDuress()) ||
             (r == SecurityRow::WIPE_ON_FAIL && Security::wipeOnFail())))
            vc = Theme::RED;
        t.setTextColor(vc, Theme::BG);
        int vw = t.textWidth(value);
        t.setCursor(w - 18 - vw, y + (hgt - t.fontHeight()) / 2);
        t.print(value);
    }
    t.drawFastHLine(4, y + hgt - 1, w - 8, Theme::PURPLE);
}

void uiSecurityTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng) {
    int w = t.width(), h = t.height();
    int top, bodyBottom, rowH;
    computeGeom(t, h, top, bodyBottom, rowH);

    Theme::Palette saved = Theme::dimPaletteForOverlay(179);
    const int bgTop = 0;
    switch (Settings::background()) {
        case Settings::Background::STARFIELD: Theme::drawStarfield(t, now, bgTop, bodyBottom); break;
        case Settings::Background::TOASTERS:  Theme::drawFlyingToasters(t, now, bgTop, bodyBottom); break;
        case Settings::Background::AQUARIUM:  Theme::drawAquarium(t, now, bgTop, bodyBottom); break;
        case Settings::Background::TERMINAL:  Theme::drawTerminalLog(t, now, bgTop, bodyBottom); break;
        case Settings::Background::FIREFLIES: Theme::drawFireflies(t, now, bgTop, bodyBottom); break;
        case Settings::Background::FIRE:      Theme::drawFire(t, now, bgTop, bodyBottom); break;
        case Settings::Background::SNOWFALL:  Theme::drawSnowfall(t, now, bgTop, bodyBottom); break;
        case Settings::Background::TUNNEL:    Theme::drawWireframeTunnel(t, now, bgTop, bodyBottom); break;
        case Settings::Background::SPECTRUM:  Theme::drawGibson(t, now, bgTop, bodyBottom, eng); break;
        case Settings::Background::SYNTHWAVE: Theme::drawSynthwave(t, now, bgTop, bodyBottom); break;
        case Settings::Background::BLACK:     t.fillRect(0, bgTop, w, bodyBottom - bgTop, Theme::BG); break;
        default:                              Theme::drawDigitalRain(t, now, bgTop, bodyBottom, true); break;
    }
    Theme::restorePalette(saved);

    Theme::drawTitleBar(t, ">> SECURITY <<");

    const bool compact = (w < 300);
    uint8_t n = rowCount();
    int y = top, idx = g_scroll, visibleCount = 0;
    while (idx < n) {
        if (y + rowH > bodyBottom) break;
        drawRow(t, w, y, rowH, rowAt((uint8_t)idx), compact);
        y += rowH; idx++; visibleCount++;
    }
    Theme::drawScrollbar(t, w - 4, top, bodyBottom - top, n, visibleCount, g_scroll);
}

SecurityRow uiSecurityHitTest(TFT_eSPI& t, int x, int y, int screenW, int screenH) {
    (void)x; (void)screenW;
    int top, bodyBottom, rowH;
    computeGeom(t, screenH, top, bodyBottom, rowH);
    uint8_t n = rowCount();
    int cy = top, idx = g_scroll;
    while (idx < n) {
        if (cy + rowH > bodyBottom) break;
        if (y >= cy && y < cy + rowH) return rowAt((uint8_t)idx);
        cy += rowH; idx++;
    }
    return SecurityRow::NONE;
}
