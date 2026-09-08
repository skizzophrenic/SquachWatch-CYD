// SquachWatch-CYD — POWER SAVER screen implementation
#include "ui_power.h"
#include "theme.h"
#include "settings.h"
#include <Arduino.h>

static const int TOP_MARGIN = 16;
static int g_scroll = 0;

static uint8_t rowCount() { return (uint8_t)PowerRow::COUNT; }
static PowerRow rowAt(uint8_t i) { return (PowerRow)i; }

// Same fixed-height-at-size-2 approach Settings and the type filter use --
// needs a live TFT_eSPI& since it depends on real font metrics, and is
// shared by drawing and hit-testing so the two cannot drift apart.
static void computeGeom(TFT_eSPI& t, int screenH, int& top, int& bodyBottom, int& rowH) {
    top = TOP_MARGIN;
    bodyBottom = screenH - 4;
    t.setTextSize(2);
    rowH = t.fontHeight() + 8;
}

void uiPowerInit(TFT_eSPI& t) {
    g_scroll = 0;
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
}

void uiPowerScroll(int delta) {
    g_scroll += delta;
    if (g_scroll < 0) g_scroll = 0;
}

// Fills in one row's label and value. valBuf is scratch for the rows that
// format a number, valid only for the caller's current loop iteration.
static void rowContent(PowerRow r, char* valBuf, size_t valBufN,
                       const char*& label, const char*& value, bool& dimmed) {
    // Everything except the master switch is greyed out while the master
    // switch is off. They are still tappable -- setting them up before
    // turning the feature on is a perfectly reasonable order to work in --
    // but they say plainly that nothing is acting on them yet.
    dimmed = !Settings::powerSaver();
    value = nullptr;
    switch (r) {
        case PowerRow::ENABLED:
            label = "LOW POWER"; value = Settings::powerSaver() ? "ON" : "OFF";
            dimmed = false;
            break;
        case PowerRow::SCREEN_TIMEOUT: {
            label = "SCREEN TIMEOUT";
            // Read the raw table, not the gated getter: this row has to show
            // what is configured even while the master switch is off.
            uint16_t sec = Settings::powerSaver() ? Settings::screenTimeoutSec()
                                                  : Settings::screenTimeoutSecRaw();
            if (!sec)            snprintf(valBuf, valBufN, "NEVER");
            else if (sec < 60)   snprintf(valBuf, valBufN, "%us", (unsigned)sec);
            else                 snprintf(valBuf, valBufN, "%umin", (unsigned)(sec / 60));
            value = valBuf;
            break;
        }
        case PowerRow::DIM_LEVEL:
            label = "DIM TO -  +";
            if (!Settings::dimLevel()) snprintf(valBuf, valBufN, "OFF");
            else snprintf(valBuf, valBufN, "%u%%", (unsigned)(Settings::dimLevel() * 100 / 255));
            value = valBuf;
            break;
        case PowerRow::IDLE_FPS: {
            label = "IDLE FRAMES";
            uint8_t f = Settings::powerSaver() ? Settings::idleFps() : Settings::idleFpsRaw();
            if (!f) snprintf(valBuf, valBufN, "FULL");
            else    snprintf(valBuf, valBufN, "%u fps", (unsigned)f);
            value = valBuf;
            break;
        }
        case PowerRow::IDLE_AFTER:
            label = "IDLE AFTER";
            snprintf(valBuf, valBufN, "%us", (unsigned)Settings::idleAfterSec());
            value = valBuf;
            break;
        case PowerRow::CPU_CLOCK: {
            label = "CPU CLOCK";
            uint16_t m = Settings::powerSaver() ? Settings::cpuMhz() : Settings::cpuMhzRaw();
            snprintf(valBuf, valBufN, "%u MHz", (unsigned)m);
            value = valBuf;
            break;
        }
        case PowerRow::WAKE_ON_ALERT:
            label = "WAKE ON ALERT"; value = Settings::wakeOnAlert() ? "ON" : "OFF";
            break;
        default:
            label = "?";
            break;
    }
}

