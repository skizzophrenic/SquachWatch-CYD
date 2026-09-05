// SquachWatch-CYD — ignored-devices screen implementation
#include "ui_ignorelist.h"
#include "theme.h"
#include "ignore_list.h"
#include <Arduino.h>
#include <stdio.h>

static const int TOP_MARGIN = 16;
static const int REMOVE_W   = 74;     // width of the tappable REMOVE zone
static int g_scroll = 0;

// Same fixed-height-at-size-2 approach Settings and the detection filter
// use -- shared by drawing and hit-testing so the two cannot drift apart.
static void computeGeom(TFT_eSPI& t, int screenH, int& top, int& bodyBottom, int& rowH) {
    top = TOP_MARGIN;
    bodyBottom = screenH - 4;
    // Two lines per row now -- type above, MAC below -- so this is a fixed
    // height rather than one derived from a single line of text.
    (void)t;
    rowH = 32;
}

void uiIgnoreListInit(TFT_eSPI& t) {
    g_scroll = 0;
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
}

void uiIgnoreListScroll(int delta) {
    g_scroll += delta;
    if (g_scroll < 0) g_scroll = 0;
    const int n = (int)IgnoreList::count();
    if (g_scroll > n - 1) g_scroll = (n > 0) ? n - 1 : 0;
}

static void drawRow(TFT_eSPI& t, int w, int y, int hgt, uint8_t idx) {
    const uint8_t* mac = IgnoreList::macAt(idx);
    if (!mac) return;

    // The same two-line shape a LOG row uses: the type in its own colour on
    // top, the MAC underneath. A column of bare MACs told you that you had
    // muted something without telling you what, and the log entry that would
    // have answered that has usually scrolled out of the ring by then.
    const DetectionType ty = IgnoreList::typeAt(idx);
    t.setTextSize(2);
    t.setTextColor(Theme::colorFor(ty), Theme::BG);
    t.setCursor(6, y + 3);
    t.print(detectionTypeName(ty));

    char buf[20];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    t.setTextSize(1);
    t.setTextColor(Theme::WHITE, Theme::BG);
    t.setCursor(6, y + 21);
    t.print(buf);

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

    const uint8_t n = IgnoreList::count();
    if (n == 0) {
        // An empty list is the normal state for most people, so it gets a
        // real explanation rather than a blank screen that reads as broken.
        t.setTextSize(2);
        t.setTextColor(Theme::WHITE, Theme::BG);
        const char* m1 = "NOTHING MUTED";
        t.setCursor((w - t.textWidth(m1)) / 2, h / 2 - 26);
        t.print(m1);
        t.setTextSize(1);
        t.setTextColor(Theme::CYAN, Theme::BG);
        const char* m2 = "Tap IGNORE on an alert to mute";
        const char* m3 = "a device you own.";
        t.setCursor((w - t.textWidth(m2)) / 2, h / 2 + 2);
        t.print(m2);
        t.setCursor((w - t.textWidth(m3)) / 2, h / 2 + 14);
        t.print(m3);
        return;
    }

    int y = top;
    for (uint8_t i = (uint8_t)g_scroll; i < n && y + rowH <= bodyBottom; i++) {
        drawRow(t, w, y, rowH, i);
        y += rowH;
    }

    // Count, bottom right, so you can tell at a glance whether the list is
    // longer than the screen without having to scroll to find out.
    char cnt[24];
    snprintf(cnt, sizeof(cnt), "%u / %u", (unsigned)n, (unsigned)IgnoreList::MAX);
    t.setTextSize(1);
    t.setTextColor(Theme::VAPOR_PURPLE, Theme::BG);
    t.setCursor(w - t.textWidth(cnt) - 6, h - 12);
    t.print(cnt);

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
    int ry = top;
    for (uint8_t i = (uint8_t)g_scroll; i < n && ry + rowH <= bodyBottom; i++) {
        if (y >= ry && y < ry + rowH) return i;
        ry += rowH;
    }
    return 0xFF;
}
