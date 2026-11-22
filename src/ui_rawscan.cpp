// SquachWatch-CYD — manual raw BLE/WiFi scanner screen implementation
#include "ui_rawscan.h"
#include "ui_scroll.h"
#include "theme.h"
#include "privacy.h"
#include "squachy.h"
#include "settings.h"
#include "ignore_list.h"
#include <Arduino.h>

static int g_scroll = 0;

// Shared by rowLayout() and uiRawScanTick() so the two can never
// drift apart -- see rowLayout()'s own comment.
static const int RS_TITLE_BOTTOM = 16;
#if defined(SQW_SMALL)
// No mini Squachy on the small boards: a 70 px strip on a 135 px screen left
// 19 px for a list whose rows are 27, so no result ever showed.
static const int RS_SQ_H         = 0;
#else
static const int RS_SQ_H         = 70;
#endif

// Two buttons in place of the normal three-slot bar: BACK (left) and
// the opposite scan mode (right) -- reuses computeButtonBar()'s y/h so
// the row lines up with every other screen's bar, just two wide
// buttons instead of three narrower ones.
static void bottomButtonRects(int screenW, int screenH,
                               int& backX, int& backY, int& backW, int& backH,
                               int& altX, int& altY, int& altW, int& altH) {
    Theme::ButtonBarGeom g = Theme::computeButtonBar(screenW, screenH);
    const int margin = 8, gap = 8;
    int bw = (screenW - 2 * margin - gap) / 2;
    backX = margin;              backY = g.y; backW = bw; backH = g.h;
    altX  = backX + bw + gap;    altY  = g.y; altW  = bw; altH  = g.h;
}

// Tracked across ticks so uiRawScanTick can fire Squachy's reactions
// on the actual moment they happen (a new BLE device appearing, the
// scan finishing) instead of every single frame.
static uint8_t g_lastBleCount = 0;
static bool    g_reportedDone = false;

static void feedReset();   // the SCAN.LOG window's clock and arrivals, further down

void uiRawScanInit(TFT_eSPI& t, bool isBle) {
    g_scroll = 0;
    g_lastBleCount = 0;
    g_reportedDone = false;
    feedReset();
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
    Squachy::scanReaction(Squachy::ScanMoment::STARTED);
}

void uiRawScanResume(TFT_eSPI& t) {
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
}

void uiRawScanScroll(int delta) {
    g_scroll += delta;
    if (g_scroll < 0) g_scroll = 0;
}

RawScanTap uiRawScanHitTest(int x, int y, int screenW, int screenH) {
    int bx, by, bw, bh, ax, ay, aw, ah;
    bottomButtonRects(screenW, screenH, bx, by, bw, bh, ax, ay, aw, ah);
    if (x >= bx && x <= bx + bw && y >= by && y <= by + bh) return RawScanTap::BACK;
    if (x >= ax && x <= ax + aw && y >= ay && y <= ay + ah) return RawScanTap::SWITCH;
    return RawScanTap::NONE;
}

static void drawBottomBar(TFT_eSPI& t, int w, int h, bool isBle) {
    int bx, by, bw, bh, ax, ay, aw, ah;
    bottomButtonRects(w, h, bx, by, bw, bh, ax, ay, aw, ah);
    Theme::drawButton(t, bx, by, bw, bh, "[ BACK ]", false);
    Theme::drawButton(t, ax, ay, aw, ah, isBle ? "[ WIFI ]" : "[ BLE ]", false);
}

// Shared by uiRawScanTick() (drawing) and uiRawScanRowAt() (hit
// testing) so the two can never drift apart -- rowH depends on live
// font metrics (see the fontHeight() comment below), which is why
// this needs a real TFT_eSPI& rather than being a compile-time
// constant.
static void rowLayout(TFT_eSPI& t, int w, int h, int& bodyTop, int& bodyBottom, int& rowH) {
    (void)t;
    Theme::ButtonBarGeom bar = Theme::computeButtonBar(w, h);
    // Inside the RESULTS.LOG window: under its title bar, over its footer
    // (none on the small boards, where every row counts). Two lines of the
    // 8 px font a row.
    bodyTop    = RS_TITLE_BOTTOM + RS_SQ_H + 14;
#if defined(SQW_SMALL)
    bodyBottom = bar.y - 4 - 3;
#else
    bodyBottom = bar.y - 4 - 14;
#endif
    rowH = 21;
}

