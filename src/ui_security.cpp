// SquachWatch-CYD — SECURITY submenu. See include/ui_security.h.
#include "ui_security.h"
#include "ui_scroll.h"
#include "theme.h"
#include "settings.h"
#include "security.h"
#include <Arduino.h>

static const int TOP_MARGIN = 16;
static int g_scroll = 0;

static uint8_t rowCount() { return (uint8_t)SecurityRow::COUNT; }
static SecurityRow rowAt(uint8_t i) { return (SecurityRow)i; }

static void computeGeom(TFT_eSPI& t, int screenH, int& top, int& bodyBottom, int& rowH) {
    top = TOP_MARGIN + Theme::listHeadingH();
    bodyBottom = screenH - Theme::pinnedBackH(t.width()) - 2;
    t.setTextSize(Theme::uiMenuTextSize(t));
    // Two pixels taller than the text strictly needs on each side: a 24 px
    // row was a near miss for a thumb, 26 is not, and seven of them still
    // fit above the BACK strip in landscape.
    rowH = t.fontHeight() + Theme::listRowPad();
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
        case SecurityRow::REMOTE_UPDATE:
            // Not gated on the PIN: it is a permission, not a lock feature.
            label = "REMOTE UPDATE"; value = Settings::remoteUpdate() ? "ON" : "OFF";
            dimmed = false;
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

    Theme::drawListRowPanel(t, w, y, hgt);

    Theme::subListTextSize(t, compact, label, value, w);   // FONTS > MENUS
    const uint16_t lab = dimmed ? Theme::blend(Theme::BG, Theme::VAPOR_PURPLE, 110) : Theme::VAPOR_PURPLE;
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
    t.setTextFont(1);
}

void uiSecurityTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng) {
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
        case Settings::Background::BLACK:     t.fillRect(0, bgTop, w, bodyBottom - bgTop, Theme::BG); break;
        default:                              Theme::drawDigitalRain(t, now, bgTop, bodyBottom, true); break;
    }
    Theme::restorePalette(saved);

    Theme::drawTitleBar(t, ">> SECURITY <<");
    Theme::drawListHeading(t, "SECURITY", Theme::VAPOR_PURPLE);

    const bool compact = (w < 300);
    uint8_t n = rowCount();
    uiClampScroll(g_scroll, n, bodyBottom - top, rowH);
    int y = top, idx = g_scroll, visibleCount = 0;
    while (idx < n) {
        if (y + rowH > bodyBottom) break;
        drawRow(t, w, y, rowH, rowAt((uint8_t)idx), compact);
        y += rowH; idx++; visibleCount++;
    }
    Theme::drawScrollbar(t, w - 4, top, bodyBottom - top, n, visibleCount, g_scroll);
    Theme::drawPinnedBack(t, "[ BACK ]");
}

SecurityRow uiSecurityHitTest(TFT_eSPI& t, int x, int y, int screenW, int screenH) {
    (void)x; (void)screenW;
    int top, bodyBottom, rowH;
    computeGeom(t, screenH, top, bodyBottom, rowH);
    uint8_t n = rowCount();
    uiClampScroll(g_scroll, n, bodyBottom - top, rowH);
    int cy = top, idx = g_scroll;
    while (idx < n) {
        if (cy + rowH > bodyBottom) break;
        if (y >= cy && y < cy + rowH) return rowAt((uint8_t)idx);
        cy += rowH; idx++;
    }
    return SecurityRow::NONE;
}
