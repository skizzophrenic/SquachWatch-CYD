// SquachWatch-CYD — on-device diagnostics screen implementation
#include "ui_diagnostics.h"
#include "clock.h"
#include "theme.h"
#include "detection.h"
#if SQUACH_MESH
#include "meshtalk.h"
#endif
#include <Arduino.h>
#include <stdarg.h>
#if defined(CYD_GPS)
#include "sd_row.h"   // degreesText() for the FIX line
#endif
#if defined(CYD_GPS_ELECROW)
#include "cyd_gps_uart.h"
#endif

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

// A compact screen (240x135) shows the report a page at a time: lines are
// laid out as usual, shifted up by the page, and the ones outside the body
// are not drawn. A tap on the body turns the page.
static int s_page = 0, s_pages = 1, s_clipTop = 0, s_clipBottom = 32767, s_off = 0;
void uiDiagnosticsNextPage() { s_page = (s_page + 1) % (s_pages > 0 ? s_pages : 1); }
bool uiDiagnosticsPaged() {
#if defined(CYD_GPS_ELECROW)
    return true;
#else
    return Theme::compact();
#endif
}

static int drawLine(TFT_eSPI& t, int y, uint16_t labelColor, const char* label, const char* fmt, ...) {
    const int fh = t.fontHeight();
    const int sy = y - s_off;
    if (sy < s_clipTop || sy + fh > s_clipBottom) return y + fh + 2;
    y = sy;
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
    return y + s_off + t.fontHeight() + 2;
}

void uiDiagnosticsTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng, const DiagnosticsInfo& info) {
    (void)now;
    int w = t.width(), h = t.height();

    Theme::drawTitleBar(t, ">> DIAGNOSTICS <<");

    Theme::ButtonBarGeom bar = Theme::computeButtonBar(w, h);
    int bodyTop = 16, bodyBottom = bar.y - 4;
#if defined(CYD_GPS_ELECROW)
    // Reserve a visible page cue, including on the 320x240 landscape CYD.
    bodyBottom -= 12;
#endif
    t.fillRect(0, bodyTop, w, bodyBottom - bodyTop, Theme::BG);

#if defined(CYD_GPS_ELECROW)
    t.setTextFont(1);
#endif
    t.setTextSize(1);
    t.setTextWrap(false);
    int y = bodyTop + 2;
    const bool paged = uiDiagnosticsPaged();
#if defined(CYD_GPS_ELECROW)
    // Whole-row gaps keep rows from falling across two page clip windows.
    const int sectionGap = t.fontHeight() + 2;
#else
    const int sectionGap = 4;
#endif
    const int pageH = (bodyBottom - bodyTop - 2) / (t.fontHeight() + 2) * (t.fontHeight() + 2);
    s_clipTop = paged ? bodyTop : 0;
    s_clipBottom = paged ? bodyBottom : 32767;
    s_off = paged ? s_page * pageH : 0;

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
#if defined(CYD_GPS_ELECROW)
                     Clock::trusted() ? "" : " (time not set)");
#else
                     Clock::trusted() ? "" : "  (TIME <epoch> over serial)");
#endif
    }
    y = drawLine(t, y, Theme::CYAN, "HEAP:", "%lu free / %lu largest",
                 (unsigned long)info.freeHeap, (unsigned long)info.largestBlock);