// ---- The wait: SCAN.LOG ------------------------------------------------------
// What the screen shows while a scan listens, instead of a pulsing
// SCANNING...: a log window that prints each device as it is heard. Picked
// from the Live Feed lab as pass C5 (2026-11-22): a green screen with
// scanlines, a status strip, each address unscrambling before its name types
// in, kind icons, TRACKER / HUNT / WATCH flags, a side panel (the loudest
// three and how many came in each second; for WiFi, the 13 channels), and a
// SCAN COMPLETE card held for a moment before the list opens.
//
// BLE: devices arrive live (rawBleCount grows). WiFi: the radio listens
// 150 ms per channel in order and names nothing until the end, so the
// channel shown while it runs is reckoned from the clock, and the networks
// are typed in one after another once it is done.
namespace feed {
// The phosphor, as RGB565 that lands on a green level on the 8-bit frames.
const uint16_t STRIPE = 0x0943;   // #0b2a1b, every other row of the window
const uint16_t STRIP  = 0x09C4;   // #0f3a25, the status strip and rules
const uint16_t TEXT   = 0xCFFB;   // #c8ffd9
const uint16_t ON     = 0x37F3;   // #33ff99
const uint16_t LABEL  = 0x1CEB;   // #1f9e5f
const uint16_t DIM    = 0x1348;   // #146b40
const uint16_t HOT    = 0xFD80;   // #ffb000, loud, and trackers
const uint16_t BLUE   = 0x7EBF;   // #7fd4ff, phones, WATCH
const uint16_t ROSE   = 0xFC5B;   // #ff8adf, audio

const uint32_t BLE_MS = 8000;     // RAW_BLE_SCAN_MS in detection.cpp
const uint32_t CH_MS  = 150;      // startRawWifiScan's dwell per channel
const uint32_t HOLD_MS = 1300;    // the SCAN COMPLETE card
const uint32_t NAME_AT = 750;     // a new line: 0.45 s of scramble, the address, then its name
const uint8_t  CAP = 32;

uint32_t startAt = 0, doneAt = 0, rxAt = 0;
uint8_t  known = 0;
uint32_t arrive[CAP];
int32_t  rssiSum = 0;
bool     holding = false;   // the feed is up (see feedHolding)

// 7x7 icons, a row a byte, bit 6 the left column.
const uint8_t I_AUD[7] = { 0x1C, 0x22, 0x41, 0x41, 0x63, 0x63, 0x00 };
const uint8_t I_PH[7]  = { 0x3E, 0x22, 0x22, 0x22, 0x22, 0x36, 0x3E };
const uint8_t I_TAG[7] = { 0x1C, 0x22, 0x49, 0x5D, 0x49, 0x22, 0x1C };
const uint8_t I_BT[7]  = { 0x08, 0x0C, 0x2A, 0x1C, 0x2A, 0x0C, 0x08 };
const uint8_t I_APL[7] = { 0x08, 0x10, 0x36, 0x7F, 0x7E, 0x7F, 0x36 };
const uint8_t I_NET[7] = { 0x3E, 0x41, 0x1C, 0x22, 0x08, 0x00, 0x08 };
const uint8_t I_UNK[7] = { 0x1C, 0x22, 0x04, 0x08, 0x08, 0x00, 0x08 };

void icon(TFT_eSPI& t, int x, int y, const uint8_t* rows, uint16_t c) {
    for (int j = 0; j < 7; j++)
        for (int i = 0; i < 7; i++)
            if (rows[j] & (0x40 >> i)) t.drawPixel(x + i, y + j, c);
}
void txt(TFT_eSPI& t, const char* s, int x, int y, uint16_t c) {
    t.setTextColor(c);
    t.setCursor(x, y);
    t.print(s);
}
void txtR(TFT_eSPI& t, const char* s, int xr, int y, uint16_t c) { txt(t, s, xr - t.textWidth(s), y, c); }
void txtC(TFT_eSPI& t, const char* s, int xc, int y, uint16_t c) { txt(t, s, xc - t.textWidth(s) / 2, y, c); }
// Cut to fit maxW pixels, in place.
void fit(TFT_eSPI& t, char* s, int maxW) {
    size_t n = strlen(s);
    while (n > 1 && t.textWidth(s) > maxW) s[--n] = 0;
}

bool isTracker(const DetectionEngine& eng, const uint8_t* mac) {
    for (uint8_t i = 0; i < eng.logCount(); i++) {
        const Detection* d = eng.logAt(i);
        if (!d || d->channel != 0 || memcmp(d->mac, mac, 6)) continue;
        if (d->type == DetectionType::AIRTAG || d->type == DetectionType::SAMSUNG_TAG ||
            d->type == DetectionType::GOOGLE_TAG || d->type == DetectionType::TILE) return true;
    }
    return false;
}
const uint8_t* bleIcon(const RawBleResult* r, bool tracker, uint16_t& c) {
    if (tracker)                      { c = HOT;   return I_TAG; }
    if (r->kind == RAW_KIND_AIRPODS)  { c = ROSE;  return I_AUD; }
    if (r->kind == RAW_KIND_IPHONE || r->kind == RAW_KIND_ANDROID || r->kind == RAW_KIND_SAMSUNG) { c = BLUE; return I_PH; }
    if (r->kind == RAW_KIND_APPLE)    { c = TEXT;  return I_APL; }
    if (r->name[0])                   { c = ON;    return I_BT; }
    c = LABEL; return I_UNK;
}

uint32_t wifiCascadeMs(uint8_t nets) { return 60 + (uint32_t)(nets < 7 ? nets : 7) * 90 + 250; }
uint32_t holdMs(bool isBle, const DetectionEngine& eng) {
    return isBle ? HOLD_MS : wifiCascadeMs(eng.rawWifiCount()) + HOLD_MS;
}

// The address, still settling: hex digits left of the settled point churn.
void scramble(char* s, uint32_t age, uint32_t seed) {
    const size_t n = strlen(s);
    static const char HEXC[] = "0123456789ABCDEF";
    for (size_t k = 0; k < n; k++) {
        if (s[k] == ':' || s[k] == 'X') continue;
        if (age >= 450u * (uint32_t)(k + 1) / (uint32_t)n) continue;
        const uint32_t v = (seed * 2654435761u) ^ ((uint32_t)k * 40503u) ^ ((age / 40) * 2246822519u);
        s[k] = HEXC[(v >> 13) & 15];
    }
}

// One line's colours by how far up the log it is: the newest three full,
// the next two dimmer, the rest dim -- the old lines fade like phosphor.
uint16_t fade(uint16_t full, int rank) { return rank <= 2 ? full : rank <= 4 ? LABEL : DIM; }

void bar(TFT_eSPI& t, int x, int y, int w, int h, uint32_t num, uint32_t den) {
    t.drawRect(x, y, w, h, LABEL);
    const int n = (w - 4) / 4;
    const int on = den ? (int)((uint64_t)n * (num > den ? den : num) / den) : n;
    for (int i = 0; i < n; i++) t.fillRect(x + 2 + i * 4, y + 2, 3, h - 4, i < on ? ON : STRIP);
}
}  // namespace feed