static void drawRow(TFT_eSPI& t, int w, int y, int hgt, PowerRow r, bool compact) {
    char buf[16];
    const char* label = "";
    const char* value = nullptr;
    bool dimmed = false;
    rowContent(r, buf, sizeof(buf), label, value, dimmed);

    t.setTextSize(compact ? 1 : 2);
    // The master switch keeps full contrast at all times; the rest fade to
    // the dim grey the rest of the UI already uses for inactive text.
    const uint16_t lab = dimmed ? Theme::blend(Theme::BG, Theme::CYAN, 110)
                                : (r == PowerRow::ENABLED ? Theme::AMBER : Theme::CYAN);
    t.setTextColor(lab, Theme::BG);
    t.setCursor(8, y + (hgt - t.fontHeight()) / 2);
    t.print(label);

    if (value) {
        t.setTextColor(dimmed ? Theme::blend(Theme::BG, Theme::WHITE, 110) : Theme::WHITE,
                       Theme::BG);
        int vw = t.textWidth(value);
        // 18px reserved on the right, same as Settings: leaves room for the
        // scrollbar without the value running under it.
        t.setCursor(w - 18 - vw, y + (hgt - t.fontHeight()) / 2);
        t.print(value);
    }
    t.drawFastHLine(4, y + hgt - 1, w - 8, Theme::PURPLE);
}

void uiPowerTick(TFT_eSPI& t, uint32_t now) {
    int w = t.width(), h = t.height();
    int top, bodyBottom, rowH;
    computeGeom(t, h, top, bodyBottom, rowH);

    // Same dimmed live backdrop the other list screens use, so this does not
    // become the one flat grey rectangle in the firmware. SPECTRUM is skipped
    // for the same reason the type filter skips it: it needs a live
    // DetectionEngine& this screen has no reason to be handed.
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
switch (Settings::background()) {
        case Settings::Background::STARFIELD: Theme::drawStarfield(t, now, bgTop, bodyBottom); break;
        case Settings::Background::TOASTERS:  Theme::drawFlyingToasters(t, now, bgTop, bodyBottom); break;
        case Settings::Background::AQUARIUM:  Theme::drawAquarium(t, now, bgTop, bodyBottom); break;
        case Settings::Background::TERMINAL:  Theme::drawTerminalLog(t, now, bgTop, bodyBottom); break;
        case Settings::Background::FIREFLIES: Theme::drawFireflies(t, now, bgTop, bodyBottom); break;
        case Settings::Background::FIRE:      Theme::drawFire(t, now, bgTop, bodyBottom); break;
        case Settings::Background::SNOWFALL:  Theme::drawSnowfall(t, now, bgTop, bodyBottom); break;
        case Settings::Background::TUNNEL:    Theme::drawWireframeTunnel(t, now, bgTop, bodyBottom); break;
        case Settings::Background::SYNTHWAVE: Theme::drawSynthwave(t, now, bgTop, bodyBottom); break;
        default:                              Theme::drawDigitalRain(t, now, bgTop, bodyBottom, true); break;
    }
    Theme::restorePalette(saved);

    Theme::drawTitleBar(t, ">> POWER SAVER <<");

    // Portrait is 240px wide, which is not enough for "SCREEN TIMEOUT" and
    // its value side by side at size 2's 12px per glyph -- the same collision
    // the Settings rows already drop to size 1 to avoid.
    const bool compact = (w < 300);

    uint8_t n = rowCount();
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

    Theme::drawScrollbar(t, w - 4, top, bodyBottom - top, n, visibleCount, g_scroll);
}

PowerRow uiPowerHitTest(TFT_eSPI& t, int x, int y, int screenW, int screenH) {
    (void)x; (void)screenW;
    int top, bodyBottom, rowH;
    computeGeom(t, screenH, top, bodyBottom, rowH);

    uint8_t n = rowCount();
    int cy = top;
    int idx = g_scroll;
    while (idx < n) {
        if (cy + rowH > bodyBottom) break;
        if (y >= cy && y < cy + rowH) return rowAt((uint8_t)idx);
        cy += rowH;
        idx++;
    }
    return PowerRow::NONE;
}
