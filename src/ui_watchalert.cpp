// SquachWatch-CYD — watched-target alert screen implementation
#include "ui_watchalert.h"
#include "theme.h"
#include "squachy.h"
#include <Arduino.h>
#include <stdio.h>
#include <string.h>

// Shared by the drawing and the hit test so the two cannot drift -- the rule
// every other panel here follows. Full width minus a margin: it is the only
// control on the screen, so there is nothing for it to crowd.
static const int REMOVE_H = 26;
static void removeRect(TFT_eSPI& t, int& x, int& y, int& w, int& h) {
    const int margin = 10;
    w = t.width() - 2 * margin;
    if (w > 240) w = 240;
    h = REMOVE_H;
    x = (t.width() - w) / 2;
    y = t.height() - h - margin;
}

// The full label needs ~22 characters and the narrowest portrait rotation
// cannot hold it. Shortened rather than shrunk: size-1 is already the
// smallest the built-in font offers.
static const char* removeLabel(TFT_eSPI& t, int w) {
    const char* full = "REMOVE FROM WATCH LIST";
    // At the size this button will actually be drawn. On a wide panel that is
    // size 2, where the full label wants 270 px in a 240 px box -- so it takes
    // the short word and keeps the bigger letters, rather than keeping all
    // twenty-two characters and being the one small button on the screen.
    t.setTextSize(Theme::uiTextSize(t, 1));
    const bool fits = t.textWidth(full) <= w - 8;
    t.setTextSize(1);
    return fits ? full : "UNWATCH";
}

// ---- LOCKED ON (2026-09-27) ---------------------------------------------------
// Picked from five design studies on a review page (SCOPE, SONAR, HUD,
// BULLSEYE, OPERATOR) after a first round of three (RED ALERT, WANTED, RADAR):
// a radar scope that has found the thing you told it to look for, with
// Squachy beside it on the headphones. The radar's distance is the signal,
// not a direction: the blip's angle is fixed per target, its radius is how
// strong it is, and its last few positions draw the approach.
// What Squachy says here, and when the line changed. He used to get one quip
// ("Back again, huh?") for three seconds from his own mood machine, which was
// gone before anyone had read it, said nothing about what had happened, and
// then left him free to wander off and chat about patrols. Here he stands
// still at the scope and explains, a line every few seconds, round and round
// until the alert is tapped.
static uint8_t  s_lineStep = 0;
static uint32_t s_lineAt   = 0;
static bool     s_lineFirst = true;
static char     s_line[96];
static const uint32_t LINE_MS = 7000;