static void drawFeed(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng, bool isBle, bool done,
                     int top, int bottom, int w) {
    using namespace feed;
    const uint8_t nBle = isBle ? eng.rawBleCount() : 0;
    for (uint8_t i = known; i < nBle && i < CAP; i++) arrive[i] = now;
    if (nBle > known) known = nBle;
    // The RX lamp: lit whenever any reading changed, which is a packet heard.
    if (isBle) {
        int32_t sum = 0;
        for (uint8_t i = 0; i < nBle; i++) { const RawBleResult* r = eng.rawBleAt(i); if (r) sum += (int32_t)r->rssi * (i + 3); }
        if (sum != rssiSum) { rssiSum = sum; rxAt = now; }
    }
    const uint32_t el = now - startAt;
    const uint8_t nNet = (!isBle && done) ? eng.rawWifiCount() : 0;
    const uint32_t sinceDone = done && doneAt ? now - doneAt : 0;
    const uint8_t chNow = done ? 13 : (uint8_t)(el / CH_MS + 1 > 13 ? 13 : el / CH_MS + 1);

    // The window: shadow, black body striped every other row, purple frame
    // and title bar.
    const int x0 = 4, y0 = top + 3, ww = w - 8, wh = bottom - top - 4;
    if (wh < 40) return;
    t.fillRect(x0 + 2, y0 + 2, ww, wh, Theme::BLACK);
    t.fillRect(x0, y0, ww, wh, Theme::BLACK);
    for (int y = y0 + 12; y < y0 + wh - 1; y += 2) t.drawFastHLine(x0 + 1, y, ww - 2, STRIPE);
    t.drawRect(x0, y0, ww, wh, Theme::PURPLE);
    t.fillRect(x0 + 1, y0 + 1, ww - 2, 11, Theme::PURPLE);
    t.setTextFont(1);
    t.setTextSize(1);
    t.setTextWrap(false);
    txt(t, "SCAN.LOG", x0 + 4, y0 + 3, Theme::BG);
    int tx = x0 + 4 + t.textWidth("SCAN.LOG") + 6;
    if (!done) { const char sp[2] = { "|/-\\"[(now / 100) % 4], 0 }; txt(t, sp, tx, y0 + 3, Theme::BG); }
    tx += 10;
    const bool rx = isBle ? (now - rxAt < 120) : (!done && (el % CH_MS) < 40);
    t.drawRect(tx, y0 + 2, 7, 7, Theme::BG);
    t.fillRect(tx + 1, y0 + 3, 5, 5, rx ? ON : Theme::PURPLE);
    txt(t, "RX", tx + 10, y0 + 3, Theme::BG);
    const char* rt = isBle ? "BLE 8000MS" : "WIFI 150MS/CH";
    if (x0 + ww - 4 - t.textWidth(rt) > tx + 26) txtR(t, rt, x0 + ww - 4, y0 + 3, Theme::BG);

    const int ix0 = x0 + 3, ix1 = x0 + ww - 3;
    const bool side = ww >= 290;
    const int px = ix1 - 84;
    const int fx0 = ix0 + 1, fx1 = side ? px - 5 : ix1;
    const int footY = y0 + wh - 13;
    char b[40];

    // The status strip.
    int y = y0 + 14;
    t.fillRect(fx0 - 1, y, fx1 - fx0 + 1, 10, STRIP);
    {
        int loud = -127; uint8_t trackers = 0;
        for (uint8_t i = 0; i < nBle; i++) {
            const RawBleResult* r = eng.rawBleAt(i);
            if (!r) continue;
            if (r->rssi > loud) loud = r->rssi;
            if (isTracker(eng, r->mac)) trackers++;
        }
        char p[4][16]; uint16_t pc[4];
        const uint32_t shown = isBle ? (el < BLE_MS ? el : BLE_MS) : (done ? doneAt - startAt : el);
        snprintf(p[0], 16, "%lu.%luS", (unsigned long)(shown / 1000), (unsigned long)(shown / 100 % 10)); pc[0] = TEXT;
        if (isBle) {
            snprintf(p[1], 16, "FOUND %u", (unsigned)nBle);                       pc[1] = ON;
            if (nBle) snprintf(p[2], 16, "LOUD %d", loud); else snprintf(p[2], 16, "LOUD --"); pc[2] = ON;
            snprintf(p[3], 16, "TRACKERS %u", (unsigned)trackers);                pc[3] = trackers ? HOT : LABEL;
        } else {
            if (done) snprintf(p[1], 16, "CH ALL"); else snprintf(p[1], 16, "CH %u", (unsigned)chNow); pc[1] = ON;
            if (done) snprintf(p[2], 16, "13 HEARD"); else snprintf(p[2], 16, "%u MHZ", 2407u + 5u * chNow); pc[2] = LABEL;
            if (done) snprintf(p[3], 16, "NETS %u", (unsigned)nNet); else snprintf(p[3], 16, "NETS --"); pc[3] = ON;
        }
        int x = fx0 + 3;
        for (int k = 0; k < 4; k++) {
            if (x + t.textWidth(p[k]) > fx1 - 2) break;
            txt(t, p[k], x, y + 1, pc[k]);
            x += t.textWidth(p[k]) + 9;
        }
    }
    y += 12;

    // The log itself.
    const int lh = 10;
    const int maxRows = (footY - 2 - y) / lh;
    if (maxRows > 0) {
        // Everything the log has to say, oldest first; it shows the newest maxRows.
        // BLE: one line per device. WiFi: a line per channel listened to,
        // then a line per network as they are typed in, loudest first.
        uint8_t ord[CAP];
        uint8_t nOrd = 0;
        if (!isBle && done) {
            for (uint8_t i = 0; i < nNet && nOrd < CAP; i++) ord[nOrd++] = i;
            for (uint8_t i = 1; i < nOrd; i++)
                for (uint8_t j = i; j > 0 && eng.rawWifiRssi(ord[j]) > eng.rawWifiRssi(ord[j - 1]); j--) {
                    const uint8_t s = ord[j]; ord[j] = ord[j - 1]; ord[j - 1] = s;
                }
        }
        int total = 0;
        if (isBle) total = nBle < CAP ? nBle : CAP;
        else {
            total = chNow;
            for (uint8_t k = 0; k < nOrd; k++) if (sinceDone >= 60 + (uint32_t)k * 90) total++;
        }
        const int first = total > maxRows ? total - maxRows : 0;
        for (int li = first; li < total; li++) {
            const int ry = y + (li - first) * lh;
            const int rank = total - 1 - li;
            uint32_t age;
            if (isBle) age = now - arrive[li];
            else if (li < 13) age = done ? 100000 : el - (uint32_t)li * CH_MS;
            else age = sinceDone - (60 + (uint32_t)(li - 13) * 90);
            if (!isBle && li < 13 && li == chNow - 1 && !done) age = 0;
            if (age < 200)      t.fillRect(fx0 - 1, ry - 1, fx1 - fx0 + 1, lh, DIM);
            else if (age < 600) t.fillRect(fx0 - 1, ry - 1, fx1 - fx0 + 1, lh, STRIP);

            if (!isBle && li < 13) {
                const bool live = !done && li == chNow - 1;
                snprintf(b, sizeof b, "CH %2u  %u", (unsigned)(li + 1), 2412u + 5u * li);
                txt(t, b, fx0 + 2, ry, live ? TEXT : fade(LABEL, rank));
                if (live) { const int d = 1 + (int)(now / 125) % 3; snprintf(b, sizeof b, "%.*s", d, "..."); txtR(t, b, fx1 - 2, ry, ON); }
                else txtR(t, "HEARD", fx1 - 2, ry, fade(DIM, rank));
                continue;
            }
            // A device or a network: icon, address-then-name, flag, dBm.
            const uint8_t* ic; uint16_t icC; int rssi; const uint8_t* addr; bool tracker = false;
            char label[40];
            if (isBle) {
                const RawBleResult* r = eng.rawBleAt((uint8_t)li);
                if (!r) continue;
                tracker = isTracker(eng, r->mac);
                ic = bleIcon(r, tracker, icC);
                rssi = r->rssi; addr = r->mac;
                if (age < NAME_AT) {
                    Privacy::mac(label, sizeof label, r->mac);
                    if (age < 450) scramble(label, age, (uint32_t)li * 977u + r->mac[5]);
                } else {
                    char pv[40];
                    const char* nm = r->name[0] ? Privacy::name(r->name, pv, sizeof pv) : rawBleKindLabel(r->kind);
                    const uint32_t chars = (age - NAME_AT) * 42 / 1000;
                    snprintf(label, sizeof label, "%.*s", (int)(chars < 39 ? chars : 39), nm);
                }
            } else {
                const uint8_t i = ord[li - 13];
                ic = I_NET; icC = ON;
                rssi = eng.rawWifiRssi(i); addr = eng.rawWifiBssid(i);
                char pv[40];
                const char* nm = Privacy::name(eng.rawWifiSsid(i), pv, sizeof pv);
                const uint32_t chars = age * 42 / 1000;
                snprintf(label, sizeof label, "%.*s", (int)(chars < 39 ? chars : 39), nm[0] ? nm : "(hidden)");
            }
            icon(t, fx0 + 2, ry, ic, rank <= 4 ? icC : DIM);
            snprintf(b, sizeof b, "%d", rssi);
            const int rxR = fx1 - 2, rw = t.textWidth("-100");
            txtR(t, b, rxR, ry, fade(TEXT, rank));
            const char* flag = nullptr; uint16_t flagC = 0;
            if (tracker)                                                 { flag = "TRACKER"; flagC = HOT; }
            else if (addr && eng.isHunted(addr, isBle))                  { flag = "HUNT";    flagC = Theme::VAPOR_PINK; }
            else if (addr && eng.isWatched(addr, isBle))                 { flag = "WATCH";   flagC = BLUE; }
            else if (!isBle) { snprintf(b, sizeof b, "CH%u", (unsigned)eng.rawWifiChannel(ord[li - 13])); flag = b; flagC = LABEL; }
            int nameR = rxR - rw - 4;
            if (flag) { txtR(t, flag, nameR, ry, rank <= 4 ? flagC : DIM); nameR -= t.textWidth(flag) + 5; }
            fit(t, label, nameR - (fx0 + 12));
            const bool settling = isBle && age < NAME_AT;
            txt(t, label, fx0 + 12, ry, settling ? LABEL : fade(rssi > -60 ? HOT : ON, rank));
        }
        // The cursor, under the last line while it listens.
        const int shown = total - first;
        if (!done && shown < maxRows && (now / 400) % 2 == 0) t.fillRect(fx0 + 2, y + shown * lh, 5, 7, ON);
    }

    // The side panel.
    if (side) {
        const int py = y0 + 14, base = footY - 3;
        t.drawFastVLine(px - 3, py - 1, base - py + 2, STRIP);
        if (isBle) {
            txt(t, "LOUDEST", px, py, LABEL);
            int8_t taken[3] = { -1, -1, -1 };
            for (int k = 0; k < 3; k++) {
                int best = -1;
                for (uint8_t i = 0; i < nBle; i++) {
                    if (i == taken[0] || i == taken[1]) continue;
                    const RawBleResult* r = eng.rawBleAt(i);
                    if (r && (best < 0 || r->rssi > eng.rawBleAt((uint8_t)best)->rssi)) best = i;
                }
                if (best < 0) break;
                taken[k] = (int8_t)best;
                const RawBleResult* r = eng.rawBleAt((uint8_t)best);
                const int yy = py + 11 + k * 15;
                char pv[40], nm[24];
                snprintf(nm, sizeof nm, "%s", r->name[0] ? Privacy::name(r->name, pv, sizeof pv) : rawBleKindLabel(r->kind));
                fit(t, nm, 82);
                txt(t, nm, px, yy, TEXT);
                int cells = (r->rssi + 95) * 12 / 45;
                cells = cells < 1 ? 1 : cells > 12 ? 12 : cells;
                for (int c = 0; c < 12; c++) t.fillRect(px + c * 7, yy + 9, 6, 3, c < cells ? (c > 8 ? HOT : ON) : STRIP);
            }
            if (base - 34 > py + 11 + 3 * 15) {
                txt(t, "PER SECOND", px, base - 34, LABEL);
                const uint32_t secNow = el / 1000;
                for (uint32_t s = 0; s < 8; s++) {
                    int n = 0;
                    for (uint8_t i = 0; i < nBle && i < CAP; i++) if ((arrive[i] - startAt) / 1000 == s) n++;
                    int bh = 2 + n * 6;
                    if (bh > 23) bh = 23;
                    const bool cur = !done && s == secNow;
                    t.fillRect(px + (int)s * 10, base - bh, 8, bh, s <= secNow ? (cur ? HOT : ON) : STRIP);
                }
            }
        } else {
            txt(t, "CHANNELS", px, py, LABEL);
            for (uint8_t ch = 1; ch <= 13; ch++) {
                const int cx = px + ((ch - 1) % 2) * 42, cy = py + 11 + ((ch - 1) / 2) * 10;
                if (cy + 8 > base) break;
                const bool now_ = !done && ch == chNow, heard = done || ch < chNow;
                snprintf(b, sizeof b, "%2u", (unsigned)ch);
                txt(t, b, cx, cy, now_ ? HOT : heard ? TEXT : DIM);
                uint8_t nets = 0;
                if (done) for (uint8_t i = 0; i < nNet; i++) if (eng.rawWifiChannel(i) == ch) nets++;
                const int bw = done ? (nets ? (nets * 7 > 22 ? 22 : nets * 7) : 2) : 22;
                t.fillRect(cx + 15, cy + 1, 22, 6, STRIP);
                if (now_)       t.fillRect(cx + 15, cy + 1, 22, 6, (now / 100) % 2 ? HOT : ON);
                else if (heard) t.fillRect(cx + 15, cy + 1, bw, 6, done ? (nets ? ON : DIM) : DIM);
            }
        }
    }

    // The footer: elapsed against the whole scan, and the count.
    t.drawFastHLine(ix0, footY - 2, ix1 - ix0, STRIP);
    const int bw = (fx1 - fx0) / 2 > 150 ? 150 : (fx1 - fx0) / 2;
    bar(t, fx0, footY, bw, 9, done ? 1 : el, done ? 1 : (isBle ? BLE_MS : 13 * CH_MS + 250));
    if (done) snprintf(b, sizeof b, "DONE");
    else { const uint32_t s = isBle && el > BLE_MS ? BLE_MS : el; snprintf(b, sizeof b, "%lu.%lu S", (unsigned long)(s / 1000), (unsigned long)(s / 100 % 10)); }
    txt(t, b, fx0 + bw + 8, footY + 1, TEXT);
    if (isBle || done) snprintf(b, sizeof b, "%u FOUND", (unsigned)(isBle ? nBle : nNet));
    else snprintf(b, sizeof b, "CH %u", (unsigned)chNow);
    txtR(t, b, ix1 - 2, footY + 1, ON);

    // SCAN COMPLETE, once the WiFi names are in.
    if (done && (isBle || sinceDone >= wifiCascadeMs(nNet))) {
        const int bw2 = ww - 20 < 200 ? ww - 20 : 200, bh = 52;
        const int bx = x0 + (ww - bw2) / 2, by = y0 + (wh - bh) / 2 + 4;
        t.fillRect(bx + 3, by + 3, bw2, bh, Theme::BLACK);
        t.fillRect(bx, by, bw2, bh, Theme::BLACK);
        t.drawRect(bx, by, bw2, bh, ON);
        t.drawRect(bx + 2, by + 2, bw2 - 4, bh - 4, DIM);
        txtC(t, "SCAN COMPLETE", bx + bw2 / 2, by + 8, TEXT);
        snprintf(b, sizeof b, "%u %s", (unsigned)(isBle ? nBle : nNet), isBle ? "DEVICES" : "NETWORKS");
        t.setTextSize(2);
        if (t.textWidth(b) > bw2 - 8) t.setTextSize(1);
        txtC(t, b, bx + bw2 / 2, by + 20, ON);
        t.setTextSize(1);
        txtC(t, (now / 330) % 2 ? "OPENING LIST_" : "OPENING LIST ", bx + bw2 / 2, by + 39, LABEL);
    }
}

