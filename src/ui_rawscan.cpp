// SquachWatch-CYD — manual raw BLE/WiFi scanner screen implementation
#include "ui_rawscan.h"
#include "theme.h"
#include "squachy.h"
#include "settings.h"
#include <Arduino.h>

static int g_scroll = 0;

// Shared by rowLayout() and uiRawScanTick() so the two can never
// drift apart -- see rowLayout()'s own comment.
static const int RS_TITLE_BOTTOM = 16;
static const int RS_SQ_H         = 70;

// Two buttons in place of the normal three-slot bar: BACK (left) and
// the opposite scan mode (right) -- reuses computeButtonBar()'s y/h so
// the row lines up with every other screen's bar, just two wide
// buttons instead of three narrower ones.
static void bottomButtonRects(int screenW, int screenH,
                               int& backX, int& backY, int& backW, int& backH,
                               int& altX, int& altY, int& altW, int& altH) {
    Theme::ButtonBarGeom g = Theme::computeButtonBar(screenW, screenH);
    const int margin = 8, gap = 8;
    int bw = (screenW - 2 * margin - gap) / 2;
    backX = margin;              backY = g.y; backW = bw; backH = g.h;
    altX  = backX + bw + gap;    altY  = g.y; altW  = bw; altH  = g.h;
}

// Tracked across ticks so uiRawScanTick can fire Squachy's reactions
// on the actual moment they happen (a new BLE device appearing, the
// scan finishing) instead of every single frame.
static uint8_t g_lastBleCount = 0;
static bool    g_reportedDone = false;

void uiRawScanInit(TFT_eSPI& t, bool isBle) {
    g_scroll = 0;
    g_lastBleCount = 0;
    g_reportedDone = false;
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
    Squachy::scanReaction(Squachy::ScanMoment::STARTED);
}

void uiRawScanScroll(int delta) {
    g_scroll += delta;
    if (g_scroll < 0) g_scroll = 0;
}

RawScanTap uiRawScanHitTest(int x, int y, int screenW, int screenH) {
    int bx, by, bw, bh, ax, ay, aw, ah;
    bottomButtonRects(screenW, screenH, bx, by, bw, bh, ax, ay, aw, ah);
    if (x >= bx && x <= bx + bw && y >= by && y <= by + bh) return RawScanTap::BACK;
    if (x >= ax && x <= ax + aw && y >= ay && y <= ay + ah) return RawScanTap::SWITCH;
    return RawScanTap::NONE;
}

static void drawBottomBar(TFT_eSPI& t, int w, int h, bool isBle) {
    int bx, by, bw, bh, ax, ay, aw, ah;
    bottomButtonRects(w, h, bx, by, bw, bh, ax, ay, aw, ah);
    Theme::drawButton(t, bx, by, bw, bh, "[ BACK ]", false);
    Theme::drawButton(t, ax, ay, aw, ah, isBle ? "[ WIFI ]" : "[ BLE ]", false);
}

// Confirm panel geometry, shared by its drawing (in uiRawScanTick())
// and uiRawScanHitConfirm() below. Three buttons in the same row --
// labels dropped their "[ ]" brackets (unlike Theme::drawButton's
// other callers) purely to fit "CANCEL" at this width on the
// narrowest portrait rotation (pw=200 -> btnW~53px).
static void confirmRects(int screenW, int screenH,
                          int& px, int& py, int& pw, int& ph,
                          int& wX, int& wY, int& wW, int& wH,
                          int& huX, int& huY, int& huW, int& huH,
                          int& igX, int& igY, int& igW, int& igH,
                          int& cnX, int& cnY, int& cnW, int& cnH) {
    pw = screenW - 40;
    if (pw > 240) pw = 240;
    // Two rows of two rather than one row of four. A fourth button in the
    // single row would be ~40px wide on the narrowest portrait rotation,
    // which is below a reliable finger target.
    ph = 124;
    px = (screenW - pw) / 2;
    py = (screenH - ph) / 2;
    const int margin = 10, gap = 10, btnH = 26;
    int btnW = (pw - 2 * margin - gap) / 2;
    igY = cnY = py + ph - btnH - margin;
    igH = cnH = btnH;
    igX = px + margin;        igW = btnW;
    cnX = igX + btnW + gap;   cnW = btnW;
    wY = huY = igY - gap - btnH;
    wH = huH = btnH;
    wX  = px + margin;        wW  = btnW;
    huX = wX + btnW + gap;    huW = btnW;
}

