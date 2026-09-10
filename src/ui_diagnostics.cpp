// SquachWatch-CYD — on-device diagnostics screen implementation
#include "ui_diagnostics.h"
#include "clock.h"
#include "theme.h"
#include "detection.h"
#include <Arduino.h>
#include <stdarg.h>

static void backButtonRect(int screenW, int screenH, int& x, int& y, int& w, int& h) {
    Theme::ButtonBarGeom g = Theme::computeButtonBar(screenW, screenH);
    w = 120;
    h = g.h;
    x = (screenW - w) / 2;
    y = g.y;
}

void uiDiagnosticsInit(TFT_eSPI& t) {
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
}

bool uiDiagnosticsHitBack(int x, int y, int screenW, int screenH) {
    int bx, by, bw, bh;
    backButtonRect(screenW, screenH, bx, by, bw, bh);
    return x >= bx && x <= bx + bw && y >= by && y <= by + bh;
}

static int drawLine(TFT_eSPI& t, int y, uint16_t labelColor, const char* label, const char* fmt, ...) {
    t.setTextColor(labelColor, Theme::BG);
    t.setCursor(6, y);
    t.print(label);

    char buf[48];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    t.setTextColor(Theme::WHITE, Theme::BG);
    t.setCursor(6 + t.textWidth(label) + 6, y);
    t.print(buf);
    return y + t.fontHeight() + 2;
}

void uiDiagnosticsTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng, const DiagnosticsInfo& info) {
    (void)now;
    int w = t.width(), h = t.height();

    Theme::drawTitleBar(t, ">> DIAGNOSTICS <<");

    Theme::ButtonBarGeom bar = Theme::computeButtonBar(w, h);
    int bodyTop = 16, bodyBottom = bar.y - 4;
    t.fillRect(0, bodyTop, w, bodyBottom - bodyTop, Theme::BG);

    t.setTextSize(1);
    t.setTextWrap(false);
    int y = bodyTop + 2;

    y = drawLine(t, y, Theme::CYAN, "BOARD:", "%s (%s)", info.boardName,
                 info.usingCapTouch ? "capacitive" : "resistive");
    y = drawLine(t, y, Theme::CYAN, "RESET:", "%s", info.resetReason);
    // Only after an actual panic, and only when the breadcrumb survived. A
    // blank line here means a clean boot, not a missing feature.
    if (info.crash.valid) {
        y = drawLine(t, y, Theme::RED, "LAST CRASH:", "%lum%lus up, %lu free, %lu block",
                     (unsigned long)(info.crash.uptimeMs / 60000),
                     (unsigned long)((info.crash.uptimeMs / 1000) % 60),
                     (unsigned long)info.crash.heapFree,
                     (unsigned long)info.crash.heapBlock);
        y = drawLine(t, y, Theme::RED, "  ON:", "screen %u, %lu detections",
                     (unsigned)info.crash.screen, (unsigned long)info.crash.lifetime);
    }
    // Uptime next to the reset reason on purpose: together they answer
    // "did this thing restart on me", which is one question and not two.
    {
        char up[24];
        Clock::formatUptime(up, sizeof(up));
        y = drawLine(t, y, Theme::CYAN, "UPTIME:", "%s", up);
        char clk[32];
        Clock::formatClock(clk, sizeof(clk));
        y = drawLine(t, y, Theme::CYAN, "CLOCK:", "%s%s", clk,
                     Clock::isSet() ? "" : "  (TIME <epoch> over serial)");
    }
    y = drawLine(t, y, Theme::CYAN, "HEAP:", "%lu free / %lu largest",
                 (unsigned long)info.freeHeap, (unsigned long)info.largestBlock);
    y += 4;

    if (info.hasRaw) {
        y = drawLine(t, y, Theme::VAPOR_PURPLE, "RAW TOUCH:", "%s a=%d b=%d",
                     info.rawTouching ? "DOWN" : "up", info.rawA, info.rawB);
    }
    y = drawLine(t, y, Theme::VAPOR_PURPLE, "MAPPED:", "%s x=%d y=%d",
                 info.touchValid ? "valid" : "--", info.mappedX, info.mappedY);
    y += 4;

    y = drawLine(t, y, Theme::VAPOR_PINK, "CAL SOURCE:", "%s",
                 info.usingSavedCal ? "saved" : "compiled-in default");
    y = drawLine(t, y, Theme::VAPOR_PINK, "CAL RANGE:", "A[%d,%d] B[%d,%d]",
                 info.calA0, info.calA1, info.calB0, info.calB1);
    y += 4;

    // Frame cost. The push is a fixed byte count over SPI, so it moves
    // only if the bus clock does -- printed separately from the drawing
    // work so a change to SPI_FREQUENCY shows up here unambiguously
    // instead of being averaged into one "it feels smoother" number.
    // fps is derived from the whole frame, not the push alone.
    {
        uint32_t fus = info.frameUs ? info.frameUs : 1;
        y = drawLine(t, y, Theme::AMBER, "PUSH:", "%lu.%lu ms",
                     (unsigned long)(info.pushUs / 1000),
                     (unsigned long)((info.pushUs % 1000) / 100));
        y = drawLine(t, y, Theme::AMBER, "FRAME:", "%lu.%lu ms  (%lu fps)",
                     (unsigned long)(fus / 1000),
                     (unsigned long)((fus % 1000) / 100),
                     (unsigned long)(1000000UL / fus));
        // FRAME above is this screen, which has no backdrop. BG is the
        // last animated screen's, and is the only figure here that says
        // anything about the background you picked.
        y = drawLine(t, y, Theme::AMBER, "BG:", "%lu.%lu ms  (max %lu)",
                     (unsigned long)(info.bgUs / 1000),
                     (unsigned long)((info.bgUs % 1000) / 100),
                     (unsigned long)(1000000UL / (info.bgUs ? info.bgUs : 1)));
    }
    y += 4;

    y = drawLine(t, y, Theme::GREEN, "LOG:", "%u entries, %lu lifetime",
                 (unsigned)eng.logCount(), (unsigned long)eng.lifetimeTotal());

#if SQUACH_MESH
    // Phase 0, read without a computer. The device alternates advertising on
    // and off in 30s arms and pools each separately, so leaving it somewhere
    // for ten minutes gives ten samples of each rather than one of a good
    // moment and one of a bad one.
    {
        y += 4;
        const MeshProbe::Stats ms = MeshProbe::stats();
        y = drawLine(t, y, Theme::CYAN, "BLE SEEN:", "%u.%u /s",
                     (unsigned)(ms.offRate / 10), (unsigned)(ms.offRate % 10));
        y = drawLine(t, y, ms.advOn ? Theme::GREEN : Theme::VAPOR_PINK,
                     "ADVERTISING:", "%s", ms.advOn ? "yes" : "no");
        // The other half of phase 0. The broadcaster role was compiled out
        // because its overhead made the CLEAR screen's framebuffer realloc on
        // rotate fail; this build turns it back on, so the largest contiguous
        // block is the canary for that bug returning.
        y = drawLine(t, y, Theme::VAPOR_PINK, "HEAP:", "%lu KB free, %lu KB block",
                     (unsigned long)ms.heapFreeKb, (unsigned long)ms.heapBlockKb);
    }
#endif

    int bx, by, bw, bh;
    backButtonRect(w, h, bx, by, bw, bh);
    Theme::drawButton(t, bx, by, bw, bh, "[ BACK ]", false);
}