// True while a finished scan still shows the feed: the WiFi names typing in,
// then the SCAN COMPLETE card.
static bool feedHolding(uint32_t now, bool isBle, bool done, const DetectionEngine& eng) {
    // holding: the feed is on screen (listening, or the hold after), so the
    // list's rows are not there to tap or for the cursor to stop on.
    if (!done) { feed::doneAt = 0; feed::holding = true; return false; }
    if (!feed::doneAt) feed::doneAt = now ? now : 1;
    feed::holding = now - feed::doneAt < feed::holdMs(isBle, eng);
    return feed::holding;
}

static void feedReset() {
    feed::startAt = millis();
    feed::doneAt = feed::rxAt = 0;
    feed::known = 0;
    feed::rssiSum = 0;
    feed::holding = false;
}

// Row index (0 = topmost visible, adjusted for current scroll) a tap
// at (x,y) falls within, or -1 if it's outside the list entirely (the
// Squachy strip, the button bar, or past the last row). Used to
// disambiguate a long-press-to-watch gesture from the drag-to-scroll
// one -- see main.cpp's RAWSCAN case.
int uiRawScanRowAt(TFT_eSPI& t, int x, int y, int screenW, int screenH) {
    int bodyTop, bodyBottom, rowH;
    rowLayout(t, screenW, screenH, bodyTop, bodyBottom, rowH);
    if (feed::holding) return -1;             // the list is not up yet
    if (y < bodyTop || y >= bodyBottom) return -1;
    // The list's own column: the summary beside it is not a row, and the
    // cursor's highlight should stop where the rows do (drawList's fx0..fx1).
    const int ww = screenW - 8, fx1 = ww >= 290 ? screenW - 96 : screenW - 7;
    if (x < 8 || x > fx1) return -1;
    return g_scroll + (y - bodyTop) / rowH;
}

