// SquachWatch-CYD — watched-target alert screen implementation
#include "ui_watchalert.h"
#include "theme.h"
#include "squachy.h"
#include <Arduino.h>

void uiWatchAlertInit(TFT_eSPI& t) {
    t.fillRect(0, 0, t.width(), t.height(), Theme::BLACK);
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

    const char* tapMsg = "tap to dismiss";
    int tmw = t.textWidth(tapMsg);
    t.setCursor((w - tmw) / 2, h - 20);
    t.print(tapMsg);
}