namespace {

struct WatchView {
    const char*   label;
    DetectionType type;       // UNKNOWN when the log has no row for it
    bool          haveRssi;
    int8_t        rssi;
    float         f;          // signal as 0..1, -100 dBm to -30 dBm
    int8_t        trend;      // +1 closer, -1 further, 0 holding
};

WatchView gather(const DetectionEngine& eng) {
    WatchView v{};
    v.label = eng.watchLabel();
    v.type  = DetectionType::UNKNOWN;
    for (uint8_t i = 0; i < eng.logCount(); i++) {
        const Detection* d = eng.logAt(i);
        if (d && (eng.isWatched(d->mac, true) || eng.isWatched(d->mac, false))) {
            v.type = d->type;
            if (!v.label || !v.label[0]) v.label = d->name;
            break;
        }
    }
    if (!v.label || !v.label[0]) v.label = "UNKNOWN DEVICE";
    const uint8_t n = eng.watchRssiCount();
    if (n > 0) {
        v.haveRssi = true;
        v.rssi = eng.watchRssiAt(n - 1);
        int r = v.rssi; if (r < -100) r = -100; if (r > -30) r = -30;
        v.f = (float)(r + 100) / 70.0f;
        if (n >= 4) {
            const int before = (eng.watchRssiAt(n - 2) + eng.watchRssiAt(n - 3) + eng.watchRssiAt(n - 4)) / 3;
            if (v.rssi - before >= 3) v.trend = 1;
            else if (before - v.rssi >= 3) v.trend = -1;
        }
    }
    return v;
}

// The label, cut to fit w at the given size with a trailing "..".
void fitPrint(TFT_eSPI& t, int x, int y, const char* s, int w, uint8_t size, uint16_t col, bool centre) {
    char buf[28];
    snprintf(buf, sizeof buf, "%s", s);
    t.setTextSize(size);
    while (strlen(buf) > 3 && t.textWidth(buf) > w) {
        const size_t n = strlen(buf);
        buf[n - 3] = '.'; buf[n - 2] = '.'; buf[n - 1] = 0;
    }
    t.setTextColor(col);
    const int tw = t.textWidth(buf);
    t.setCursor(centre ? x + (w - tw) / 2 : x, y);
    t.print(buf);
}

// Ten blocks, cold on the left and hot on the right: how close, at a glance.
void trendLine(TFT_eSPI& t, int x, int y, const WatchView& v, uint16_t col, bool centreIn, int w) {
    char buf[32];
    const char* word = v.trend > 0 ? "CLOSER" : v.trend < 0 ? "FURTHER" : "HOLDING";
    snprintf(buf, sizeof buf, "%d dBm  %s", (int)v.rssi, word);
    t.setTextSize(1);
    t.setTextColor(col);
    const int tw = t.textWidth(buf) + 10;
    const int x0 = centreIn ? x + (w - tw) / 2 : x;
    t.setCursor(x0 + 10, y);
    t.print(buf);
    const uint16_t ac = v.trend > 0 ? t.color565(255, 36, 0) : v.trend < 0 ? t.color565(0, 219, 0) : col;
    if (v.trend > 0)      t.fillTriangle(x0, y + 7, x0 + 6, y + 7, x0 + 3, y, ac);
    else if (v.trend < 0) t.fillTriangle(x0, y, x0 + 6, y, x0 + 3, y + 7, ac);
    else                  t.fillRect(x0, y + 3, 7, 2, ac);
}

void headline(TFT_eSPI& t, int cx, int y, const char* msg, uint16_t col) {
    const int tw = Theme::bangersTextWidth(msg, Theme::BangersSize::MD);
    Theme::drawBangersOutline(t, cx - tw / 2, y, msg, Theme::BLACK, Theme::BangersSize::MD, 2);
    Theme::drawBangersText(t, cx - tw / 2, y, msg, col, Theme::BangersSize::MD);
}

int historyF(const DetectionEngine& eng, float* out, int maxN) {
    const uint8_t n = eng.watchRssiCount();
    const int from = n > maxN ? n - maxN : 0;
    int k = 0;
    for (int i = from; i < n; i++) {
        int r = eng.watchRssiAt((uint8_t)i); if (r < -100) r = -100; if (r > -30) r = -30;
        out[k++] = (float)(r + 100) / 70.0f;
    }
    return k;
}

float labelAngle(const char* label) {
    uint32_t hs = 2166136261u;
    for (const char* p = label; *p; p++) { hs ^= (uint8_t)*p; hs *= 16777619u; }
    return (float)(hs % 628) / 100.0f;
}

// The text column every variant shares: headline, name, type, trend, line.
void scope(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng, const WatchView& v,
           int rcx, int rcy, int R, bool tag) {
    const uint16_t g1 = t.color565(0, 73, 0), g2 = t.color565(0, 146, 0), g3 = t.color565(0, 255, 0);
    const float TAU = 6.2831853f;
    t.fillCircle(rcx, rcy, R, Theme::BLACK);
    const float a = (float)(now % 2400) / 2400.0f * TAU;
    for (int k = 5; k >= 1; k--) {
        const float a0 = a - (float)k * 0.09f, a1 = a - (float)(k - 1) * 0.09f;
        t.fillTriangle(rcx, rcy, rcx + (int)(cosf(a0) * R), rcy + (int)(sinf(a0) * R),
                       rcx + (int)(cosf(a1) * R), rcy + (int)(sinf(a1) * R), k <= 1 ? g2 : g1);
    }
    for (int k = 1; k <= 3; k++) t.drawCircle(rcx, rcy, R * k / 3, g2);
    t.drawCircle(rcx, rcy, R + 1, g3);
    t.drawFastHLine(rcx - R, rcy, 2 * R, g1);
    t.drawFastVLine(rcx, rcy - R, 2 * R, g1);
    t.drawLine(rcx, rcy, rcx + (int)(cosf(a) * R), rcy + (int)(sinf(a) * R), g3);

    const float b = labelAngle(v.label);
    auto radius = [&](float f) { return (float)R * (0.12f + 0.78f * (1.0f - f)); };
    // The approach: where it was on each of the last few readings.
    float hf[8];
    const int hn = historyF(eng, hf, 8);
    for (int i = 0; i + 1 < hn; i++) {
        const float rr = radius(hf[i]);
        t.fillCircle(rcx + (int)(cosf(b) * rr), rcy + (int)(sinf(b) * rr), i >= hn - 3 ? 2 : 1, g2);
    }
    const float rr = radius(v.haveRssi ? v.f : 0.3f);
    const int bx = rcx + (int)(cosf(b) * rr), by = rcy + (int)(sinf(b) * rr);
    float since = a - b; while (since < 0) since += TAU;
    const float fl = since < 1.2f ? 1.0f - since / 1.2f : 0.0f;
    t.fillCircle(bx, by, 3 + (int)(fl * 3.0f), Theme::blend(g2, Theme::WHITE, (uint16_t)(fl * 255.0f)));
    const int s = 10 + (int)(3.0f * sinf((float)(now % 800) / 800.0f * TAU));
    const uint16_t lc = ((now / 250) & 1u) ? t.color565(255, 36, 0) : t.color565(255, 219, 0);
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sy = -1; sy <= 1; sy += 2) {
            const int cx = bx + sx * s, cy = by + sy * s;
            t.drawFastHLine(sx < 0 ? cx : cx - 4, cy, 5, lc);
            t.drawFastVLine(cx, sy < 0 ? cy : cy - 4, 5, lc);
        }
    // A tag on a leader line: what it is, pinned to where it is.
    if (tag && v.type != DetectionType::UNKNOWN) {
        const char* nm = detectionTypeName(v.type);
        t.setTextSize(1);
        const int lw = t.textWidth(nm) + 6;
        const int dir = (cosf(b) > 0) ? -1 : 1;              // point the tag back toward the middle
        const int lx = bx + dir * (s + 10), ly = by - s - 10;
        t.drawLine(bx + dir * s, by - s, lx, ly + 5, lc);
        const int rx = dir > 0 ? lx : lx - lw;
        t.fillRect(rx, ly, lw, 11, Theme::BLACK);
        t.drawRect(rx, ly, lw, 11, g3);
        t.setTextColor(g3);
        t.setCursor(rx + 3, ly + 2);
        t.print(nm);
    }
}