// ---- The results: RESULTS.LOG ------------------------------------------------
// Once the scan is done the same green window stays up and becomes the list
// (asked for 2026-11-22: "make the list view look as good"). Two lines a
// device: its icon, name, dBm and bars, then its address (or channel and
// lock) with its flags. Rows are where rowLayout() says, which is what the
// taps and the cursor use. On a wide enough screen the side panel sums the
// scan up: how many of each kind, or how many open and locked networks.
namespace feed {
void frame(TFT_eSPI& t, int x0, int y0, int ww, int wh, const char* title, const char* right) {
    t.fillRect(x0 + 2, y0 + 2, ww, wh, Theme::BLACK);
    t.fillRect(x0, y0, ww, wh, Theme::BLACK);
    for (int y = y0 + 12; y < y0 + wh - 1; y += 2) t.drawFastHLine(x0 + 1, y, ww - 2, STRIPE);
    t.drawRect(x0, y0, ww, wh, Theme::PURPLE);
    t.fillRect(x0 + 1, y0 + 1, ww - 2, 11, Theme::PURPLE);
    t.setTextFont(1);
    t.setTextSize(1);
    t.setTextWrap(false);
    txt(t, title, x0 + 4, y0 + 3, Theme::BG);
    if (right && x0 + ww - 4 - t.textWidth(right) > x0 + 8 + t.textWidth(title)) txtR(t, right, x0 + ww - 4, y0 + 3, Theme::BG);
}
void bars3(TFT_eSPI& t, int x, int base, int rssi) {
    const int lv = rssi > -62 ? 3 : rssi > -74 ? 2 : 1;
    for (int k = 0; k < 3; k++) { const int hh = 2 + k * 2; t.fillRect(x + k * 3, base - hh, 2, hh, k < lv ? ON : STRIP); }
}
// One line of the summary: an icon (or none), a word, a number on the right.
void sumRow(TFT_eSPI& t, int x, int xr, int y, const uint8_t* ic, uint16_t icC, const char* what, unsigned n, uint16_t c) {
    if (ic) icon(t, x, y, ic, n ? icC : DIM);
    txt(t, what, x + (ic ? 10 : 0), y, n ? c : DIM);
    char b[8];
    snprintf(b, sizeof b, "%u", n);
    txtR(t, b, xr, y, n ? TEXT : DIM);
}
}  // namespace feed

