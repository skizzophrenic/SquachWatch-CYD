// SquachWatch-CYD — log screen implementation
#include "ui_log.h"
#include "theme.h"
#include "settings.h"
#include <Arduino.h>
#include <ctype.h>

static int g_scroll = 0;

void uiLogInit(TFT_eSPI& t) {
    g_scroll = 0;
    // fillScreen() relies on TFT_eSPI's base-class width/height, which
    // TFT_eSprite::createSprite() never updates — it leaves stale
    // remnants of whatever screen was drawn before when t is a sprite.
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
}

void uiLogScroll(int delta) {
    g_scroll += delta;
    if (g_scroll < 0) g_scroll = 0;
}

// Confirm panel geometry/drawing/hit-test. A 2x2 grid (WATCH/HUNT on
// top, INFO/CANCEL below) rather than the single row of three
// ui_rawscan.cpp's identical-minus-INFO panel uses (see that module's
// own comment for why it doesn't get a fourth button) -- a single row
// of four got too cramped on the narrowest 240px rotation once CANCEL
// had to share the row with three other labels.
static void confirmRects(int screenW, int screenH,
                          int& px, int& py, int& pw, int& ph,
                          int& wX, int& wY, int& wW, int& wH,
                          int& huX, int& huY, int& huW, int& huH,
                          int& infX, int& infY, int& infW, int& infH,
                          int& igX, int& igY, int& igW, int& igH,
                          int& cnX, int& cnY, int& cnW, int& cnH) {
    pw = screenW - 40;
    if (pw > 240) pw = 240;
    // Was 132 for a 2x2 grid. IGNORE makes it three rows: WATCH/HUNT,
    // IGNORE/MORE INFO, then CANCEL alone across the bottom. CANCEL gets
    // the full width because it is the one button you hit by reflex and
    // the one that must never be mistaken for its neighbour.
    ph = 164;
    px = (screenW - pw) / 2;
    py = (screenH - ph) / 2;
    const int margin = 10, gap = 8, btnH = 24;
    int btnW = (pw - 2 * margin - gap) / 2;

    cnY = py + ph - btnH - margin;
    cnH = btnH;
    cnX = px + margin;
    cnW = pw - 2 * margin;

    igY = infY = cnY - gap - btnH;
    igH = infH = btnH;
    igX  = px + margin;       igW  = btnW;
    infX = igX + btnW + gap;  infW = btnW;

    wY = huY = igY - gap - btnH;
    wH = huH = btnH;
    wX  = px + margin;      wW  = btnW;
    huX = wX + btnW + gap;  huW = btnW;
}

LogConfirmTap uiLogHitConfirm(int x, int y, int screenW, int screenH) {
    int px, py, pw, ph, wX, wY, wW, wH, huX, huY, huW, huH, infX, infY, infW, infH,
        igX, igY, igW, igH, cnX, cnY, cnW, cnH;
    confirmRects(screenW, screenH, px, py, pw, ph, wX, wY, wW, wH, huX, huY, huW, huH,
                 infX, infY, infW, infH, igX, igY, igW, igH, cnX, cnY, cnW, cnH);
    if (x >= wX && x <= wX + wW && y >= wY && y <= wY + wH) return LogConfirmTap::WATCH;
    if (x >= huX && x <= huX + huW && y >= huY && y <= huY + huH) return LogConfirmTap::HUNT;
    if (x >= igX && x <= igX + igW && y >= igY && y <= igY + igH) return LogConfirmTap::IGNORE;
    if (x >= infX && x <= infX + infW && y >= infY && y <= infY + infH) return LogConfirmTap::INFO;
    if (x >= cnX && x <= cnX + cnW && y >= cnY && y <= cnY + cnH) return LogConfirmTap::CANCEL;
    return LogConfirmTap::NONE;
}