// The scope on the left and Squachy on the right on headphones, running it
// himself, the two sharing one vertical centre. His speech bubble owns the
// top of the screen, so LOCKED ON goes under the scope, with the name and
// the trend beside it.
void drawOperator(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng, int hintY, bool advance) {
    const int w = t.width(), h = t.height();
    const WatchView v = gather(eng);
    const uint16_t g0 = t.color565(0, 36, 0), g1 = t.color565(0, 73, 0), g3 = t.color565(0, 255, 0);
    t.fillRect(0, 0, w, h, g0);
    for (int x = 0; x < w; x += 20) t.drawFastVLine(x, 0, h, g1);
    for (int y = 0; y < h; y += 20) t.drawFastHLine(0, y, w, g1);
    const bool wide = w >= 300;

    const int bandTop = 34, bandBot = hintY - 32;   // LOCKED ON is 24 rows, plus a gap over the hint
    const int bandH = bandBot - bandTop;
    int R = bandH / 2 - 2;
    // Narrow screens: the scope gives up width so he still fits beside it.
    const int maxR = (wide ? w * 30 : w * 28) / 100;
    if (R > maxR) R = maxR;
    const int rcx = 10 + R, rcy = bandTop + bandH / 2;
    scope(t, now, eng, v, rcx, rcy, R, wide);

    // Squachy on the same centre line as the scope, sized to the gap beside
    // it as well as to the scope's height: he is about two thirds as wide as
    // he is tall.
    const int gap = w - (rcx + R);
    const int sx = rcx + R + gap / 2;
    // Drawn as a cameo, standing still: the live Squachy has his own ideas
    // about walking off and what to talk about. 53 units runs from the top
    // of his head to his soles, so his feet go half that below the centre.
    // A little under the scope's height, so the headset band clears his bubble.
    int sh = 2 * R - 6;
    if (sh > gap * 13 / 10) sh = gap * 13 / 10;
    const float sc = (float)sh / 60.0f;
    const int feet = rcy + (int)(26.5f * sc);
    (void)advance;
    Squachy::setHeadset(true);
    Squachy::drawWaving(t, sx, feet, now, sc, s_line, now - s_lineAt < 2500, 0, false);
    Squachy::setHeadset(false);
    headline(t, rcx, bandBot + 2, "LOCKED ON", g3);

    // Beside the headline: what it is and how strong, then which way.
    const int nx = rcx + Theme::bangersTextWidth("LOCKED ON", Theme::BangersSize::MD) / 2 + 10;
    const int nw = w - nx - 6;
    char nb[40];
    if (v.haveRssi) snprintf(nb, sizeof nb, "%s  %d dBm", v.label, (int)v.rssi);
    else            snprintf(nb, sizeof nb, "%s", v.label);
    fitPrint(t, nx, bandBot + 4, nb, nw, 1, t.color565(182, 255, 170), false);
    if (v.haveRssi) {
        const char* word = v.trend > 0 ? "CLOSER" : v.trend < 0 ? "FURTHER" : "HOLDING";
        const uint16_t ac = v.trend > 0 ? t.color565(255, 36, 0) : v.trend < 0 ? g3 : t.color565(182, 255, 170);
        const int ay = bandBot + 16;
        if (v.trend > 0)      t.fillTriangle(nx, ay + 7, nx + 6, ay + 7, nx + 3, ay, ac);
        else if (v.trend < 0) t.fillTriangle(nx, ay, nx + 6, ay, nx + 3, ay + 7, ac);
        else                  t.fillRect(nx, ay + 3, 7, 2, ac);
        t.setTextSize(1);
        t.setTextColor(g3);
        t.setCursor(nx + 10, ay);
        t.print(word);
    }
}

}  // namespace

