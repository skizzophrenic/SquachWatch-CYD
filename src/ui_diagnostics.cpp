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
    // To the bottom of the glass, not to the drawn edge: Theme::hitTestButtonBar
    // explains the five bare rows under every bar built from computeButtonBar
    // and why a press landing in them is the button's. Same rule here.
    return x >= bx && x <= bx + bw && y >= by && y < screenH;
}

// The body's floor, and what happened against it. Seventeen lines at the
// built-in font's 8 px plus 2 of lead are 170 rows, and the four group gaps
// another 16: fine under a 20-row bar, not under the 34 the 2.8" CYD grew
// (Theme::buttonBarH) -- there the body is 180 rows and the last line drew
// under [ BACK ]. So a line that would cross the floor is not drawn and is
// counted; on the first frame that counts one the gaps go to zero and the
// frame is drawn again at once (that alone brings 17 lines home on the 2.8"
// and the 2.4"); whatever still does not fit is said in one marked figure at
// the foot instead of being painted under the button.
static int  s_bottom  = 0;
static int  s_skipped = 0;
static int  s_gap     = 4;
static bool s_tight   = false;

static int drawLine(TFT_eSPI& t, int y, uint16_t labelColor, const char* label, const char* fmt, ...) {
    if (y + t.fontHeight() > s_bottom) { s_skipped++; return y; }
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
    s_bottom  = bodyBottom;
    s_skipped = 0;
    s_gap     = s_tight ? 0 : 4;

    t.setTextSize(1);
    t.setTextWrap(false);
    int y = bodyTop + 2;

    y = drawLine(t, y, Theme::CYAN, "BOARD:", "%s (%s)", info.boardName,
                 info.usingCapTouch ? "capacitive" : "resistive");
    y = drawLine(t, y, Theme::CYAN, "RESET:", "%s", info.resetReason);
    // Only after an actual panic, and only when the breadcrumb survived. A
    // blank line here means a clean boot, not a missing feature.
    if (info.crash.valid || info.crash.haveDump) {
        if (info.crash.valid)
            y = drawLine(t, y, Theme::RED, "LAST CRASH:", "%lum%lus up, %lu free, %lu block",
                         (unsigned long)(info.crash.uptimeMs / 60000),
                         (unsigned long)((info.crash.uptimeMs / 1000) % 60),
                         (unsigned long)info.crash.heapFree,
                         (unsigned long)info.crash.heapBlock);
        else
            y = drawLine(t, y, Theme::RED, "LAST CRASH:", "no breadcrumb survived");
        if (info.crash.haveDump) {
            // Where it died, from the core dump. With these two lines and the
            // ELF of the build that crashed, addr2line names the functions --
            // which is the difference between "it crashed" and a fix.
            y = drawLine(t, y, Theme::RED, "  IN:", "%s @ %08lX%s", info.crash.task,
                         (unsigned long)info.crash.pc, info.crash.dumpOlder ? " (old fw)" : "");
            char bt[40] = "--";
            size_t o = 0;
            for (uint8_t i = 0; i < info.crash.btN && i < 3; i++)
                o += snprintf(bt + o, sizeof bt - o, "%s%08lX", i ? " " : "",
                              (unsigned long)info.crash.bt[i]);
            y = drawLine(t, y, Theme::RED, "  BT:", "%s", bt);
        } else {
            y = drawLine(t, y, Theme::RED, "  ON:", "screen %u, %lu detections",
                         (unsigned)info.crash.screen, (unsigned long)info.crash.lifetime);
        }
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
                     Clock::trusted() ? "" : "  (TIME <epoch> over serial)");
    }
    y = drawLine(t, y, Theme::CYAN, "HEAP:", "%lu free / %lu largest",
                 (unsigned long)info.freeHeap, (unsigned long)info.largestBlock);
    // Where the heap went on the way up, in KB: free/largest with WiFi up,
    // with Bluetooth up, and at the first pass of loop().
    {
        const BootHeap bh = bootHeap();
        y = drawLine(t, y, Theme::CYAN, "BOOT:", "wifi %lu/%lu  ble %lu/%lu  loop %lu/%lu KB",
                     (unsigned long)(bh.wifiFree / 1024), (unsigned long)(bh.wifiLargest / 1024),
                     (unsigned long)(bh.bleFree / 1024),  (unsigned long)(bh.bleLargest / 1024),
                     (unsigned long)(info.loopFree / 1024), (unsigned long)(info.loopLargest / 1024));
    }
    y = drawLine(t, y, Theme::CYAN, "SLOT:", "%s  other: %s",
                 info.otaSlot ? info.otaSlot : "?", info.otaOther ? info.otaOther : "none");
    if (info.lora[0]) y = drawLine(t, y, Theme::CYAN, "LORA:", "%s", info.lora);
    // The black box: what is kept in flash across restarts. The newest crash
    // kept by date and version, which is what a photo of this screen needs
    // to match it to a build; BLACKBOX on the console has the rest.
    if (!info.bbReady) {
        y = drawLine(t, y, Theme::CYAN, "KEPT:", "off (flash space in use)");
    } else if (!info.bbCrashes) {
        y = drawLine(t, y, Theme::CYAN, "KEPT:", "%u seen, no crashes", (unsigned)info.bbKept);
    } else {
        char when[12] = "";
        if (info.bbHaveLast) Clock::formatEpochStamp(info.bbLast.epoch, when, sizeof when);
        // The version only where it fits: portrait is forty characters.
        const bool wide = w >= 300;
        y = drawLine(t, y, Theme::RED, "KEPT:", "%u seen, %u crash%s, last %s%s%s",
                     (unsigned)info.bbKept, (unsigned)info.bbCrashes, info.bbCrashes == 1 ? "" : "es",
                     when, wide ? " v" : "", wide ? info.bbLast.version : "");
    }
    y += s_gap;

    if (info.hasRaw) {
        y = drawLine(t, y, Theme::VAPOR_PURPLE, "RAW TOUCH:", "%s a=%d b=%d",
                     info.rawTouching ? "DOWN" : "up", info.rawA, info.rawB);
    }
    y = drawLine(t, y, Theme::VAPOR_PURPLE, "MAPPED:", "%s x=%d y=%d",
                 info.touchValid ? "valid" : "--", info.mappedX, info.mappedY);
    y += s_gap;

    y = drawLine(t, y, Theme::VAPOR_PINK, "CAL SOURCE:", "%s",
                 info.calSource ? info.calSource
                                : info.usingSavedCal ? "saved" : "compiled-in default");
    y = drawLine(t, y, Theme::VAPOR_PINK, "CAL RANGE:", "A[%d,%d] B[%d,%d]",
                 info.calA0, info.calA1, info.calB0, info.calB1);
    y += s_gap;

    // Frame cost. The push is a fixed byte count over SPI, so it moves
    // only if the bus clock does -- kept as its own figure so a change to
    // SPI_FREQUENCY shows up here unambiguously instead of being averaged
    // into one "it feels smoother" number. fps is derived from the whole
    // frame, not the push alone. On one line with FRAME since the crash
    // report grew a line: the screen already ran into the BACK button.
    {
        uint32_t fus = info.frameUs ? info.frameUs : 1;
        y = drawLine(t, y, Theme::AMBER, "FRAME:", "%lu.%lu ms (%lu fps), push %lu.%lu",
                     (unsigned long)(fus / 1000),
                     (unsigned long)((fus % 1000) / 100),
                     (unsigned long)(1000000UL / fus),
                     (unsigned long)(info.pushUs / 1000),
                     (unsigned long)((info.pushUs % 1000) / 100));
        // FRAME above is this screen, which has no backdrop. LAST is the
        // screen you came from -- its whole frame, which is the frame rate
        // you actually saw there -- and the part of it its background took.
        if (info.lastScreenName && info.lastScreenUs) {
            const uint32_t lus = info.lastScreenUs;
            y = drawLine(t, y, Theme::AMBER, "LAST:", "%s %lu.%lu ms (%lu fps), bg %lu.%lu",
                         info.lastScreenName,
                         (unsigned long)(lus / 1000), (unsigned long)((lus % 1000) / 100),
                         (unsigned long)(1000000UL / lus),
                         (unsigned long)(info.bgUs / 1000), (unsigned long)((info.bgUs % 1000) / 100));
        } else {
            y = drawLine(t, y, Theme::AMBER, "LAST:", "no screen timed yet, bg %lu.%lu",
                         (unsigned long)(info.bgUs / 1000), (unsigned long)((info.bgUs % 1000) / 100));
        }
    }
    y += s_gap;

    y = drawLine(t, y, Theme::GREEN, "LOG:", "%u entries, %lu lifetime",
                 (unsigned)eng.logCount(), (unsigned long)eng.lifetimeTotal());