static void drawConfirmPanel(TFT_eSPI& t, int w, int h, const char* label) {
    int px, py, pw, ph, wX, wY, wW, wH, huX, huY, huW, huH, infX, infY, infW, infH,
        igX, igY, igW, igH, cnX, cnY, cnW, cnH;
    confirmRects(w, h, px, py, pw, ph, wX, wY, wW, wH, huX, huY, huW, huH,
                 infX, infY, infW, infH, igX, igY, igW, igH, cnX, cnY, cnW, cnH);
    t.fillRoundRect(px, py, pw, ph, 6, Theme::BG);
    t.drawRoundRect(px, py, pw, ph, 6, Theme::PURPLE);

    t.setTextWrap(false);
    t.setTextSize(1);
    t.setTextColor(Theme::CYAN, Theme::BG);
    const char* q = "TRACK THIS TARGET?";
    int qw = t.textWidth(q);
    t.setCursor(px + (pw - qw) / 2, py + 8);
    t.print(q);

    // The Bangers glyph table is uppercase-only (it was built for
    // hardcoded shout-caps strings like "RING"/"FLOCK") -- lowercase
    // letters have no glyph and silently vanish (bangersFind() returns
    // null, drawBangersPass() skips it), which is why a real device
    // label like "Apple" rendered as just "A". Upper-case a local copy
    // before measuring/drawing rather than touching the caller's label.
    char upperLabel[32];
    uint8_t li = 0;
    for (; label[li] && li < sizeof(upperLabel) - 1; li++) upperLabel[li] = toupper((unsigned char)label[li]);
    upperLabel[li] = 0;

    int lw = Theme::bangersTextWidth(upperLabel, Theme::BangersSize::MD);
    int maxLw = pw - 16;
    if (lw > maxLw) lw = maxLw; // clipped, not shrunk -- real labels fit comfortably as-is
    Theme::drawBangersText(t, px + (pw - lw) / 2, py + 26, upperLabel, Theme::RED, Theme::BangersSize::MD);

    Theme::drawButton(t, wX, wY, wW, wH, "WATCH", false);
    Theme::drawButton(t, huX, huY, huW, huH, "HUNT", false);
    Theme::drawButton(t, igX, igY, igW, igH, "IGNORE", false);
    Theme::drawButton(t, infX, infY, infW, infH, "MORE INFO", false);
    Theme::drawButton(t, cnX, cnY, cnW, cnH, "CANCEL", false);
}

// Row geometry, shared by uiLogTick() (drawing) and uiLogRowAt() (hit
// testing) so the two can never drift apart -- same reasoning as
// ui_rawscan.cpp's rowLayout(): rowH depends on live font metrics, not
// a compile-time constant.
static void rowLayout(TFT_eSPI& t, int bodyTop, int& detailY, int& rowH) {
    t.setTextSize(2);
    int nameH = t.fontHeight();
    t.setTextSize(1);
    int detailH = t.fontHeight();
    const int topPad = 1;
    detailY = topPad + nameH;
    rowH = topPad + nameH + detailH + 2;
    (void)bodyTop;
}

int uiLogRowAt(TFT_eSPI& t, int x, int y, int screenW, int screenH) {
    Theme::ButtonBarGeom bar = Theme::computeButtonBar(screenW, screenH);
    const int bodyTop = 16, bodyBottom = bar.y - 4;
    if (y < bodyTop || y >= bodyBottom) return -1;
    int detailY, rowH;
    rowLayout(t, bodyTop, detailY, rowH);
    return g_scroll + (y - bodyTop) / rowH;
}

void uiLogTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng, int scrollOffset,
               bool confirmPending, const char* confirmLabel,
               bool infoPending, const char* infoTypeName, const char* infoText) {
    int w = t.width();
    int h = t.height();

    Theme::ButtonBarGeom bar = Theme::computeButtonBar(w, h);
    const int bodyTop    = 16;
    const int bodyBottom = bar.y - 4;
    const int bodyH      = bodyBottom - bodyTop;

    // Title bar
    char title[32];
    snprintf(title, sizeof(title), ">> LOG  (%u) <<", (unsigned)eng.logCount());
    Theme::drawTitleBar(t, title);

    // Body -- whatever background style CLEAR is showing, drawn at
    // reduced strength behind the list, same treatment Settings' body
    // got (see Theme::dimPaletteForOverlay()'s comment). Rows never had
    // a full-row fillRect of their own to begin with (just a
    // drawFastHLine separator plus per-field two-arg setTextColor()
    // calls, which already erase their own glyph cells against
    // Theme::BG), so this drops in with no further changes needed.
    Theme::Palette saved = Theme::dimPaletteForOverlay(179);
    switch (Settings::background()) {
        case Settings::Background::STARFIELD: Theme::drawStarfield(t, now, bodyTop, bodyBottom); break;
        case Settings::Background::TOASTERS:   Theme::drawFlyingToasters(t, now, bodyTop, bodyBottom); break;
        case Settings::Background::AQUARIUM:   Theme::drawAquarium(t, now, bodyTop, bodyBottom); break;
        case Settings::Background::TERMINAL:   Theme::drawTerminalLog(t, now, bodyTop, bodyBottom); break;
        case Settings::Background::FIREFLIES:  Theme::drawFireflies(t, now, bodyTop, bodyBottom); break;
        case Settings::Background::FIRE:       Theme::drawFire(t, now, bodyTop, bodyBottom); break;
        case Settings::Background::SNOWFALL:   Theme::drawSnowfall(t, now, bodyTop, bodyBottom); break;
        case Settings::Background::SPECTRUM:   Theme::drawSpectrumWaterfall(t, now, bodyTop, bodyBottom, eng); break;
        case Settings::Background::TUNNEL:     Theme::drawWireframeTunnel(t, now, bodyTop, bodyBottom); break;
        case Settings::Background::SYNTHWAVE: Theme::drawSynthwave(t, now, bodyTop, bodyBottom); break;
        default:                               Theme::drawDigitalRain(t, now, bodyTop, bodyBottom, true); break;
    }
    Theme::restorePalette(saved);

    uint8_t count = eng.logCount();

    if (count == 0) {
        // Make the empty state impossible to mistake for a broken screen.
        float pulse = 0.55f + 0.45f * sinf((float)(now % 2000) / 2000.0f * 6.2831853f);
        uint16_t col = Theme::blend(Theme::GREEN, Theme::CYAN, (uint16_t)(pulse * 200.0f));
        t.setTextSize(3);
        t.setTextColor(col, Theme::BG);
        const char* msg = "LOG EMPTY";
        int mw = t.textWidth(msg);
        t.setCursor((w - mw) / 2, bodyTop + bodyH / 3);
        t.print(msg);

        t.setTextSize(1);
        t.setTextColor(Theme::CYAN, Theme::BG);
        const char* sub = "no detections yet";
        int sw = t.textWidth(sub);
        t.setCursor((w - sw) / 2, bodyTop + bodyH / 3 + 35);
        t.print(sub);

        Theme::drawButtonBar(t, ButtonId::LOG);
        if (infoPending)        Theme::drawInfoPanel(t, w, h, now, infoTypeName, infoText);
        else if (confirmPending) drawConfirmPanel(t, w, h, confirmLabel);
        return;
    }

    // Row height derived from the actual rendered text heights, not a
    // guessed constant -- the size-2 type label is 16px tall, but the
    // MAC/RSSI/hits line below it used to start at a fixed y+14,
    // running the two lines into each other (confirmed on real
    // hardware: the label's bottom smeared into the MAC line above
    // it).
    // fontHeight() with NO argument -- fontHeight(int) takes a font
    // INDEX (2 means "built-in font #2", not "current font at size
    // 2"), which was silently querying an unrelated font's metrics
    // instead of the GLCD font actually being drawn here.
    t.setTextSize(2);
    int nameH = t.fontHeight();
    t.setTextSize(1);
    int detailH = t.fontHeight();
    const int topPad = 1;
    int detailY = topPad + nameH;
    int rowH = topPad + nameH + detailH + 2;

    int y = bodyTop;
    int idx = g_scroll;
    int max = (bodyH / rowH);

    for (int i = 0; i < max && idx < count; i++, idx++) {
        const Detection* d = eng.logAt(idx);
        if (!d) break;
        // Row separator
        t.drawFastHLine(0, y + rowH - 1, w, Theme::PURPLE);

        // Type label (colored)
        t.setTextSize(2);
        t.setTextColor(Theme::colorFor(d->type), Theme::BG);
        t.setCursor(4, y + topPad);
        t.print(detectionTypeName(d->type));

        // MAC + RSSI line
        t.setTextSize(1);
        t.setTextColor(Theme::WHITE, Theme::BG);
        char mac[24];
        snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
                 d->mac[0], d->mac[1], d->mac[2],
                 d->mac[3], d->mac[4], d->mac[5]);
        t.setCursor(4, y + detailY);
        t.print(mac);

        // RSSI — MAC above runs "XX:XX:XX:XX:XX:XX" (17 chars, 102px
        // at this font size) starting from x=4, so this column can't
        // start before ~110 without drawing on top of it.
        t.setTextColor(Theme::CYAN, Theme::BG);
        t.setCursor(112, y + detailY);
        t.printf("%ddBm", d->rssi);

        // Hits
        t.setTextColor(Theme::VAPOR_PURPLE, Theme::BG);
        t.setCursor(164, y + detailY);
        t.printf("x%u", d->hits);

        // Timestamp (right edge)
        uint32_t ms = d->firstSeen;
        uint32_t sec = ms / 1000;
        char ts[12];
        snprintf(ts, sizeof(ts), "%02lu:%02lu", (unsigned long)(sec / 60), (unsigned long)(sec % 60));
        int tw = t.textWidth(ts);
        t.setTextColor(Theme::VAPOR_PINK, Theme::BG);
        t.setCursor(w - tw - 14, y + topPad);
        t.print(ts);

        y += rowH;
    }

    // Scroll position indicator -- reserved 10px on the right edge
    // (the timestamp column above already stops short of the true
    // edge to make room for it) so there's finally a visual hint this
    // list can hold more than what's on screen.
    Theme::drawScrollbar(t, w - 4, bodyTop, bodyH, count, max, g_scroll);

    // Bottom soft buttons
    Theme::drawButtonBar(t, ButtonId::LOG);

    if (infoPending)        Theme::drawInfoPanel(t, w, h, now, infoTypeName, infoText);
    else if (confirmPending) drawConfirmPanel(t, w, h, confirmLabel);
}
