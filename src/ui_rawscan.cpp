// SquachWatch-CYD — manual raw BLE/WiFi scanner screen implementation
#include "ui_rawscan.h"
#include "theme.h"
#include "squachy.h"
#include <Arduino.h>

static int g_scroll = 0;

// Single centered back button in place of the normal three-slot bar --
// reuses computeButtonBar()'s y/h so it lines up with every other
// screen's bar row, just one wide button instead of three.
static void backButtonRect(int screenW, int screenH, int& x, int& y, int& w, int& h) {
    Theme::ButtonBarGeom g = Theme::computeButtonBar(screenW, screenH);
    w = 120;
    h = g.h;
    x = (screenW - w) / 2;
    y = g.y;
}

void uiRawScanInit(TFT_eSPI& t, bool isBle) {
    (void)isBle;
    g_scroll = 0;
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
}

void uiRawScanScroll(int delta) {
    g_scroll += delta;
    if (g_scroll < 0) g_scroll = 0;
}

bool uiRawScanHitBack(int x, int y, int screenW, int screenH) {
    int bx, by, bw, bh;
    backButtonRect(screenW, screenH, bx, by, bw, bh);
    return x >= bx && x <= bx + bw && y >= by && y <= by + bh;
}

void uiRawScanTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng, bool isBle, bool done) {
    int w = t.width();
    int h = t.height();

    Theme::ButtonBarGeom bar = Theme::computeButtonBar(w, h);
    const int titleBottom = 16;
    // Squachy gets a small strip of his own below the title bar --
    // "mini" version of his CLEAR-screen self, same tick() call, just
    // a lot less room to run around in.
    const int sqTop    = titleBottom;
    const int sqH       = 46;
    const int bodyTop    = sqTop + sqH;
    const int bodyBottom = bar.y - 4;
    const int bodyH      = bodyBottom - bodyTop;

    char title[32];
    snprintf(title, sizeof(title), ">> %s SCAN <<", isBle ? "BLE" : "WIFI");
    Theme::drawTitleBar(t, title);

    t.fillRect(0, sqTop, w, sqH, Theme::BG);
    Squachy::tick(t, w / 2, sqTop, sqH, now, true);

    t.fillRect(0, bodyTop, w, bodyH, Theme::BG);

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

        int bx, by, bw, bh;
        backButtonRect(w, h, bx, by, bw, bh);
        Theme::drawButton(t, bx, by, bw, bh, "[ BACK ]", false);
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

        int bx, by, bw, bh;
        backButtonRect(w, h, bx, by, bw, bh);
        Theme::drawButton(t, bx, by, bw, bh, "[ BACK ]", false);
        return;
    }

    int rowH = 22;
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
            t.setCursor(4, y + 3);
            t.print(r->name[0] ? r->name : "(unnamed)");

            t.setTextSize(1);
            t.setTextColor(Theme::WHITE, Theme::BG);
            char mac[24];
            snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
                     r->mac[0], r->mac[1], r->mac[2], r->mac[3], r->mac[4], r->mac[5]);
            t.setCursor(4, y + 14);
            t.print(mac);

            t.setTextColor(Theme::VAPOR_PURPLE, Theme::BG);
            char rssi[12];
            snprintf(rssi, sizeof(rssi), "%ddBm", r->rssi);
            int rw = t.textWidth(rssi);
            t.setCursor(w - rw - 4, y + 3);
            t.print(rssi);
        } else {
            t.setTextSize(2);
            t.setTextColor(Theme::CYAN, Theme::BG);
            t.setCursor(4, y + 3);
            t.print(eng.rawWifiSsid(idx));

            t.setTextSize(1);
            t.setTextColor(Theme::WHITE, Theme::BG);
            char line[24];
            snprintf(line, sizeof(line), "CH%u  %s", (unsigned)eng.rawWifiChannel(idx),
                     eng.rawWifiOpen(idx) ? "OPEN" : "LOCKED");
            t.setCursor(4, y + 14);
            t.print(line);

            t.setTextColor(Theme::VAPOR_PURPLE, Theme::BG);
            char rssi[12];
            snprintf(rssi, sizeof(rssi), "%ddBm", eng.rawWifiRssi(idx));
            int rw = t.textWidth(rssi);
            t.setCursor(w - rw - 4, y + 3);
            t.print(rssi);
        }

        y += rowH;
    }

    int bx, by, bw, bh;
    backButtonRect(w, h, bx, by, bw, bh);
    Theme::drawButton(t, bx, by, bw, bh, "[ BACK ]", false);
}