static void drawList(TFT_eSPI& t, const DetectionEngine& eng, bool isBle,
                     int w, int h, int listTop, int listBottom, int rowH) {
    using namespace feed;
    const uint8_t count = isBle ? eng.rawBleCount() : eng.rawWifiCount();
    const int barY = Theme::computeButtonBar(w, h).y;
    const int x0 = 4, y0 = listTop - 13, ww = w - 8, wh = (barY - 5) - y0;
    char b[40];
    if (isBle) snprintf(b, sizeof b, "BLE  %u FOUND", (unsigned)count);
    else       snprintf(b, sizeof b, "WIFI  %u NETS", (unsigned)count);
    frame(t, x0, y0, ww, wh, "RESULTS.LOG", b);

    const int ix0 = x0 + 3, ix1 = x0 + ww - 3;
    const bool side = ww >= 290;
    const int px = ix1 - 84;
    const int fx0 = ix0 + 1, fx1 = side ? px - 5 : ix1;
    const bool footer = listBottom < barY - 10;

    if (count == 0) {
        t.setTextSize(2);
        txtC(t, "NOTHING HEARD", (fx0 + fx1) / 2, listTop + (listBottom - listTop) / 2 - 12, ON);
        t.setTextSize(1);
        txtC(t, isBle ? "TRY AGAIN, OR TRY WIFI" : "TRY AGAIN, OR TRY BLE", (fx0 + fx1) / 2, listTop + (listBottom - listTop) / 2 + 8, LABEL);
    }

    // The rows.
    const int bodyH = listBottom - listTop;
    uiClampScroll(g_scroll, count, bodyH, rowH);
    const int max = bodyH / rowH;
    const bool scrolls = count > max;
    const int rr = fx1 - (scrolls ? 6 : 2);           // the rows' right edge, clear of the scrollbar
    for (int i = 0; i < max && g_scroll + i < count; i++) {
        const int idx = g_scroll + i;
        const int ry = listTop + i * rowH, l2 = ry + 10;
        if (i + 1 < max && idx + 1 < count) t.drawFastHLine(fx0, ry + rowH - 2, rr - fx0, STRIP);
        const uint8_t* ic; uint16_t icC; int rssi; const uint8_t* addr; bool tracker = false;
        char name[40], line2[28];
        char pv[40];
        int arrow = 0;
        if (isBle) {
            const RawBleResult* r = eng.rawBleAt((uint8_t)idx);
            if (!r) break;
            tracker = isTracker(eng, r->mac);
            ic = bleIcon(r, tracker, icC);
            rssi = r->rssi; addr = r->mac;
            snprintf(name, sizeof name, "%s", r->name[0] ? Privacy::name(r->name, pv, sizeof pv) : rawBleKindLabel(r->kind));
            Privacy::mac(line2, sizeof line2, r->mac);
            const int d = (int)r->rssi - (int)r->prev;
            arrow = d >= 4 ? 1 : d <= -4 ? -1 : 0;
        } else {
            ic = I_NET; icC = ON;
            rssi = eng.rawWifiRssi((uint8_t)idx); addr = eng.rawWifiBssid((uint8_t)idx);
            const char* nm = Privacy::name(eng.rawWifiSsid((uint8_t)idx), pv, sizeof pv);
            snprintf(name, sizeof name, "%s", nm[0] ? nm : "(hidden)");
            const unsigned ch = eng.rawWifiChannel((uint8_t)idx);
            snprintf(line2, sizeof line2, "%sCH%u  %s", ch > 14 ? "5G " : "", ch, eng.rawWifiOpen((uint8_t)idx) ? "OPEN" : "LOCKED");
        }
        // Line one: icon, name, dBm and bars.
        icon(t, fx0 + 2, ry + 1, ic, icC);
        bars3(t, rr - 9, ry + 8, rssi);
        snprintf(b, sizeof b, "%d", rssi);
        txtR(t, b, rr - 13, ry + 1, TEXT);
        fit(t, name, rr - 13 - t.textWidth("-100") - 4 - (fx0 + 12));
        txt(t, name, fx0 + 12, ry + 1, rssi > -60 ? HOT : ON);
        // Line two: the address or channel, the flags, nearer or further.
        txt(t, line2, fx0 + 12, l2, LABEL);
        int fx = fx0 + 12 + t.textWidth(line2) + 6;
        auto flag = [&](const char* s, uint16_t c) {
            if (fx + t.textWidth(s) > rr - 10) return;
            txt(t, s, fx, l2, c);
            fx += t.textWidth(s) + 5;
        };
        if (tracker) flag("TRACKER", HOT);
        if (addr && eng.isHunted(addr, isBle))  flag("HUNT", Theme::VAPOR_PINK);
        if (addr && eng.isWatched(addr, isBle)) flag("WATCH", BLUE);
        if (addr && IgnoreList::contains(addr)) flag("IGNORE", Theme::AMBER);
        if (arrow) {
            const int ax = rr - 7, ay = l2 + 1;
            if (arrow > 0) t.fillTriangle(ax, ay + 6, ax + 6, ay + 6, ax + 3, ay, ON);
            else           t.fillTriangle(ax, ay, ax + 6, ay, ax + 3, ay + 6, Theme::RED);
        }
    }
    // The scrollbar, in the list's own colours.
    if (scrolls) {
        const int sx = fx1 - 3;
        t.fillRect(sx, listTop, 2, bodyH, STRIP);
        const int th = bodyH * max / count < 6 ? 6 : bodyH * max / count;
        const int ty = listTop + (bodyH - th) * g_scroll / (count - max);
        t.fillRect(sx, ty, 2, th, ON);
    }

    // The summary.
    if (side) {
        const int py = listTop - 1, base = barY - 8;
        t.drawFastVLine(px - 3, py - 1, base - py + 2, STRIP);
        int yy = py;
        if (isBle) {
            unsigned phones = 0, audio = 0, apple = 0, named = 0, unknown = 0, trackers = 0;
            int loud = 0; int8_t loudR = -128;
            for (uint8_t i = 0; i < count; i++) {
                const RawBleResult* r = eng.rawBleAt(i);
                if (!r) continue;
                if (r->rssi > loudR) { loudR = r->rssi; loud = i; }
                if (isTracker(eng, r->mac)) { trackers++; continue; }
                if (r->kind == RAW_KIND_AIRPODS) audio++;
                else if (r->kind == RAW_KIND_IPHONE || r->kind == RAW_KIND_ANDROID || r->kind == RAW_KIND_SAMSUNG) phones++;
                else if (r->kind == RAW_KIND_APPLE) apple++;
                else if (r->name[0]) named++;
                else unknown++;
            }
            txt(t, "BY KIND", px, yy, LABEL); yy += 11;
            sumRow(t, px, ix1 - 2, yy, I_PH,  BLUE,  "PHONES",   phones,   TEXT); yy += 10;
            sumRow(t, px, ix1 - 2, yy, I_AUD, ROSE,  "AUDIO",    audio,    TEXT); yy += 10;
            sumRow(t, px, ix1 - 2, yy, I_APL, TEXT,  "APPLE",    apple,    TEXT); yy += 10;
            sumRow(t, px, ix1 - 2, yy, I_BT,  ON,    "NAMED",    named,    TEXT); yy += 10;
            sumRow(t, px, ix1 - 2, yy, I_UNK, LABEL, "UNKNOWN",  unknown,  TEXT); yy += 10;
            sumRow(t, px, ix1 - 2, yy, I_TAG, HOT,   "TRACKERS", trackers, HOT);  yy += 13;
            if (count && yy + 18 <= base) {
                const RawBleResult* r = eng.rawBleAt((uint8_t)loud);
                char pv[40], nm[24];
                txt(t, "LOUDEST", px, yy, LABEL);
                snprintf(b, sizeof b, "%d", r->rssi); txtR(t, b, ix1 - 2, yy, HOT);
                snprintf(nm, sizeof nm, "%s", r->name[0] ? Privacy::name(r->name, pv, sizeof pv) : rawBleKindLabel(r->kind));
                fit(t, nm, 82);
                txt(t, nm, px, yy + 10, TEXT);
            }
        } else {
            unsigned open = 0, g24 = 0, g5 = 0;
            uint8_t perCh[15] = { 0 };
            int loud = 0; int8_t loudR = -128;
            for (uint8_t i = 0; i < count; i++) {
                if (eng.rawWifiOpen(i)) open++;
                const uint8_t ch = eng.rawWifiChannel(i);
                if (ch > 14) g5++; else { g24++; if (ch) perCh[ch]++; }
                if (eng.rawWifiRssi(i) > loudR) { loudR = eng.rawWifiRssi(i); loud = i; }
            }
            uint8_t busy = 0;
            for (uint8_t ch = 1; ch <= 14; ch++) if (perCh[ch] > perCh[busy]) busy = ch;
            txt(t, "SUMMARY", px, yy, LABEL); yy += 11;
            sumRow(t, px, ix1 - 2, yy, nullptr, 0, "OPEN",    open,         HOT);  yy += 10;
            sumRow(t, px, ix1 - 2, yy, nullptr, 0, "LOCKED",  count - open, TEXT); yy += 10;
            sumRow(t, px, ix1 - 2, yy, nullptr, 0, "2.4 GHZ", g24,          TEXT); yy += 10;
#if SQW_WIFI_5G
            sumRow(t, px, ix1 - 2, yy, nullptr, 0, "5 GHZ",   g5,           TEXT); yy += 10;
#else
            (void)g5;
#endif
            if (busy) {
                txt(t, "BUSIEST", px, yy, TEXT);
                snprintf(b, sizeof b, "CH %u", (unsigned)busy); txtR(t, b, ix1 - 2, yy, ON);
                yy += 10;
            }
            yy += 3;
            if (count && yy + 18 <= base) {
                char pv[40], nm[24];
                txt(t, "LOUDEST", px, yy, LABEL);
                snprintf(b, sizeof b, "%d", eng.rawWifiRssi((uint8_t)loud)); txtR(t, b, ix1 - 2, yy, HOT);
                const char* s = Privacy::name(eng.rawWifiSsid((uint8_t)loud), pv, sizeof pv);
                snprintf(nm, sizeof nm, "%s", s[0] ? s : "(hidden)");
                fit(t, nm, 82);
                txt(t, nm, px, yy + 10, TEXT);
            }
        }
    }

    // The footer: how to pick one, and where the list is.
    if (footer && count) {
        const int fy = listBottom + 3;
        t.drawFastHLine(ix0, fy - 2, ix1 - ix0, STRIP);
        txt(t, "HOLD A ROW TO OPEN IT", fx0 + 2, fy + 1, LABEL);
        const int last = g_scroll + max < count ? g_scroll + max : count;
        snprintf(b, sizeof b, "%d-%d OF %u", g_scroll + 1, last, (unsigned)count);
        if (fx0 + 2 + t.textWidth("HOLD A ROW TO OPEN IT") + 8 + t.textWidth(b) < ix1 - 2) txtR(t, b, ix1 - 2, fy + 1, ON);
    }
}

void uiRawScanTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng, bool isBle, bool done,
                    bool advance) {
    int w = t.width();
    int h = t.height();

    // Squachy gets a small strip of his own below the title bar -- a
    // mini version of his CLEAR-screen self (minScale below his usual
    // scale-1.0 floor, see squachy.h), at 80% of his normal size. sqH
    // is sized so that scale comfortably lands at ~0.8 on its own
    // (charAvail = sqH - his 16px bubble row =~ BASE_HEIGHT * 0.8);
    // minScale is there as a floor, not the thing forcing this size.
    const int sqTop = RS_TITLE_BOTTOM;
    const int sqH    = RS_SQ_H;
    int listTop, listBottom, rowH;
    rowLayout(t, w, h, listTop, listBottom, rowH);
    // The whole area under Squachy, which the background and the windows fill.
    const int bodyTop    = RS_TITLE_BOTTOM + RS_SQ_H;
    const int bodyBottom = Theme::computeButtonBar(w, h).y - 4;


    // Fire Squachy's reactions on the actual moment they happen, not
    // every frame: a new BLE device appearing (WiFi's count only ever
    // jumps once, all at once, when the sweep finishes, so this is
    // BLE-only), and the scan finishing (latched via g_reportedDone so
    // it only fires once per scan, whichever mode).
    if (isBle && !done) {
        uint8_t n = eng.rawBleCount();
        if (n > g_lastBleCount) Squachy::scanReaction(Squachy::ScanMoment::HIT);
        g_lastBleCount = n;
    }
    if (done && !g_reportedDone) {
        g_reportedDone = true;
        uint8_t n = isBle ? eng.rawBleCount() : eng.rawWifiCount();
        Squachy::scanReaction(n > 0 ? Squachy::ScanMoment::DONE_FOUND : Squachy::ScanMoment::DONE_EMPTY, n);
    }

    // Whatever background style is picked in Settings, drawn ONCE as a
    // single continuous pass from just below the title bar all the way
    // to the bottom of the list (sqTop..bodyBottom) -- Squachy's strip
    // and the results list used to each get their own separate call
    // (the strip via a setViewport crop of a *full-screen-scaled*
    // render, the list via its own bodyTop..bodyBottom-scaled render),
    // which are two different scalings/phases of the "same" effect and
    // read as two disconnected backgrounds stacked on top of each
    // other, with a visible seam and brightness jump where they met.
    // One call means one continuous scaling, so it reads as one scene
    // Squachy happens to be standing in front of instead of a patchwork.
    // Dimmed the same way Settings'/LOG's bodies are (see
    // Theme::dimPaletteForOverlay()'s comment) -- needed for the list
    // text below to stay legible, and looks fine behind Squachy too;
    // none of the states below (scanning/empty/results) draw an opaque
    // full-row fill of their own, just a drawFastHLine separator and
    // per-field two-arg setTextColor() calls that already erase their
    // own glyph cells.
    Theme::Palette saved = Theme::dimPaletteForOverlay(179);
        // The background starts at the TOP OF THE SCREEN, not at the body's own
    // top. Those first sixteen rows used to be the title bar's; nothing owns
    // them now except the two corner buttons, which draw their own opaque
    // boxes over whatever is behind them. Leaving the animation to start
    // below them left a flat dead strip across the top of this screen --
    // the same relic CLEAR had, and the same fix.
    //
    // Only the BACKGROUND moves. Everything else on this screen still
    // begins where it did, so no content shifts.
    const int bgTop = 0;
if (!Theme::stillBackdrop(t)) switch (Settings::background()) {
        case Settings::Background::STARFIELD: Theme::drawStarfield(t, now, bgTop, bodyBottom); break;
        case Settings::Background::TOASTERS:   Theme::drawFlyingToasters(t, now, bgTop, bodyBottom); break;
        case Settings::Background::AQUARIUM:   Theme::drawAquarium(t, now, bgTop, bodyBottom); break;
        case Settings::Background::TERMINAL:   Theme::drawTerminalLog(t, now, bgTop, bodyBottom); break;
        case Settings::Background::FIREFLIES:  Theme::drawFireflies(t, now, bgTop, bodyBottom); break;
        case Settings::Background::FIRE:       Theme::drawFire(t, now, bgTop, bodyBottom); break;
        case Settings::Background::SNOWFALL:   Theme::drawSnowfall(t, now, bgTop, bodyBottom); break;
        case Settings::Background::SPECTRUM:   Theme::drawGibson(t, now, bgTop, bodyBottom, eng); break;
        case Settings::Background::SYNTHWAVE: Theme::drawSynthwave(t, now, bgTop, bodyBottom); break;
        // Fills rather than skips -- see the note in drawActiveBackground.
        case Settings::Background::BLACK:      t.fillRect(0, bgTop, t.width(), bodyBottom - bgTop, Theme::BG); break;
        default:                               Theme::drawDigitalRain(t, now, bgTop, bodyBottom, true); break;
    }
    Theme::restorePalette(saved);
#if defined(SQW_SMALL)
    // The 4 px between the list and the buttons: no background reaches it,
    // and at 135 rows the TRACK panel draws there, so it stayed behind.
    t.fillRect(0, bodyBottom, w, Theme::computeButtonBar(w, h).y - bodyBottom, Theme::BG);
#endif
    char title[32];
    snprintf(title, sizeof(title), ">> %s SCAN <<", isBle ? "BLE" : "WIFI");
    Theme::drawTitleBar(t, title);

    // No viewport needed now that the background above already covers
    // this whole strip directly at its real screen position -- drawn
    // straight on top of it, same as ui_hunt.cpp/ui_watchalert.cpp's
    // mini cameos already do.
    if (sqH > 0) Squachy::tick(t, w / 2, sqTop, sqH, now, advance, 0.8f, !done);

    // While it listens, and for a moment after: the SCAN.LOG window.
    const bool hold = feedHolding(now, isBle, done, eng);
    if (!done || hold) {
        drawFeed(t, now, eng, isBle, done, bodyTop - 2, bodyBottom, w);
        drawBottomBar(t, w, h, isBle);
        return;
    }

    drawList(t, eng, isBle, w, h, listTop, listBottom, rowH);
    drawBottomBar(t, w, h, isBle);

}