#if SQUACH_MESH
    // Phase 0, read without a computer. The device alternates advertising on
    // and off in 30s arms and pools each separately, so leaving it somewhere
    // for ten minutes gives ten samples of each rather than one of a good
    // moment and one of a bad one.
    {
        y += s_gap;
        const MeshProbe::Stats ms = MeshProbe::stats();
        y = drawLine(t, y, ms.advOn ? Theme::GREEN : Theme::CYAN, "BLE SEEN:", "%u.%u /s, %s%s",
                     (unsigned)(ms.offRate / 10), (unsigned)(ms.offRate % 10),
                     ms.advOn ? "advertising" : "not advertising",
                     scanPassiveNow() ? ", passive" : ", active");
        // The seatbelt's count: adverts refused because the largest free
        // block was under 1.5 KB when they came in. A big number here with
        // a small HEAP line is a board that is only alive because it is
        // ignoring the radio.
        if (advertsDropped())
            y = drawLine(t, y, Theme::VAPOR_PINK, "DROPPED:", "%lu adverts, no heap for them",
                         (unsigned long)advertsDropped());
        // What the once-a-minute scan restart gave back: the leak it closes,
        // measured. This row used to repeat the HEAP line at the top, which
        // already carries free and largest block. Kilobytes a restart in a busy
        // room and a flat heap up top means the leak was NimBLE holding on to
        // devices that never answered; zeros here and a falling heap mean it
        // was not, and the search goes on.
        const ScanFlushStats fs = scanFlushStats();
        y = drawLine(t, y, Theme::VAPOR_PINK, "FLUSHED:", "%lu KB total, %lu B last (x%lu)",
                     (unsigned long)(fs.totalFreed / 1024), (unsigned long)fs.lastFreed,
                     (unsigned long)fs.count);
    }
#endif

    if (s_skipped && !s_tight) {
        // Once: the gaps were the slack, take them and draw again.
        s_tight = true;
        uiDiagnosticsTick(t, now, eng, info);
        return;
    }
    if (s_skipped) {
        // The lines that still did not fit, said where the eye ends up. The
        // marker is one glyph, as everywhere on this device (ui_fit.h).
        char more[16];
        snprintf(more, sizeof more, "+%d more>", s_skipped);
        t.setTextColor(Theme::CYAN, Theme::BG);   // the screen's label ink: a note, not an alarm
        t.setCursor(w - 6 - t.textWidth(more), bodyBottom - t.fontHeight() - 1);
        t.print(more);
    }

    int bx, by, bw, bh;
    backButtonRect(w, h, bx, by, bw, bh);
    Theme::drawButton(t, bx, by, bw, bh, "[ BACK ]", false);
}