#if defined(CYD_GPS)
    if (info.gpsShown) {
#if defined(CYD_GPS_ELECROW)
        const uint32_t bytes = CydGpsUart::receivedBytes();
        const bool uartReady = CydGpsUart::receiving() && CydGpsUart::rxBufferReady();
        y = drawLine(t, y, Theme::CYAN, "GPS UART:", "%u RX%d %lu 8N1",
                     CydGpsUart::uartNumber(), CydGpsUart::rxPin(), (unsigned long)CydGpsUart::baud());
        y = drawLine(t, y, Theme::CYAN, "GPS RX:", "%lu bytes", (unsigned long)bytes);
        y = drawLine(t, y, Theme::CYAN, "GOOD/BAD:", "%lu / %lu",
                     (unsigned long)info.gpsGood, (unsigned long)info.gpsBad);
        const bool receiverPosition = info.source == Gnss::Source::RECEIVER;
        const char* state = !uartReady ? "UART unavailable" : !bytes ? "no serial bytes" :
                            !info.gpsGood ? "bytes / no valid NMEA" : !receiverPosition ? "receiving / no receiver fix" :
                            info.fresh ? "fresh fix" : info.fixAgeMs != UINT32_MAX ? "stale fix" :
                            "receiving / no fix";
        y = drawLine(t, y, info.fresh && receiverPosition && uartReady ? Theme::GREEN : Theme::CYAN,
                     "GPS:", "%s", state);
        y = drawLine(t, y, Theme::CYAN, "SATS:", "%u view / %u heard / %u used",
                     info.view, info.heard, info.used);
        // Position can fall back to a paired phone even while the receiver
        // has NMEA but no solution. Keep its source and age separate from
        // receiver health; only the physical receiver can feed this profile's
        // wardrive export.
        const char* source = receiverPosition ? "RECEIVER" : info.source == Gnss::Source::PHONE ?
                             "PHONE (wardrive excluded)" : "BENCH (wardrive excluded)";
        y = drawLine(t, y, Theme::CYAN, "SOURCE:", "%s", source);
        y = drawLine(t, y, info.fresh && receiverPosition ? Theme::GREEN : Theme::CYAN,
                     "POSITION:", "%s", info.fixAgeMs == UINT32_MAX ? "no fix" : info.fresh ? "fresh fix" : "stale fix");
        if (info.fixAgeMs == UINT32_MAX) {
            y = drawLine(t, y, Theme::CYAN, "FIX:", "none");
        } else {
            y = drawLine(t, y, Theme::CYAN, "FIX:", "%lus; est +-%u m",
                         (unsigned long)(info.fixAgeMs / 1000), info.accM);
            // Separate coordinates fit even the longest negative values in
            // portrait. Accuracy is the existing HDOP estimate, not a survey.
            char coordinate[16];
            SdRow::degreesText(coordinate, sizeof coordinate, info.lat7, 5);
            y = drawLine(t, y, Theme::CYAN, "LAT:", "%s", coordinate);
            SdRow::degreesText(coordinate, sizeof coordinate, info.lon7, 5);
            y = drawLine(t, y, Theme::CYAN, "LON:", "%s", coordinate);
        }
#else
        if (info.gpsGood == 0)
            y = drawLine(t, y, Theme::CYAN, "GPS:", "no data from the module");
        else
            y = drawLine(t, y, Theme::CYAN, "GPS:", "%u in view, %u heard, %u used",
                         info.view, info.heard, info.used);
        if (info.fixAgeMs == UINT32_MAX) {
            y = drawLine(t, y, Theme::CYAN, "FIX:", "none");
        } else {
            char la[16], lo[16], acc[12];
            SdRow::degreesText(la, sizeof la, info.lat7, 5);
            SdRow::degreesText(lo, sizeof lo, info.lon7, 5);
            if (info.faked) snprintf(acc, sizeof acc, "FAKE");
            else            snprintf(acc, sizeof acc, "+-%u m", info.accM);
            y = drawLine(t, y, info.fresh ? Theme::GREEN : Theme::CYAN, "FIX:", "%s,%s %s %lus",
                         la, lo, acc, (unsigned long)(info.fixAgeMs / 1000));
        }
#endif
        // Two lines: the file name does not fit beside the counts at 320 px.
        static const char* const WD[] = { "OFF", "NO CARD", "LOW MEMORY", "WAITING FOR FIX", "ON", "WRITE ERROR" };
#if defined(CYD_GPS_ELECROW)
        // A status plus three ten-digit counters cannot fit on either CYD
        // orientation (or in drawLine's value buffer). Give each a full row.
        y = drawLine(t, y, Theme::CYAN, "WARDRIVE:", "%s", !info.wdOn ? "off" :
                     info.wdState < sizeof WD / sizeof WD[0] ? WD[info.wdState] : "?");
        if (info.wdOn) {
            y = drawLine(t, y, Theme::CYAN, "WRITTEN:", "%lu", (unsigned long)info.wdWritten);
            y = drawLine(t, y, Theme::CYAN, "REPEATS:", "%lu", (unsigned long)info.wdSkipped);
            y = drawLine(t, y, Theme::CYAN, "DROPPED:", "%lu", (unsigned long)info.wdDropped);
        }
#else
        if (!info.wdOn)
            y = drawLine(t, y, Theme::CYAN, "WARDRIVE:", "off");
        else
            y = drawLine(t, y, Theme::CYAN, "WARDRIVE:", "%s, %lu written, %lu repeats, %lu dropped",
                         info.wdState < sizeof WD / sizeof WD[0] ? WD[info.wdState] : "?", (unsigned long)info.wdWritten,
                         (unsigned long)info.wdSkipped, (unsigned long)info.wdDropped);
#endif
        y = drawLine(t, y, Theme::CYAN, "WIGLE:", "%s", info.wdFile && *info.wdFile ? info.wdFile : "no file yet");
    }
