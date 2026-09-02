// SquachWatch-CYD — watched-target alert screen implementation
#include "ui_watchalert.h"
#include "theme.h"
#include "squachy.h"
#include <Arduino.h>

void uiWatchAlertInit(TFT_eSPI& t) {
    t.fillRect(0, 0, t.width(), t.height(), Theme::BLACK);
    Squachy::watchAlertReaction();
}

void uiWatchAlertTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng, bool advance) {
    int w = t.width();
    int h = t.height();

    // Urgent pulsing wash -- deliberately different from CLEAR's
    // vaporwave background and from the normal ALERT screen's
    // per-type ambient scene, so this reads as its own distinct thing
    // the instant it appears. Kept fairly dark/modest rather than a
    // big dramatic swing -- Squachy's own bubble-erase (see tick()'s
    // header comment) clears its footprint with the app's flat
    // Theme::BG rather than this screen's own color, so a subtler
    // pulse keeps any stray patch from that unlikely to stand out.
    float pulse = 0.5f + 0.5f * sinf((float)(now % 1400) / 1400.0f * 6.2831853f);
    uint16_t bg = Theme::blend(Theme::BLACK, Theme::RED, (uint16_t)(pulse * 90.0f));
    t.fillRect(0, 0, w, h, bg);

    // Squachy runs around behind the text, full-size (not the raw-scan
    // screen's mini cameo) -- same tick() call CLEAR uses, just with
    // the whole screen to himself instead of sharing it with counters
    // and buttons.
    const int topY        = 16;
    const int availHeight = h - topY - 40;
    Squachy::tick(t, w / 2, topY, availHeight, now, advance);

    // Headline, outlined the same way CLEAR's ALL CLEAR/DETECTIONS
    // LOGGED status text is (2px black outline via an offset grid) --
    // stays legible over him regardless of where he's standing.
    static const int8_t OUTLINE_OFS[24][2] = {
        {-2,-2},{-1,-2},{0,-2},{1,-2},{2,-2},
        {-2,-1},{-1,-1},{0,-1},{1,-1},{2,-1},
        {-2, 0},{-1, 0},        {1, 0},{2, 0},
        {-2, 1},{-1, 1},{0, 1},{1, 1},{2, 1},
        {-2, 2},{-1, 2},{0, 2},{1, 2},{2, 2},
    };
    const char* msg = "TARGET IN RANGE";
    uint16_t col = Theme::blend(Theme::RED, Theme::WHITE, (uint16_t)(pulse * 120.0f));
    int ty = h / 2 - 20;
    int tw = Theme::bangersTextWidth(msg, Theme::BangersSize::MD);
    if (tw <= w - 8) {
        int tx = (w - tw) / 2;
        for (uint8_t i = 0; i < 24; i++) {
            Theme::drawBangersText(t, tx + OUTLINE_OFS[i][0], ty + OUTLINE_OFS[i][1],
                                    msg, Theme::BLACK, Theme::BangersSize::MD);
        }
        Theme::drawBangersText(t, tx, ty, msg, col, Theme::BangersSize::MD);
    } else {
        // Narrowest portrait rotations: same fallback CLEAR's status
        // text uses when the Bangers glyph set won't fit.
        t.setTextSize(2);
        int sw = t.textWidth(msg);
        int sx = (w - sw) / 2, sy = ty;
        t.setTextColor(Theme::BLACK, bg);
        for (uint8_t i = 0; i < 24; i++) {
            t.setCursor(sx + OUTLINE_OFS[i][0], sy + OUTLINE_OFS[i][1]);
            t.print(msg);
        }
        t.setTextColor(col, bg);
        t.setCursor(sx, sy);
        t.print(msg);
    }

    // Sub-line: what's actually being watched, and the dismiss hint --
    // plain text, not outlined (same tier as CLEAR's counter row).
    t.setTextSize(1);
    t.setTextWrap(false);
    t.setTextColor(Theme::WHITE, bg);
    int sw2 = t.textWidth(eng.watchLabel());
    t.setCursor((w - sw2) / 2, ty + 30);
    t.print(eng.watchLabel());

    // Signal-strength trend -- lets you tell "getting closer" from
    // "just sitting there" instead of only knowing it's in range at
    // all. Needs at least 2 samples to draw a line; a single fresh
    // watch (or one that's only fired once) just shows the number.
    uint8_t rssiN = eng.watchRssiCount();
    if (rssiN > 0) {
        char rbuf[24];
        snprintf(rbuf, sizeof(rbuf), "%d dBm", (int)eng.watchRssiAt(rssiN - 1));
        int rw = t.textWidth(rbuf);
        int labelY = ty + 44;
        t.setCursor((w - rw) / 2, labelY);
        t.print(rbuf);

        if (rssiN >= 2) {
            // -100..-30 dBm covers "barely there" to "right next to
            // it" for both BLE and WiFi -- clamped rather than
            // auto-scaled so the line's slope means the same thing
            // graph to graph instead of rescaling per-target.
            const int RSSI_LO = -100, RSSI_HI = -30;
            const int graphW = (w - 40 < 140) ? (w - 40) : 140;
            const int graphH = 24;
            const int gx = (w - graphW) / 2;
            const int gy = labelY + 12;

            auto mapY = [&](int8_t rssi) {
                int v = rssi;
                if (v < RSSI_LO) v = RSSI_LO;
                if (v > RSSI_HI) v = RSSI_HI;
                return gy + graphH - ((v - RSSI_LO) * graphH) / (RSSI_HI - RSSI_LO);
            };

            int prevX = gx, prevY = mapY(eng.watchRssiAt(0));
            for (uint8_t i = 1; i < rssiN; i++) {
                int x = gx + (int)((uint32_t)i * graphW / (rssiN - 1));
                int y = mapY(eng.watchRssiAt(i));
                t.drawLine(prevX, prevY, x, y, Theme::WHITE);
                prevX = x;
                prevY = y;
            }
            t.fillCircle(prevX, prevY, 2, col);
        }
    }

    const char* tapMsg = "tap to dismiss";
    int tmw = t.textWidth(tapMsg);
    t.setCursor((w - tmw) / 2, h - 20);
    t.print(tapMsg);
}