void uiWatchAlertInit(TFT_eSPI& t) {
    t.fillRect(0, 0, t.width(), t.height(), Theme::BLACK);
    s_lineStep = 0;
    s_lineFirst = true;
}

void uiWatchAlertTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng, bool advance) {
    const int w = t.width(), h = t.height();
    // Two ways out doing different things: anywhere on the screen dismisses
    // the alert and leaves the watch running, the button ends the watch.
    // The hint sits clear of the button by the font height plus a gap.
    const int hintY = h - REMOVE_H - 10 - 8 - 6;
    // The next line, on the pass that advances (see advance).
    if (advance && (s_lineFirst || now - s_lineAt >= LINE_MS)) {
        const WatchView v = gather(eng);
        switch (s_lineStep) {
            case 0:
                snprintf(s_line, sizeof s_line, "Heads up! The %s you're watching is back in range.", v.label);
                break;
            case 1:
                snprintf(s_line, sizeof s_line, "%s",
                         v.trend > 0 ? "It's getting closer. The nearer the middle, the nearer to you."
                       : v.trend < 0 ? "It's moving away. The dot drifts out as it goes."
                                     : "It's holding still. The nearer the middle, the nearer to you.");
                break;
            default:
                snprintf(s_line, sizeof s_line, "Tap anywhere to close this. The button stops watching it.");
                break;
        }
        s_lineStep = (uint8_t)((s_lineStep + 1) % 3);
        s_lineAt = now;
        s_lineFirst = false;
    }
    drawOperator(t, now, eng, hintY, advance);
    const char* tapMsg = "tap anywhere to dismiss";
    t.setTextSize(1);
    t.setTextColor(Theme::WHITE);
    t.setCursor((w - t.textWidth(tapMsg)) / 2, hintY);
    t.print(tapMsg);
    int bx, by, bw, bh;
    removeRect(t, bx, by, bw, bh);
    Theme::drawButton(t, bx, by, bw, bh, removeLabel(t, bw), false);
}

bool uiWatchAlertHitRemove(TFT_eSPI& t, int x, int y) {
    int bx, by, bw, bh;
    removeRect(t, bx, by, bw, bh);
    // The ten-pixel margin under the button is nobody's; the only control on
    // the screen may as well answer for it (Theme::hitTestButtonBar has the
    // measurement for why a press lands there).
    return x >= bx && x <= bx + bw && y >= by && y < t.height();
}