RawScanConfirmTap uiRawScanHitConfirm(int x, int y, int screenW, int screenH) {
    int px, py, pw, ph, wX, wY, wW, wH, huX, huY, huW, huH, igX, igY, igW, igH, cnX, cnY, cnW, cnH;
    confirmRects(screenW, screenH, px, py, pw, ph, wX, wY, wW, wH, huX, huY, huW, huH,
                 igX, igY, igW, igH, cnX, cnY, cnW, cnH);
    if (x >= wX && x <= wX + wW && y >= wY && y <= wY + wH) return RawScanConfirmTap::WATCH;
    if (x >= huX && x <= huX + huW && y >= huY && y <= huY + huH) return RawScanConfirmTap::HUNT;
    if (x >= igX && x <= igX + igW && y >= igY && y <= igY + igH) return RawScanConfirmTap::IGNORE;
    if (x >= cnX && x <= cnX + cnW && y >= cnY && y <= cnY + cnH) return RawScanConfirmTap::CANCEL;
    return RawScanConfirmTap::NONE;
}

// Drawn last, on top of whatever else this tick already drew (the
// scanning state, the empty state, or the results list) -- called
// right before every return point in uiRawScanTick() rather than
// restructuring those into a single shared tail.
static void drawConfirmPanel(TFT_eSPI& t, int w, int h, const char* label) {
    int px, py, pw, ph, wX, wY, wW, wH, huX, huY, huW, huH, igX, igY, igW, igH, cnX, cnY, cnW, cnH;
    confirmRects(w, h, px, py, pw, ph, wX, wY, wW, wH, huX, huY, huW, huH,
                 igX, igY, igW, igH, cnX, cnY, cnW, cnH);
    t.fillRoundRect(px, py, pw, ph, 6, Theme::BG);
    t.drawRoundRect(px, py, pw, ph, 6, Theme::PURPLE);

    t.setTextSize(1);
    t.setTextWrap(false);
    t.setTextColor(Theme::CYAN, Theme::BG);
    const char* q = "TRACK THIS TARGET?";
    int qw = t.textWidth(q);
    t.setCursor(px + (pw - qw) / 2, py + 8);
    t.print(q);

    t.setTextColor(Theme::WHITE, Theme::BG);
    int lw = t.textWidth(label);
    int maxLw = pw - 16;
    int lx = px + (pw - (lw < maxLw ? lw : maxLw)) / 2;
    t.setCursor(lx, py + 24);
    t.print(label);

    Theme::drawButton(t, wX, wY, wW, wH, "WATCH", false);
    Theme::drawButton(t, huX, huY, huW, huH, "HUNT", false);
    Theme::drawButton(t, igX, igY, igW, igH, "IGNORE", false);
    Theme::drawButton(t, cnX, cnY, cnW, cnH, "CANCEL", false);
}

// Shared by uiRawScanTick() (drawing) and uiRawScanRowAt() (hit
// testing) so the two can never drift apart -- rowH depends on live
// font metrics (see the fontHeight() comment below), which is why
// this needs a real TFT_eSPI& rather than being a compile-time
// constant.
static void rowLayout(TFT_eSPI& t, int w, int h, int& bodyTop, int& bodyBottom, int& rowH) {
    Theme::ButtonBarGeom bar = Theme::computeButtonBar(w, h);
    bodyTop    = RS_TITLE_BOTTOM + RS_SQ_H;
    bodyBottom = bar.y - 4;

    // fontHeight() with NO argument -- fontHeight(int) takes a font
    // INDEX (2 means "built-in font #2", not "current font at size
    // 2"), which silently queries an unrelated font's metrics instead
    // of the GLCD font actually drawn here. The no-arg overload
    // correctly reads back whatever font+size is currently active.
    t.setTextSize(2);
    int nameH = t.fontHeight();
    t.setTextSize(1);
    int detailH = t.fontHeight();
    rowH = 1 /* topPad */ + nameH + detailH + 2;
}

// Row index (0 = topmost visible, adjusted for current scroll) a tap
// at (x,y) falls within, or -1 if it's outside the list entirely (the
// Squachy strip, the button bar, or past the last row). Used to
// disambiguate a long-press-to-watch gesture from the drag-to-scroll
// one -- see main.cpp's RAWSCAN case.
int uiRawScanRowAt(TFT_eSPI& t, int x, int y, int screenW, int screenH) {
    int bodyTop, bodyBottom, rowH;
    rowLayout(t, screenW, screenH, bodyTop, bodyBottom, rowH);
    if (y < bodyTop || y >= bodyBottom) return -1;
    return g_scroll + (y - bodyTop) / rowH;
}

void uiRawScanTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng, bool isBle, bool done,
                    bool confirmPending, const char* confirmLabel) {
    int w = t.width();
    int h = t.height();

    // Squachy gets a small strip of his own below the title bar -- a
    // mini version of his CLEAR-screen self (minScale below his usual
    // scale-1.0 floor, see squachy.h), at 80% of his normal size. sqH
    // is sized so that scale comfortably lands at ~0.8 on its own
    // (charAvail = sqH - his 16px bubble row =~ BASE_HEIGHT * 0.8);
    // minScale is there as a floor, not the thing forcing this size.
    const int sqTop = RS_TITLE_BOTTOM;
    const int sqH    = RS_SQ_H;
    int bodyTop, bodyBottom, rowH;
    rowLayout(t, w, h, bodyTop, bodyBottom, rowH);
    const int bodyH = bodyBottom - bodyTop;

    char title[32];
    snprintf(title, sizeof(title), ">> %s SCAN <<", isBle ? "BLE" : "WIFI");
    Theme::drawTitleBar(t, title);

    // Fire Squachy's reactions on the actual moment they happen, not
    // every frame: a new BLE device appearing (WiFi's count only ever
    // jumps once, all at once, when the sweep finishes, so this is
    // BLE-only), and the scan finishing (latched via g_reportedDone so
    // it only fires once per scan, whichever mode).
    if (isBle && !done) {
        uint8_t n = eng.rawBleCount();
        if (n > g_lastBleCount) Squachy::scanReaction(Squachy::ScanMoment::HIT);
        g_lastBleCount = n;
    }
    if (done && !g_reportedDone) {
        g_reportedDone = true;
        uint8_t n = isBle ? eng.rawBleCount() : eng.rawWifiCount();
        Squachy::scanReaction(n > 0 ? Squachy::ScanMoment::DONE_FOUND : Squachy::ScanMoment::DONE_EMPTY, n);
    }

    // Whatever background style is picked in Settings, drawn ONCE as a
    // single continuous pass from just below the title bar all the way
    // to the bottom of the list (sqTop..bodyBottom) -- Squachy's strip
    // and the results list used to each get their own separate call
    // (the strip via a setViewport crop of a *full-screen-scaled*
    // render, the list via its own bodyTop..bodyBottom-scaled render),
    // which are two different scalings/phases of the "same" effect and
    // read as two disconnected backgrounds stacked on top of each
    // other, with a visible seam and brightness jump where they met.
    // One call means one continuous scaling, so it reads as one scene
    // Squachy happens to be standing in front of instead of a patchwork.
    // Dimmed the same way Settings'/LOG's bodies are (see
    // Theme::dimPaletteForOverlay()'s comment) -- needed for the list
    // text below to stay legible, and looks fine behind Squachy too;
    // none of the states below (scanning/empty/results) draw an opaque
    // full-row fill of their own, just a drawFastHLine separator and
    // per-field two-arg setTextColor() calls that already erase their
    // own glyph cells.
    Theme::Palette saved = Theme::dimPaletteForOverlay(179);
    switch (Settings::background()) {
        case Settings::Background::STARFIELD: Theme::drawStarfield(t, now, sqTop, bodyBottom); break;
        case Settings::Background::TOASTERS:   Theme::drawFlyingToasters(t, now, sqTop, bodyBottom); break;
        case Settings::Background::AQUARIUM:   Theme::drawAquarium(t, now, sqTop, bodyBottom); break;
        case Settings::Background::TERMINAL:   Theme::drawTerminalLog(t, now, sqTop, bodyBottom); break;
        case Settings::Background::FIREFLIES:  Theme::drawFireflies(t, now, sqTop, bodyBottom); break;
        case Settings::Background::FIRE:       Theme::drawFire(t, now, sqTop, bodyBottom); break;
        case Settings::Background::SNOWFALL:   Theme::drawSnowfall(t, now, sqTop, bodyBottom); break;
        case Settings::Background::SPECTRUM:   Theme::drawSpectrumWaterfall(t, now, sqTop, bodyBottom, eng); break;
        case Settings::Background::TUNNEL:     Theme::drawWireframeTunnel(t, now, sqTop, bodyBottom); break;
        case Settings::Background::SYNTHWAVE: Theme::drawSynthwave(t, now, sqTop, bodyBottom); break;
        default:                               Theme::drawDigitalRain(t, now, sqTop, bodyBottom, true); break;
    }
    Theme::restorePalette(saved);

    // No viewport needed now that the background above already covers
    // this whole strip directly at its real screen position -- drawn
    // straight on top of it, same as ui_hunt.cpp/ui_watchalert.cpp's
    // mini cameos already do.
    Squachy::tick(t, w / 2, sqTop, sqH, now, true, 0.8f, !done);

    if (!done) {
        float pulse = 0.55f + 0.45f * sinf((float)(now % 2000) / 2000.0f * 6.2831853f);
        uint16_t col = Theme::blend(Theme::GREEN, Theme::CYAN, (uint16_t)(pulse * 200.0f));
        t.setTextSize(3);
        t.setTextColor(col, Theme::BG);
        const char* msg = "SCANNING...";
        int mw = t.textWidth(msg);
        t.setCursor((w - mw) / 2, bodyTop + bodyH / 3);
        t.print(msg);

        if (isBle) {
            char sub[24];
            snprintf(sub, sizeof(sub), "%u found so far", (unsigned)eng.rawBleCount());
            t.setTextSize(1);
            t.setTextColor(Theme::CYAN, Theme::BG);
            int sw = t.textWidth(sub);
            t.setCursor((w - sw) / 2, bodyTop + bodyH / 3 + 35);
            t.print(sub);
        }

        drawBottomBar(t, w, h, isBle);
        if (confirmPending) drawConfirmPanel(t, w, h, confirmLabel);
        return;
    }

    uint8_t count = isBle ? eng.rawBleCount() : eng.rawWifiCount();

    if (count == 0) {
        t.setTextSize(3);
        t.setTextColor(Theme::VAPOR_PINK, Theme::BG);
        const char* msg = "NOTHING FOUND";
        int mw = t.textWidth(msg);
        t.setCursor((w - mw) / 2, bodyTop + bodyH / 3);
        t.print(msg);

        drawBottomBar(t, w, h, isBle);
        if (confirmPending) drawConfirmPanel(t, w, h, confirmLabel);
        return;
    }

    const int topPad = 1;
    t.setTextSize(2);
    int detailY = topPad + t.fontHeight();
    // Off, not the TFT_eSPI default (on) -- a long SSID or BLE device
    // name otherwise wraps onto a second line at this width, which
    // pushes into (or reads as extra space before) the detail line
    // below it. Long names just run off the right edge instead, same
    // as everywhere else in this file already relies on clipping.
    t.setTextWrap(false);

    int y = bodyTop;
    int idx = g_scroll;
    int max = (bodyH / rowH);

    for (int i = 0; i < max && idx < count; i++, idx++) {
        t.drawFastHLine(0, y + rowH - 1, w, Theme::PURPLE);

        if (isBle) {
            const RawBleResult* r = eng.rawBleAt(idx);
            if (!r) break;
            t.setTextSize(2);
            t.setTextColor(Theme::CYAN, Theme::BG);
            t.setCursor(4, y + topPad);
            t.print(r->name[0] ? r->name : "(unnamed)");

            t.setTextSize(1);
            t.setTextColor(Theme::WHITE, Theme::BG);
            char mac[24];
            snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
                     r->mac[0], r->mac[1], r->mac[2], r->mac[3], r->mac[4], r->mac[5]);
            t.setCursor(4, y + detailY);
            t.print(mac);

            t.setTextColor(Theme::VAPOR_PURPLE, Theme::BG);
            char rssi[12];
            snprintf(rssi, sizeof(rssi), "%ddBm", r->rssi);
            int rw = t.textWidth(rssi);
            t.setCursor(w - rw - 14, y + topPad);
            t.print(rssi);
        } else {
            t.setTextSize(2);
            t.setTextColor(Theme::CYAN, Theme::BG);
            t.setCursor(4, y + topPad);
            t.print(eng.rawWifiSsid(idx));

            t.setTextSize(1);
            t.setTextColor(Theme::WHITE, Theme::BG);
            char line[24];
            snprintf(line, sizeof(line), "CH%u  %s", (unsigned)eng.rawWifiChannel(idx),
                     eng.rawWifiOpen(idx) ? "OPEN" : "LOCKED");
            t.setCursor(4, y + detailY);
            t.print(line);

            t.setTextColor(Theme::VAPOR_PURPLE, Theme::BG);
            char rssi[12];
            snprintf(rssi, sizeof(rssi), "%ddBm", eng.rawWifiRssi(idx));
            int rw = t.textWidth(rssi);
            t.setCursor(w - rw - 14, y + topPad);
            t.print(rssi);
        }

        y += rowH;
    }

    Theme::drawScrollbar(t, w - 4, bodyTop, bodyH, count, max, g_scroll);

    drawBottomBar(t, w, h, isBle);
    if (confirmPending) drawConfirmPanel(t, w, h, confirmLabel);
}
