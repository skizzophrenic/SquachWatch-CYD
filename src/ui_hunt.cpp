// SquachWatch-CYD — HUNT MODE screen implementation
#include "ui_hunt.h"
#include "theme.h"
#include "squachy.h"
#include <Arduino.h>

// -100..-30 dBm covers "barely there" to "right next to it" for both
// BLE and WiFi -- same clamp range ui_watchalert.cpp's sparkline uses,
// so a reading means the same thing on both screens.
static const int RSSI_LO = -100, RSSI_HI = -30;

static void backButtonRect(int screenW, int screenH, int& x, int& y, int& w, int& h) {
    Theme::ButtonBarGeom g = Theme::computeButtonBar(screenW, screenH);
    w = 120;
    h = g.h;
    x = (screenW - w) / 2;
    y = g.y;
}

// Tracks quip-worthy transitions across ticks -- see the trend block
// in uiHuntTick(). Reset on every fresh entry to the screen so a new
// hunt always gets its own STARTED line and a clean slate for
// warmer/colder/hot instead of carrying over whatever the last target
// left behind.
enum class TrendState : uint8_t { NONE, WARMER, COLDER, STEADY };
static TrendState s_lastTrend    = TrendState::NONE;
static bool       s_hotFired     = false;
static bool       s_everHot      = false;  // gates STALLED -- no point razzing someone who already found it
static bool       s_gotFirstSignal = false;
static uint32_t   s_enterMs        = 0;
static bool       s_stalledFired   = false;
static const uint32_t STALLED_MS = 60000;

void uiHuntInit(TFT_eSPI& t) {
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
    s_lastTrend       = TrendState::NONE;
    s_hotFired        = false;
    s_everHot         = false;
    s_gotFirstSignal  = false;
    s_enterMs         = millis();
    s_stalledFired    = false;
    Squachy::huntReaction(Squachy::HuntMoment::STARTED);
}

bool uiHuntHitBack(int x, int y, int screenW, int screenH) {
    int bx, by, bw, bh;
    backButtonRect(screenW, screenH, bx, by, bw, bh);
    return x >= bx && x <= bx + bw && y >= by && y <= by + bh;
}

// Semicircle strength gauge -- sweeps left (weak) to right (strong)
// over the top, speedometer-style. Deliberately not a compass: theta
// is driven purely by current signal strength, not a heading, so the
// "direction" only comes from the user physically turning/moving
// while watching which way the needle swings.
static void drawGauge(TFT_eSPI& t, int cx, int cy, int r, float frac, uint16_t needleColor) {
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;

    // Track: a 9-point arc outline plus a short radial tick at each
    // point, dim purple to read as scale rather than signal.
    const uint8_t TICKS = 8;
    int px = cx - r, py = cy;
    for (uint8_t i = 1; i <= TICKS; i++) {
        float theta = (float)i * (PI / TICKS);
        int x = cx + (int)(r * cosf(theta) * -1.0f);
        int y = cy - (int)(r * sinf(theta));
        t.drawLine(px, py, x, y, Theme::PURPLE);
        int tx = cx + (int)((r - 6) * cosf(theta) * -1.0f);
        int ty = cy - (int)((r - 6) * sinf(theta));
        t.drawLine(x, y, tx, ty, Theme::PURPLE);
        px = x;
        py = y;
    }

    // Needle: theta=0 at frac=0 (left/weak), theta=PI at frac=1
    // (right/strong) -- same theta(0..PI) -> left..right sweep as the
    // track loop above.
    float needleTheta = PI * frac;
    int nx = cx + (int)((r - 10) * cosf(needleTheta) * -1.0f);
    int ny = cy - (int)((r - 10) * sinf(needleTheta));
    t.drawLine(cx, cy, nx, ny, needleColor);
    t.drawLine(cx + 1, cy, nx + 1, ny, needleColor);
    t.fillCircle(cx, cy, 4, needleColor);
}

void uiHuntTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng) {
    int w = t.width(), h = t.height();

    Theme::drawTitleBar(t, ">> HUNT MODE <<");

    Theme::ButtonBarGeom bar = Theme::computeButtonBar(w, h);
    int bodyTop = 16, bodyBottom = bar.y - 4;
    t.fillRect(0, bodyTop, w, bodyBottom - bodyTop, Theme::BG);

    // Mini Squachy cameo, same reduced-header-strip reuse ui_rawscan.cpp
    // and ui_watchalert.cpp already use -- his normal idle tick, just
    // given a small box instead of the whole screen. scanningFx ties
    // his little "ping" animation to the hunting theme.
    const int sqH = 44;
    Squachy::tick(t, w / 2, bodyTop, sqH, now, true, 0.6f, true);

    t.setTextSize(1);
    t.setTextWrap(false);
    t.setTextColor(Theme::CYAN, Theme::BG);
    int labelY = bodyTop + sqH + 2;
    const char* label = eng.huntLabel();
    int lw = t.textWidth(label);
    int maxLw = w - 16;
    t.setCursor((w - (lw < maxLw ? lw : maxLw)) / 2, labelY);
    t.print(label);

    uint8_t rssiN = eng.huntRssiCount();

    // First-ever sample for this target -- fires once, well before
    // there's enough history for a warmer/colder trend (needs 2+).
    if (rssiN > 0 && !s_gotFirstSignal) {
        s_gotFirstSignal = true;
        Squachy::huntReaction(Squachy::HuntMoment::FIRST_SIGNAL);
    }

    // A hunt that's dragged on a while without ever reaching HOT gets
    // one (only one) impatient nudge -- pure flavor, checked once a
    // tick is cheap enough not to bother gating further.
    if (!s_everHot && !s_stalledFired && (now - s_enterMs) >= STALLED_MS) {
        s_stalledFired = true;
        Squachy::huntReaction(Squachy::HuntMoment::STALLED);
    }

    int textBlockH = 36;
    int gaugeTop = labelY + 12;
    int gaugeBottom = bodyBottom - textBlockH;
    int r = gaugeBottom - gaugeTop;
    int maxRw = (w - 40) / 2;
    if (r > maxRw) r = maxRw;
    if (r < 30) r = 30;
    int cx = w / 2, cy = gaugeBottom;

    float frac = 0.0f;
    int8_t latestRssi = RSSI_LO;
    if (rssiN > 0) {
        latestRssi = eng.huntRssiAt(rssiN - 1);
        int v = latestRssi;
        if (v < RSSI_LO) v = RSSI_LO;
        if (v > RSSI_HI) v = RSSI_HI;
        frac = (float)(v - RSSI_LO) / (float)(RSSI_HI - RSSI_LO);
    }
    uint16_t needleColor = (frac < 0.5f)
        ? Theme::blend(Theme::RED, Theme::AMBER, (uint16_t)(frac * 2.0f * 256))
        : Theme::blend(Theme::AMBER, Theme::GREEN, (uint16_t)((frac - 0.5f) * 2.0f * 256));
    drawGauge(t, cx, cy, r, frac, needleColor);

    // Fires once per visit to "basically on top of it" territory, not
    // every tick it stays there -- re-arms if the signal drops back out
    // so a genuine re-approach (walked away, came back) can fire again.
    if (rssiN > 0 && frac >= 0.92f) {
        if (!s_hotFired) { Squachy::huntReaction(Squachy::HuntMoment::HOT); s_hotFired = true; }
        s_everHot = true;
    } else {
        s_hotFired = false;
    }

    // Numeric readout + warmer/colder trend, compared against a sample
    // from ~6 ticks (roughly 12s) back so a single noisy reading can't
    // flip it -- a hard deadband on top of that for the same reason.
    char rbuf[16];
    if (rssiN > 0) snprintf(rbuf, sizeof(rbuf), "%d dBm", (int)latestRssi);
    else           snprintf(rbuf, sizeof(rbuf), "-- dBm");
    t.setTextSize(2);
    int rw = t.textWidth(rbuf);
    t.setTextColor(Theme::WHITE, Theme::BG);
    t.setCursor((w - rw) / 2, cy + 8);
    t.print(rbuf);
    // fontHeight() no-arg reads back the size-2 metrics just set above
    // -- fontHeight(int) takes a FONT INDEX, not a size multiplier, and
    // would silently query the wrong thing here.
    int readoutH = t.fontHeight();

    const char* trend = "WAITING FOR SIGNAL...";
    uint16_t trendColor = Theme::WHITE;
    if (rssiN >= 2) {
        uint8_t backIdx = (rssiN > 6) ? (rssiN - 6) : 0;
        int delta = (int)latestRssi - (int)eng.huntRssiAt(backIdx);
        TrendState cur;
        if (delta > 3)       { trend = "GETTING WARMER";  trendColor = Theme::GREEN; cur = TrendState::WARMER; }
        else if (delta < -3) { trend = "GETTING COLDER";  trendColor = Theme::RED;   cur = TrendState::COLDER; }
        else                 { trend = "HOLDING STEADY";  trendColor = Theme::CYAN;  cur = TrendState::STEADY; }
        // Only on an actual change -- not every tick the trend still
        // reads the same way, or he'd never shut up.
        if (cur != s_lastTrend) {
            if (cur == TrendState::WARMER) Squachy::huntReaction(Squachy::HuntMoment::WARMER);
            else if (cur == TrendState::COLDER) Squachy::huntReaction(Squachy::HuntMoment::COLDER);
            s_lastTrend = cur;
        }
    }
    t.setTextSize(1);
    int tw = t.textWidth(trend);
    t.setTextColor(trendColor, Theme::BG);
    t.setCursor((w - tw) / 2, cy + 8 + readoutH + 2);
    t.print(trend);

    int bx, by, bw, bh;
    backButtonRect(w, h, bx, by, bw, bh);
    Theme::drawButton(t, bx, by, bw, bh, "[ BACK ]", false);
}