#endif
#if SQUACH_MESH
    // Unlike BLE SEEN's old "advertising" flag, this is read from NimBLE's
    // actual controller state. Failed starts can leave the UI looking like
    // SEND worked; show the failure count and last failure stage on-device.
    {
        const Mesh::TxStatus tx = Mesh::txStatus();
        y = drawLine(t, y, tx.advertising ? Theme::GREEN : Theme::AMBER,
                     "MESH TX:", "%s %s, ok %lu / fail %lu (%u)",
                     tx.advertising ? "ON" : "OFF",
                     tx.sendingFrame ? "FRAME" : "IDLE",
                     (unsigned long)tx.starts, (unsigned long)tx.failures,
                     (unsigned)tx.lastFailure);
        const MeshTalk::RxDiagnostics rx = MeshTalk::rxDiagnostics();
        y = drawLine(t, y, Theme::CYAN, "MESH RX:", "%lu queued / %lu heard, full %lu",
                     (unsigned long)rx.queued, (unsigned long)rx.seen,
                     (unsigned long)rx.queueFull);
        y = drawLine(t, y, Theme::CYAN, "INV RX:", "parts %lu mask %x/15 done %lu",
                     (unsigned long)rx.inviteParts, (unsigned)rx.mask,
                     (unsigned long)rx.inviteComplete);
        y = drawLine(t, y, rx.inviteRejected ? Theme::AMBER : Theme::CYAN,
                     "INV ERR:", "%lu rejected (%u), replay %lu",
                     (unsigned long)rx.inviteRejected,
                     (unsigned)rx.lastReject,
                     (unsigned long)rx.replayRejected);
    }
#endif
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
    y += sectionGap;

    if (info.hasRaw) {
        y = drawLine(t, y, Theme::VAPOR_PURPLE, "RAW TOUCH:", "%s a=%d b=%d",
                     info.rawTouching ? "DOWN" : "up", info.rawA, info.rawB);
    }
    y = drawLine(t, y, Theme::VAPOR_PURPLE, "MAPPED:", "%s x=%d y=%d",
                 info.touchValid ? "valid" : "--", info.mappedX, info.mappedY);
    y += sectionGap;

    y = drawLine(t, y, Theme::VAPOR_PINK, "CAL SOURCE:", "%s",
                 info.calSource ? info.calSource
                                : info.usingSavedCal ? "saved" : "compiled-in default");
    y = drawLine(t, y, Theme::VAPOR_PINK, "CAL RANGE:", "A[%d,%d] B[%d,%d]",
                 info.calA0, info.calA1, info.calB0, info.calB1);
    y += sectionGap;

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
    y += sectionGap;

    y = drawLine(t, y, Theme::GREEN, "LOG:", "%u entries, %lu lifetime",
                 (unsigned)eng.logCount(), (unsigned long)eng.lifetimeTotal());

#if SQUACH_MESH
    // Phase 0, read without a computer. The device alternates advertising on
    // and off in 30s arms and pools each separately, so leaving it somewhere
    // for ten minutes gives ten samples of each rather than one of a good
    // moment and one of a bad one.
    {
        y += sectionGap;
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

    if (paged) {
        s_pages = (y - bodyTop - 2 + pageH - 1) / pageH;
        if (s_pages < 1) s_pages = 1;
        if (s_page >= s_pages) s_page = 0;
        char pg[32];
#if defined(CYD_GPS_ELECROW)
        snprintf(pg, sizeof pg, "PAGE %d/%d - tap body", s_page + 1, s_pages);
        t.fillRect(0, bar.y - 16, w, 12, Theme::BG);
        t.setTextColor(Theme::CYAN, Theme::BG);
        t.setCursor((w - t.textWidth(pg)) / 2, bar.y - 12);
#else
        snprintf(pg, sizeof pg, "%d/%d", s_page + 1, s_pages);
        t.setTextColor(Theme::W95_SHADOW, Theme::BG);
        t.setCursor(6, bar.y + (bar.h - t.fontHeight()) / 2);
#endif
        t.print(pg);
    }
    s_clipTop = 0; s_clipBottom = 32767; s_off = 0;
    int bx, by, bw, bh;
    backButtonRect(w, h, bx, by, bw, bh);
    Theme::drawButton(t, bx, by, bw, bh, "[ BACK ]", false);
}
