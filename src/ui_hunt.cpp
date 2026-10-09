// SquachWatch-CYD — HUNT MODE screen implementation
#include "ui_hunt.h"
#include "theme.h"
#include "privacy.h"
#include "squachy.h"
#include <Arduino.h>

// -100..-30 dBm covers "barely there" to "right next to it" for both
// BLE and WiFi -- same clamp range ui_watchalert.cpp's sparkline uses,
// so a reading means the same thing on both screens.
static const int RSSI_LO = -100, RSSI_HI = -30;

// Two buttons now, not one: BACK leaves the screen with the hunt still
// running (which is the point -- you can go look at something else and come
// back), and STOP ends it. Until STOP existed, clearHunt() had no caller in
// any screen and a hunt could only be replaced or rebooted away.
static void backButtonRect(int screenW, int screenH, int& x, int& y, int& w, int& h) {
    Theme::ButtonBarGeom g = Theme::computeButtonBar(screenW, screenH);
    w = 110;
    h = g.h;
    // The pair sits centred as a unit: BACK left of centre, STOP right.
    x = screenW / 2 - w - 5;
    y = g.y;
}

static void stopButtonRect(int screenW, int screenH, int& x, int& y, int& w, int& h) {
    backButtonRect(screenW, screenH, x, y, w, h);
    x = screenW / 2 + 5;
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

// The catch. HOT fires his line at the top of the gauge, but a needle in
// the green is easy to miss with two boards in your hands, so the screen
// says it outright: two samples running at arm's length or closer, and
// CAUGHT! takes the trend line, held for a few seconds so a single dip
// does not take it away again. -48 dBm is what two of these boards read
// at about a metre; touching, they read -30 to -40.
static const int8_t   CAUGHT_DBM  = -48;
static const uint32_t CAUGHT_HOLD = 5000;
static uint32_t s_caughtUntil = 0;
static bool     s_caughtFired = false;
static bool     s_gaugeStarted = false;
static uint8_t  s_listOrder[DetectionEngine::HUNT_TARGET_CAP] = {0};
static uint8_t  s_listCount = 0;
static uint8_t  s_listScroll = 0;

void uiHuntInit(TFT_eSPI& t) {
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
    s_lastTrend       = TrendState::NONE;
    s_hotFired        = false;
    s_everHot         = false;
    s_gotFirstSignal  = false;
    s_enterMs         = millis();
    s_stalledFired    = false;
    s_caughtUntil     = 0;
    s_caughtFired     = false;
    s_gaugeStarted    = false;
    s_listScroll      = 0;
}

bool uiHuntHitBack(int x, int y, int screenW, int screenH) {
    int bx, by, bw, bh;
    backButtonRect(screenW, screenH, bx, by, bw, bh);
    return x >= bx && x <= bx + bw && y >= by && y <= by + bh;
}

bool uiHuntCaught() { return (int32_t)(s_caughtUntil - millis()) > 0; }

bool uiHuntHitStop(int x, int y, int screenW, int screenH) {
    int bx, by, bw, bh;
    stopButtonRect(screenW, screenH, bx, by, bw, bh);
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


static void huntListGeom(TFT_eSPI& t, int w, int h,
                         int& top, int& bottom, int& rowH, int& visible) {
    Theme::ButtonBarGeom bar = Theme::computeButtonBar(w, h);
    top = 24;
    bottom = bar.y - 4;
    t.setTextSize(2);
    const int nameH = t.fontHeight();
    t.setTextSize(1);
    const int detailH = t.fontHeight();
    rowH = nameH + detailH + 5;
    visible = (bottom - top) / rowH;
    if (visible < 1) visible = 1;
}

static int huntListScore(const DetectionEngine::HuntTargetInfo& info, uint32_t now) {
    if (!info.seen || now - info.lastSeenMs > 10000) return -200;
    return (int)info.rssi;
}

static void buildHuntOrder(const DetectionEngine& eng, uint32_t now) {
    s_listCount = eng.huntTargetCount();
    for (uint8_t i = 0; i < s_listCount; i++) s_listOrder[i] = i;
    for (uint8_t i = 1; i < s_listCount; i++) {
        const uint8_t key = s_listOrder[i];
        DetectionEngine::HuntTargetInfo keyInfo;
        eng.huntTargetInfo(key, keyInfo);
        const int keyScore = huntListScore(keyInfo, now);
        uint8_t j = i;
        while (j > 0) {
            DetectionEngine::HuntTargetInfo prevInfo;
            eng.huntTargetInfo(s_listOrder[j - 1], prevInfo);
            if (huntListScore(prevInfo, now) >= keyScore) break;
            s_listOrder[j] = s_listOrder[j - 1];
            j--;
        }
        s_listOrder[j] = key;
    }
}

static void drawHuntTrend(TFT_eSPI& t, int x, int y,
                          const DetectionEngine::HuntTargetInfo& info) {
    if (info.samples < 2) return;
    const int delta = (int)info.rssi - (int)info.previousRssi;
    if (delta >= 4)
        t.fillTriangle(x, y + 7, x + 8, y + 7, x + 4, y, Theme::GREEN);
    else if (delta <= -4)
        t.fillTriangle(x, y, x + 8, y, x + 4, y + 7, Theme::RED);
    else
        t.drawFastHLine(x, y + 4, 8, Theme::CYAN);
}

static void drawHuntList(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng) {
    const int w = t.width(), h = t.height();
    t.fillRect(0, 0, w, h, Theme::BG);
    Theme::drawTitleBar(t, ">> HUNT TARGETS <<");

    int top, bottom, rowH, visible;
    huntListGeom(t, w, h, top, bottom, rowH, visible);
    buildHuntOrder(eng, now);

    const int maxScroll = s_listCount > visible ? s_listCount - visible : 0;
    if (s_listScroll > maxScroll) s_listScroll = (uint8_t)maxScroll;

    if (!s_listCount) {
        t.setTextSize(1);
        t.setTextColor(Theme::WHITE, Theme::BG);
        const char* empty = "NO HUNT TARGETS";
        t.setCursor((w - t.textWidth(empty)) / 2, top + 24);
        t.print(empty);
    }

    for (uint8_t row = 0; row < (uint8_t)visible; row++) {
        const uint8_t pos = (uint8_t)(s_listScroll + row);
        if (pos >= s_listCount) break;

        DetectionEngine::HuntTargetInfo info;
        if (!eng.huntTargetInfo(s_listOrder[pos], info)) continue;
        const int y = top + row * rowH;
        const bool fresh = info.seen && now - info.lastSeenMs <= 10000;

        t.drawFastHLine(6, y + rowH - 1, w - 12, Theme::PURPLE);

        char fitted[28], pv[40];
        t.setTextSize(2);
        snprintf(fitted, sizeof fitted, "%s", Privacy::name(info.label, pv, sizeof pv));
        while (fitted[0] && t.textWidth(fitted) > w - 104)
            fitted[strlen(fitted) - 1] = '\0';
        t.setTextColor(Theme::WHITE, Theme::BG);
        t.setCursor(8, y + 1);
        t.print(fitted);

        char rbuf[16];
        if (fresh) snprintf(rbuf, sizeof rbuf, "%d dBm", (int)info.rssi);
        else if (info.seen) snprintf(rbuf, sizeof rbuf, "OUT");
        else snprintf(rbuf, sizeof rbuf, "WAIT");
        const int rw = t.textWidth(rbuf);
        t.setTextColor(fresh ? Theme::CYAN : Theme::AMBER, Theme::BG);
        t.setCursor(w - rw - 24, y + 1);
        t.print(rbuf);
        if (fresh) drawHuntTrend(t, w - 17, y + 5, info);

        t.setTextSize(1);
        t.setTextColor(Theme::VAPOR_PURPLE, Theme::BG);
        t.setCursor(8, y + 18);
        t.print(info.kind == DetectionEngine::WatchKind::BLE ? "BLE" : "WIFI");
        if (info.seen) {
            const uint32_t age = (now - info.lastSeenMs) / 1000;
            t.printf("   seen %lus ago", (unsigned long)age);
        } else {
            t.print("   waiting for signal");
        }
    }

    Theme::ButtonBarGeom bar = Theme::computeButtonBar(w, h);
    const int bw = 120, bx = (w - bw) / 2;
    Theme::drawButton(t, bx, bar.y, bw, bar.h, "[ BACK ]", false);
}

int uiHuntListHitTarget(TFT_eSPI& t, int x, int y, int screenW, int screenH) {
    int top, bottom, rowH, visible;
    huntListGeom(t, screenW, screenH, top, bottom, rowH, visible);
    if (x < 0 || x >= screenW || y < top || y >= bottom) return -1;
    const int row = (y - top) / rowH;
    const int pos = (int)s_listScroll + row;
    if (row < 0 || row >= visible || pos < 0 || pos >= s_listCount) return -1;
    return (int)s_listOrder[pos];
}

bool uiHuntListHitBack(int x, int y, int screenW, int screenH) {
    Theme::ButtonBarGeom bar = Theme::computeButtonBar(screenW, screenH);
    const int bw = 120, bx = (screenW - bw) / 2;
    return x >= bx && x <= bx + bw && y >= bar.y && y < screenH;
}

void uiHuntListScroll(int delta) {
    int n = (int)s_listScroll + delta;
    if (n < 0) n = 0;
    if (n >= s_listCount) n = s_listCount ? s_listCount - 1 : 0;
    s_listScroll = (uint8_t)n;
}

void uiHuntTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng, bool advance) {
    if (eng.huntKind() == DetectionEngine::WatchKind::NONE) {
        drawHuntList(t, now, eng);
        return;
    }

    if (!s_gaugeStarted) {
        s_gaugeStarted = true;
        Squachy::huntReaction(Squachy::HuntMoment::STARTED);
    }

    int w = t.width(), h = t.height();

    Theme::drawTitleBar(t, ">> HUNT MODE <<");

    Theme::ButtonBarGeom bar = Theme::computeButtonBar(w, h);
    int bodyTop = 16, bodyBottom = bar.y - 4;
    t.fillRect(0, bodyTop, w, bodyBottom - bodyTop, Theme::BG);

    // Mini Squachy cameo, same reduced-header-strip reuse ui_rawscan.cpp
    // and ui_watchalert.cpp already use -- his normal idle tick, just
    // given a small box instead of the whole screen. scanningFx ties
    // his little "ping" animation to the hunting theme.
    // A compact screen (240x135) puts him in a column on the left and the
    // gauge in the rest, centred in it, instead of one above the other.
    const bool tiny = Theme::compact();
    const int colX = tiny ? 70 : 0, colW = w - colX;
    const int sqH = tiny ? bodyBottom - bodyTop - 6 : 44;
    Squachy::tick(t, tiny ? 36 : w / 2, bodyTop, sqH, now, advance, 0.6f, true);

    t.setTextSize(1);
    t.setTextWrap(false);
    t.setTextColor(Theme::CYAN, Theme::BG);
    int labelY = tiny ? bodyTop + 2 : bodyTop + sqH + 2;
    char pv[40];
    const char* label = Privacy::name(eng.huntLabel(), pv, sizeof pv);
    int lw = t.textWidth(label);
    int maxLw = colW - 16;
    t.setCursor(colX + (colW - (lw < maxLw ? lw : maxLw)) / 2, labelY);
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

    int textBlockH = tiny ? 26 : 36;
    int gaugeTop = labelY + 12;
    int gaugeBottom = bodyBottom - textBlockH;
    int r = gaugeBottom - gaugeTop;
    int maxRw = (colW - 40) / 2;
    if (r > maxRw) r = maxRw;
    if (r < 30) r = 30;
    int cx = colX + colW / 2, cy = gaugeBottom;

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
    // The catch: this sample and the one before both at arm's length.
    if (rssiN >= 2 && latestRssi >= CAUGHT_DBM && eng.huntRssiAt(rssiN - 2) >= CAUGHT_DBM) {
        s_caughtUntil = now + CAUGHT_HOLD;
        if (!s_caughtFired) {
            s_caughtFired = true;
            Squachy::huntReaction(Squachy::HuntMoment::HOT);
        }
    } else if (rssiN >= 1 && latestRssi < CAUGHT_DBM - 10 && !uiHuntCaught()) {
        s_caughtFired = false;         // walked off again: the next catch counts
    }
    const bool caught = uiHuntCaught();

    // Numeric readout + warmer/colder trend, compared against a sample
    // from ~6 ticks (roughly 12s) back so a single noisy reading can't
    // flip it -- a hard deadband on top of that for the same reason.
    char rbuf[16];
    if (rssiN > 0) snprintf(rbuf, sizeof(rbuf), "%d dBm", (int)latestRssi);
    else           snprintf(rbuf, sizeof(rbuf), "-- dBm");
    t.setTextSize(2);
    int rw = t.textWidth(rbuf);
    t.setTextColor(Theme::WHITE, Theme::BG);
    const int readY = tiny ? cy + 4 : cy + 8;
    t.setCursor(colX + (colW - rw) / 2, readY);
    if (!caught) t.print(rbuf);
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
    // Caught: the word takes the readout's row, in a green box the eye cannot
    // miss from across a table, and the number moves down to the trend line.
    if (caught) {
        t.setTextSize(2);
        const int cw2 = t.textWidth("CAUGHT!") + 16, ch2 = t.fontHeight() + 4;
        const int cx2 = colX + (colW - cw2) / 2, cy2 = readY - 2;
        t.fillRect(cx2, cy2, cw2, ch2, Theme::GREEN);
        t.setTextColor(Theme::BLACK, Theme::GREEN);
        t.setCursor(cx2 + 8, cy2 + 2);
        t.print("CAUGHT!");
        trend = rbuf;
        trendColor = Theme::GREEN;
    }
    t.setTextSize(1);
    int tw = t.textWidth(trend);
    t.setTextColor(trendColor, Theme::BG);
    t.setCursor(colX + (colW - tw) / 2, readY + readoutH + (tiny ? 1 : 2));
    t.print(trend);

    int bx, by, bw, bh;
    backButtonRect(w, h, bx, by, bw, bh);
    Theme::drawButton(t, bx, by, bw, bh, "[ BACK ]", false);
    stopButtonRect(w, h, bx, by, bw, bh);
    Theme::drawButton(t, bx, by, bw, bh, "[ STOP ]", false);
}
