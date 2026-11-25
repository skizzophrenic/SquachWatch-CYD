// SquachWatch-CYD — ignored-devices screen implementation
#include "ui_ignorelist.h"
#include "ui_scroll.h"
#include "theme.h"
#include "privacy.h"
#include "ignore_list.h"
#include <Arduino.h>
#include <stdio.h>

static const int TOP_MARGIN = 16;
static const int REMOVE_W   = 74;     // width of the tappable REMOVE zone
static int g_scroll = 0;

// Same fixed-height-at-size-2 approach Settings and the detection filter
// use -- shared by drawing and hit-testing so the two cannot drift apart.
static void computeGeom(TFT_eSPI& t, int screenH, int& top, int& bodyBottom, int& rowH) {
    top = TOP_MARGIN + Theme::listHeadingH();
    bodyBottom = screenH - Theme::pinnedBackH(t.width()) - 2;
    // Two lines per row now -- type above, MAC below -- so this is a fixed
    // height rather than one derived from a single line of text.
    // 48 on the large panels: a size-3 type over a body-font MAC.
    (void)t;
    rowH = Theme::large() ? 48 : 32;
}

void uiIgnoreListInit(TFT_eSPI& t) {
    g_scroll = 0;
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
}

void uiIgnoreListScroll(int delta) {
    g_scroll += delta;
    if (g_scroll < 0) g_scroll = 0;
    // The bottom stop is applied where the geometry is, in the draw and the
    // hit test below. It used to be here and stopped at n-1 -- the last
    // entry alone at the top of an otherwise empty screen.
}

static void drawRow(TFT_eSPI& t, int w, int y, int hgt, uint8_t idx) {
    const uint8_t* mac = IgnoreList::macAt(idx);
    if (!mac) return;

    // The same two-line shape a LOG row uses: the type in its own colour on
    // top, the MAC underneath. A column of bare MACs told you that you had
    // muted something without telling you what, and the log entry that would
    // have answered that has usually scrolled out of the ring by then.
    const DetectionType ty = IgnoreList::typeAt(idx);
    Theme::menuFontOn(t, false);   // FONTS > MENUS
    t.setTextColor(Theme::colorFor(ty), Theme::BG);
    t.setCursor(6, y + 3);
    t.print(detectionTypeName(ty));

    char buf[20];
    Privacy::mac(buf, sizeof buf, mac);
    const int macY = y + 5 + t.fontHeight();   // under the type, whatever its size
    Theme::bodyFontOn(t);
    t.setTextColor(Theme::WHITE, Theme::BG);
    t.setCursor(6, macY);
    t.print(buf);
    Theme::bodyFontOff(t);

    // REMOVE, drawn as a real button so it reads as the one thing on the
    // row you can press. Its rect matches uiIgnoreListHitRemove() exactly.
    const int bw = REMOVE_W - 12, bh = 20;
    Theme::drawButton(t, w - REMOVE_W + 4, y + (hgt - bh) / 2, bw, bh, "REMOVE", false);

    t.drawFastHLine(4, y + hgt - 1, w - 8, Theme::PURPLE);
}

void uiIgnoreListTick(TFT_eSPI& t, uint32_t now) {
    (void)now;
    const int w = t.width(), h = t.height();

    int top, bodyBottom, rowH;
    computeGeom(t, h, top, bodyBottom, rowH);

    t.fillRect(0, 0, w, h, Theme::BG);
    Theme::drawTitleBar(t, ">> IGNORED <<");
    Theme::drawListHeading(t, "IGNORED", Theme::AMBER);
    Theme::drawPinnedBack(t, "[ BACK ]");

    const uint8_t n = IgnoreList::count();
    if (n == 0) {
        // An empty list is the normal state for most people, so it gets a
        // real explanation rather than a blank screen that reads as broken.
        t.setTextSize(Theme::uiMenuTextSize(t));
        t.setTextColor(Theme::WHITE, Theme::BG);
        const char* m1 = "NOTHING MUTED";
        t.setCursor((w - t.textWidth(m1)) / 2, h / 2 - 26);
        t.print(m1);
        Theme::bodyFontOn(t);
        t.setTextColor(Theme::CYAN, Theme::BG);
#if defined(SQW_SMALL)
        const char* m2 = "Pick IGNORE on an alert to mute";
#else
        const char* m2 = "Tap IGNORE on an alert to mute";
#endif
        const char* m3 = "a device you own.";
        t.setCursor((w - t.textWidth(m2)) / 2, h / 2 + 2);
        t.print(m2);
        t.setCursor((w - t.textWidth(m3)) / 2, h / 2 + 6 + t.fontHeight());
        t.print(m3);
        Theme::bodyFontOff(t);
        return;
    }

    uiClampScroll(g_scroll, (int)n, bodyBottom - top, rowH);
    int y = top;
    for (uint8_t i = (uint8_t)g_scroll; i < n && y + rowH <= bodyBottom; i++) {
        drawRow(t, w, y, rowH, i);
        y += rowH;
    }

    // Count, bottom right, so you can tell at a glance whether the list is
    // longer than the screen without having to scroll to find out.
    char cnt[24];
    snprintf(cnt, sizeof(cnt), "%u / %u", (unsigned)n, (unsigned)IgnoreList::MAX);
    Theme::bodyFontOn(t);
    t.setTextColor(Theme::VAPOR_PURPLE, Theme::BG);
    t.setCursor(w - t.textWidth(cnt) - 8, Theme::listHeadingY(t));
    t.print(cnt);
    Theme::bodyFontOff(t);

    if ((int)n > (bodyBottom - top) / rowH) {
        Theme::drawScrollbar(t, w - 3, top, bodyBottom - top,
                             n, (bodyBottom - top) / rowH, g_scroll);
    }
}

uint8_t uiIgnoreListHitRemove(TFT_eSPI& t, int x, int y, int screenW, int screenH) {
    int top, bodyBottom, rowH;
    computeGeom(t, screenH, top, bodyBottom, rowH);
    if (x < screenW - REMOVE_W) return 0xFF;      // not in the REMOVE column

    const uint8_t n = IgnoreList::count();
    uiClampScroll(g_scroll, (int)n, bodyBottom - top, rowH);
    int ry = top;
    for (uint8_t i = (uint8_t)g_scroll; i < n && ry + rowH <= bodyBottom; i++) {
        if (y >= ry && y < ry + rowH) return i;
        ry += rowH;
    }
    return 0xFF;
}
