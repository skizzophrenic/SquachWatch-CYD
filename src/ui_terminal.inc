// SquachWatch-CYD -- TERMINAL MODE's home screen (what BORING MODE became).
//
// Included into ui_clear.cpp, after the counter helpers it shares
// (counterLabel, activeCounterTypes, uiClearColumnHolds). With the mode on,
// CLEAR draws this instead of the background, Squachy, the pets and the
// counter tiles: a desktop of windows -- the LOG, scrolling, on the left, and
// AIR / COUNTS / LATEST down the right -- between the title bar and the
// buttons, which stay exactly as they are and keep doing what they did.
//
// Picked from the Boring Mode lab as B16 (2026-11-11): striped rows, no hour
// dividers. Colours come from the theme: PURPLE is the frame, CYAN the text.
// A real catch still opens the full ALERT screen -- that is not this screen's
// job -- so the lab's catch window is not here.
//
// Sorting (sort lab E6, 2026-11-12): the LOG's headings are buttons in their
// own sunken strip. Tap one to sort by it, again to flip it; the one in charge
// is lit and drops into its column like a tab. TIME, newest at the bottom, is
// the live log; any other order says SORTED and names itself in the footer.
// The log is already one line per device (a repeat sighting updates its row),
// so each line shows how many times it came back (x5) instead.

#include "dex.h"

namespace {

// Darker steps of a theme colour that keep its hue on the 8-bit frame. Plain
// scaling turns a dark purple olive: the channels round down to different
// levels. The strong channels (at least 0.6 of the strongest) round UP and
// never to zero; the weak ones round down. The answer is an RGB565 whose top
// bits are exactly the RGB332 level, so the 8-bit sprite keeps it as is.
uint16_t tmShade(uint16_t c, uint8_t pct) {
    const int r5 = (c >> 11) & 31, g6 = (c >> 5) & 63, b5 = c & 31;
    const int n[3] = {7, 7, 3};
    const int L[3] = {(r5 * 7 + 15) / 31, (g6 * 7 + 31) / 63, (b5 * 3 + 15) / 31};
    int mx = 0;   // compared in 1/1000ths of each channel's own scale
    int f[3];
    for (int i = 0; i < 3; i++) { f[i] = L[i] * 1000 / n[i]; if (f[i] > mx) mx = f[i]; }
    if (!mx) return 0;
    int o[3];
    for (int i = 0; i < 3; i++) {
        const int s = f[i] * pct * n[i];                 // level x 100000
        if (f[i] * 10 >= mx * 6) {
            int v = (s + 99999) / 100000;
            o[i] = v < 1 ? 1 : (v > n[i] ? n[i] : v);
        } else {
            o[i] = s / 100000;
        }
    }
    const int R = (o[0] << 2) | (o[0] >> 1), G = (o[1] << 3) | o[1], B = (o[2] << 3) | (o[2] << 1) | (o[2] >> 1);
    return (uint16_t)((R << 11) | (G << 5) | B);
}

struct TmTheme { uint16_t HI, ACT, MID, LO, DOT, ZEB, FG, FGM, FGD; };
TmTheme tmTheme() {
    const uint16_t F = Theme::PURPLE, T = Theme::CYAN;
    return { tmShade(F, 100), tmShade(F, 75), tmShade(F, 50), tmShade(F, 30), tmShade(F, 22), tmShade(F, 14),
             tmShade(T, 100), tmShade(T, 62), tmShade(T, 38) };
}

struct TmRect { int16_t x, y, w, h; bool in(int px, int py) const { return w > 0 && px >= x && px < x + w && py >= y && py < y + h; } };

constexpr uint16_t TM_AMBER = 0xFD20;
constexpr uint8_t  TM_ROWS_MAX = 24;
constexpr uint8_t  TM_ROW_H = 10;
// The LOG's type and spacing. Font 1 in 10 px rows on the small panels; on a
// big one (the 3.5in, the Tab5 -- 480 px or more on its long side) font 2 in
// 18 px rows, with taller heading buttons and a wider scrollbar to match.
struct TmM { uint8_t f, rh, bh, sb; };
TmM      s_tmM = { 1, 10, 13, 14 };
uint8_t  s_tmRowH = 10;         // what the touch handler steps by

// What the last frame drew, for the touch handler.
bool     s_tmDrawn = false;
TmRect   s_tmWin[4];            // LOG, AIR, COUNTS, DETAILS (w 0 = not drawn)
TmRect   s_tmBody;              // the log's rows
TmRect   s_tmUp, s_tmDn, s_tmTrack, s_tmThumb, s_tmPill;
uint8_t  s_tmVis = 0, s_tmMax = 0, s_tmRowN = 0;
int16_t  s_tmRowY0 = 0;
uint8_t  s_tmRowMac[TM_ROWS_MAX][6];

// What the person did.
uint16_t s_tmScroll = 0;        // rows up from the newest
uint16_t s_tmNew = 0;           // arrived below while scrolled up
uint32_t s_tmLastTotal = 0;
bool     s_tmSelOn = false;
uint8_t  s_tmSel[6];
uint8_t  s_tmFocus = 0;         // which window's title is lit: 0 LOG
// The device DETAILS / LATEST is showing, and a request (a tap on that window,
// or on the picked row again) for its menu, which main.cpp takes.
bool     s_tmDetOn = false;
uint8_t  s_tmDetMac[6];
bool     s_tmOpenReq = false;
uint8_t  s_tmOpenMac[6];

// The order. TIME going forward is the live log (hangs from the newest, at the
// bottom); every other order reads from the top. Kept until a restart.
enum : uint8_t { TM_S_TIME, TM_S_KIND, TM_S_DEV, TM_S_RSSI };
constexpr uint8_t TM_LOG_MAX = 64;
uint8_t  s_tmSort = TM_S_TIME;
int8_t   s_tmDir = 1;
uint32_t s_tmSortAt = 0;        // when it last changed: the rows ripple back in
int8_t   s_tmPressCol = -1;     // the heading under the finger
bool     s_tmPressHeld = false;
uint32_t s_tmPressUntil = 0;    // ...and a moment after it lets go
int16_t  s_tmColX[5];           // TIME | KIND | DEVICE | dBm edges
bool tmLive() { return s_tmSort == TM_S_TIME && s_tmDir > 0; }

// AIR: the busiest second of each of the last sixty minutes.
uint8_t  s_tmAir[60];
uint8_t  s_tmAirHead = 0;
uint32_t s_tmAirMinute = 0xFFFFFFFF;

void tmText(TFT_eSPI& t, const char* s, int x, int y, uint16_t c, bool right = false, uint8_t font = 1) {
    t.setTextFont(font);
    t.setTextSize(1);
    t.setTextColor(c);
    if (right) x -= t.textWidth(s);
    t.setCursor(x, y);
    t.print(s);
}

void tmBevel(TFT_eSPI& t, int x, int y, int w, int h, bool up, const TmTheme& T) {
    const uint16_t a = up ? T.HI : T.LO, b = up ? T.LO : T.HI;
    t.drawFastHLine(x, y, w, a);
    t.drawFastVLine(x, y, h, a);
    t.drawFastHLine(x, y + h - 1, w, b);
    t.drawFastVLine(x + w - 1, y, h, b);
}

// A window: fine checkered shadow, bevelled frame, rounded corners, a
// two-tone title bar when it has focus. Returns the inside.
TmRect tmWindow(TFT_eSPI& t, int x, int y, int w, int h, const char* title, bool act, bool closeBox,
                const char* chip, uint16_t chipC, const TmTheme& T) {
    for (int yy = y + 3; yy < y + h + 3; yy++)
        for (int xx = x + w + ((x + w + yy) & 1); xx < x + w + 3; xx += 2) t.drawPixel(xx, yy, Theme::BLACK);
    for (int yy = y + h; yy < y + h + 3; yy++)
        for (int xx = x + 3 + ((x + 3 + yy) & 1); xx < x + w; xx += 2) t.drawPixel(xx, yy, Theme::BLACK);
    t.fillRect(x, y, w, h, Theme::BLACK);
    tmBevel(t, x, y, w, h, true, T);
    t.drawRect(x + 1, y + 1, w - 2, h - 2, act ? T.MID : T.LO);
    t.drawPixel(x, y, T.ZEB); t.drawPixel(x + w - 1, y, T.ZEB);
    t.drawPixel(x, y + h - 1, T.ZEB); t.drawPixel(x + w - 1, y + h - 1, T.ZEB);
    const int tx = x + 2, tw = w - 4, ty = y + 2;
    if (act) {
        t.fillRect(tx, ty, tw, 4, T.HI);
        t.fillRect(tx, ty + 5, tw, 5, T.ACT);
        for (int xx = tx; xx < tx + tw; xx++) t.drawPixel(xx, ty + 4, (xx & 1) ? T.ACT : T.HI);
    } else {
        t.fillRect(tx, ty, tw, 10, T.LO);
    }
    const uint16_t tc = act ? Theme::BLACK : T.FGM;
    tmText(t, title, tx + 3, ty + 1, tc);
    int cx = x + w - 11;
    if (closeBox) {
        t.drawLine(cx + 1, ty + 2, cx + 6, ty + 7, tc);
        t.drawLine(cx + 6, ty + 2, cx + 1, ty + 7, tc);
    }
    if (chip) {
        const int cw = (int)strlen(chip) * 6 + 6, chx = cx - 6 - cw + (closeBox ? 0 : 10);
        t.fillRect(chx, ty + 1, cw, 9, act ? Theme::BLACK : T.ZEB);
        tmText(t, chip, chx + 3, ty + 2, chipC);
    }
    return { (int16_t)(x + 3), (int16_t)(y + 13), (int16_t)(w - 6), (int16_t)(h - 16) };
}

void tmStamp(const Detection& d, char* out, size_t n) {
    if (d.restored) Clock::formatEpochStamp(d.firstSeen, out, n);
    else            Clock::formatStamp(d.firstSeen, out, n);
    out[5] = 0;
}

// The name a row goes by: what it called itself, else who made it, else the
// end of its address.
void tmName(const Detection& d, char* out, size_t n) {
    if (d.name[0])                snprintf(out, n, "%s", d.name);
    else if (vendorText(d)[0])    snprintf(out, n, "%s", vendorText(d));
    else                          snprintf(out, n, "%02X:%02X:%02X", d.mac[3], d.mac[4], d.mac[5]);
    for (char* p = out; *p; p++) if (*p >= 'a' && *p <= 'z') *p -= 32;
}

// Three bars standing on y + 7 (font 1) or y + 14 (font 2), twice the size on the big panels.
void tmBars(TFT_eSPI& t, int x, int y, int8_t rssi, uint16_t on, uint16_t off, int s = 1) {
    const int lv = rssi > -62 ? 3 : rssi > -74 ? 2 : 1, bw = s == 1 ? 2 : 3, base = y + 7 * s;
    for (int k = 0; k < 3; k++) { const int hh = (2 + k * 2) * s; t.fillRect(x + k * (bw + 1), base - hh, bw, hh, k < lv ? on : off); }
}

// The log in display order, top to bottom, as logAt() indices (0 = newest).
// Ties keep the time order. Sixty-four rows: an insertion sort is plenty.
void tmOrder(const DetectionEngine& eng, int n, uint8_t* ord) {
    for (int k = 0; k < n; k++) ord[k] = (uint8_t)(n - 1 - k);   // oldest first
    if (s_tmSort == TM_S_TIME) {
        if (s_tmDir < 0) for (int k = 0; k < n; k++) ord[k] = (uint8_t)k;
        return;
    }
    char nm[TM_LOG_MAX][10];
    if (s_tmSort == TM_S_DEV)
        for (int i = 0; i < n; i++) {
            char b[24] = "";
            if (const Detection* d = eng.logAt((uint8_t)i)) tmName(*d, b, sizeof b);
            snprintf(nm[i], sizeof nm[i], "%s", b);
        }
    auto before = [&](uint8_t a, uint8_t b) {
        const Detection* A = eng.logAt(a);
        const Detection* B = eng.logAt(b);
        if (!A || !B) return false;
        int c;
        if (s_tmSort == TM_S_KIND)     c = (int)A->type - (int)B->type;
        else if (s_tmSort == TM_S_DEV) c = strcmp(nm[a], nm[b]);
        else                           c = (int)B->rssi - (int)A->rssi;   // strongest first
        return s_tmDir > 0 ? c < 0 : c > 0;
    };
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0 && before(ord[j], ord[j - 1]); j--) { const uint8_t x = ord[j]; ord[j] = ord[j - 1]; ord[j - 1] = x; }
}

void tmLog(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng, TmRect r, const TmTheme& T) {
    const TmM M = s_tmM;
    const int f = M.f, RH = M.rh, SB = M.sb, foot = RH + 1, k2 = f == 1 ? 1 : 2;
    const int usable = r.w - SB - 1, right = r.x + usable - 2, top = M.bh + 5;
    const bool live = tmLive();

    // The columns, measured in the font they are drawn in. On font 1 these come
    // out at the 2.8in's 33 / 38 / 85 / right - 30.
    t.setTextFont(f);
    t.setTextSize(1);
    int timeW = t.textWidth("00:00");
    if (f != 1 && t.textWidth("TIME") + 9 > timeW) timeW = t.textWidth("TIME") + 9;
    timeW += 3;
    const int badgeX = r.x + timeW, kindX = badgeX + 5;
    const int nameX = kindX + t.textWidth("TRACKER") + 5;
    const int barsW = 3 * ((k2 == 1 ? 2 : 3) + 1) - 1, rssiR = right - barsW - 3;
    int colR = rssiR - t.textWidth("-99") - 1;
    if (f != 1 && r.x + usable - (t.textWidth("dBm") + 14) < colR) colR = r.x + usable - (t.textWidth("dBm") + 14);
    const int ex[5] = { r.x, r.x + timeW + 2, nameX, colR, r.x + usable };
    for (int i = 0; i < 5; i++) s_tmColX[i] = (int16_t)ex[i];

    // The headings: four buttons in a sunken strip, a 2 px gap between them.
    t.fillRect(r.x, r.y - 1, usable, M.bh + 5, Theme::BLACK);
    tmBevel(t, r.x, r.y - 1, usable, M.bh + 4, false, T);
    static const char* const LBL[4] = { "TIME", "KIND", "DEVICE", "dBm" };
    for (int k = 0; k < 4; k++) {
        const bool on  = s_tmSort == k;
        const bool pr  = s_tmPressCol == k && (s_tmPressHeld || (int32_t)(s_tmPressUntil - now) > 0);
        const bool tab = on && !pr;                     // lit, and joined to its column
        const int bx = ex[k] + 1, bw = ex[k + 1] - ex[k] - 2, by = r.y + 1, bh = tab ? M.bh + 4 : M.bh, p = pr ? 1 : 0;
        t.fillRect(bx, by, bw, bh, on ? T.HI : T.ZEB);
        if (!on) t.fillRect(bx + 1, by + 1, bw - 2, M.bh / 2 - 1, T.DOT);   // two-tone, like the title bars
        if (tab) {
            t.drawFastHLine(bx, by, bw, (uint16_t)0xFFFF);
            t.drawFastVLine(bx, by, bh, (uint16_t)0xFFFF);
            t.drawFastVLine(bx + bw - 1, by + 1, bh - 1, T.ACT);
        } else {
            tmBevel(t, bx, by, bw, bh, !pr, T);
        }
        t.drawPixel(bx, by, Theme::BLACK);
        t.drawPixel(bx + bw - 1, by, Theme::BLACK);
        if (!tab) { t.drawPixel(bx, by + bh - 1, Theme::BLACK); t.drawPixel(bx + bw - 1, by + bh - 1, Theme::BLACK); }
        const bool rs = k == TM_S_RSSI;
        tmText(t, LBL[k], (rs ? bx + bw - 3 : bx + 3) + p, by + 3 + p, on ? Theme::BLACK : T.FG, rs, f);
        // The sort mark: up-and-down on the idle ones, one solid arrow on the lit one.
        const int cx = (rs ? bx + 5 : bx + bw - 5) + p, cy = by + (M.bh - 7) / 2 + p;
        if (on) {
            for (int i = 0; i < 3; i++) t.drawFastHLine(cx - i, s_tmDir < 0 ? cy + 1 + i : cy + 3 - i, 2 * i + 1, Theme::BLACK);
        } else {
            t.drawPixel(cx, cy, T.MID);
            t.drawFastHLine(cx - 1, cy + 1, 3, T.MID);
            t.drawFastHLine(cx - 1, cy + 5, 3, T.MID);
            t.drawPixel(cx, cy + 6, T.MID);
        }
    }

    const int n = eng.logCount() > TM_LOG_MAX ? TM_LOG_MAX : eng.logCount();
    uint8_t ord[TM_LOG_MAX];
    tmOrder(eng, n, ord);
    int vis = (r.h - top + 1 - foot) / RH;
    if (vis > TM_ROWS_MAX) vis = TM_ROWS_MAX;
    if (vis < 1) vis = 1;
    const int max = n > vis ? n - vis : 0;
    if (s_tmScroll > max) s_tmScroll = (uint16_t)max;
    if (!s_tmScroll) s_tmNew = 0;
    const int off = s_tmScroll;
    // Live: rows up from the newest at the bottom. Sorted: rows down from the top.
    const int start = live ? (n - vis - off > 0 ? n - vis - off : 0) : off;
    const int shown = live ? (n - off) - start : (n - start < vis ? n - start : vis);
    s_tmVis = (uint8_t)vis; s_tmMax = (uint8_t)(max > 255 ? 255 : max);
    s_tmBody = { r.x, (int16_t)(r.y + top - 1), (int16_t)usable, (int16_t)(r.h - top + 1 - foot) };
    s_tmRowY0 = (int16_t)(r.y + top);
    s_tmRowH = (uint8_t)RH;
    s_tmRowN = 0;

    const int b0 = ex[s_tmSort], b1 = ex[s_tmSort + 1];
    for (int j = 0; j < shown; j++) {
        const int y = r.y + top + j * RH;
        const Detection* d = eng.logAt(ord[start + j]);
        if (!d) continue;
        if (j & 1) t.fillRect(r.x, y - 1, usable, RH, T.ZEB);
        t.fillRect(b0, y - 1, b1 - b0, RH, (j & 1) ? T.DOT : T.ZEB);   // the sorted column's band
        if (s_tmRowN < TM_ROWS_MAX) memcpy(s_tmRowMac[s_tmRowN++], d->mac, 6);
        // Just re-sorted: the rows come back in from the top, 28 ms apart.
        if (s_tmSortAt && now - s_tmSortAt < (uint32_t)j * 28) continue;
        const bool sel = s_tmSelOn && !memcmp(d->mac, s_tmSel, 6);
        if (sel) t.fillRect(r.x, y - 1, usable, RH, T.HI);
        uint16_t fg = d->active ? T.FG : T.FGM, fm = T.FGM;
        if (IgnoreList::silenced(d->mac)) fg = T.FGD;
        if (!d->restored && now - d->firstSeen < 2000) { fg = now - d->firstSeen < 1000 ? T.FGD : T.FGM; fm = T.FGD; }
        const uint16_t kc = sel ? Theme::BLACK : Theme::colorFor(d->type);
        if (sel) { fg = Theme::BLACK; fm = Theme::BLACK; }
        char buf[24];
        tmStamp(*d, buf, sizeof buf);
        tmText(t, buf, r.x + 1, y, fm, false, f);
        t.fillRect(badgeX, y + (k2 - 1) * 2, 3, 7 * k2 - (k2 - 1) * 2, kc);
        tmText(t, Dex::shortName(d->type), kindX, y, kc, false, f);
        // The name, cut to fit, and how many times it came back.
        tmName(*d, buf, sizeof buf);
        char cnt[6] = "";
        if (d->hits > 1) snprintf(cnt, sizeof cnt, "x%u", (unsigned)(d->hits > 999 ? 999 : d->hits));
        t.setTextFont(f);
        int room = colR - 4 - nameX - (cnt[0] ? t.textWidth(cnt) + 6 : 0);
        for (int L = (int)strlen(buf); L > 1 && t.textWidth(buf) > room; ) buf[--L] = 0;
        tmText(t, buf, nameX, y, fg, false, f);
        if (cnt[0]) { t.setTextFont(f); tmText(t, cnt, nameX + t.textWidth(buf) + 6, y, sel ? Theme::BLACK : T.FGD, false, f); }
        tmBars(t, right - barsW, y, d->rssi, sel ? Theme::BLACK : fm, sel ? T.MID : T.LO, k2);
        snprintf(buf, sizeof buf, "%d", (int)d->rssi);
        tmText(t, buf, rssiR, y, fg, true, f);
    }
    if (live && !off) {
        const int cy = r.y + top + shown * RH;
        if (cy + RH - 2 <= r.y + r.h - foot) {
            if ((now / 500) & 1) t.fillRect(r.x + 1, cy, 6 * k2 - (k2 - 1) * 4, RH - 2, T.FG);
            if (!n) tmText(t, "all quiet, still listening", r.x + 6 * k2 + 6, cy, T.FGD, false, f);
        }
    }

    // The scrollbar: arrows, a dotted track, a bevelled thumb with grip lines.
    const int sx = r.x + r.w - SB, sy = r.y, sh = r.h - foot, ah = SB + 1;
    t.drawFastVLine(sx - 1, sy, sh, T.LO);
    t.fillRect(sx, sy, SB, sh, Theme::BLACK);
    for (int yy = sy + ah + 1; yy < sy + sh - ah - 1; yy += 2)
        for (int xx = sx + (((yy >> 1) & 1) ? 1 : 3); xx < sx + SB; xx += 4) t.drawPixel(xx, yy, T.ZEB);
    for (int a = 0; a < 2; a++) {
        const int y0 = a ? sy + sh - ah : sy;
        tmBevel(t, sx, y0, SB, ah, true, T);
        const int cx = sx + SB / 2, cy = y0 + ah / 2, A = SB / 4 + 1;
        for (int k = 0; k < A; k++) t.drawFastHLine(cx - k, a ? cy + A / 2 - k : cy - A / 2 + k, 2 * k + 1, T.FG);
    }
    const int track = sh - 2 * ah - 2;
    const int th = max ? (track * vis / n > 10 ? track * vis / n : 10) : track;
    const int tyy = sy + ah + 1 + (max ? (track - th) * (live ? max - off : off) / max : 0);
    t.fillRect(sx + 1, tyy, SB - 2, th, T.ZEB);
    tmBevel(t, sx + 1, tyy, SB - 2, th, true, T);
    for (int k = -1; k <= 1; k++) t.drawFastHLine(sx + 4, tyy + th / 2 + k * 2, SB - 8, T.MID);
    s_tmUp    = { (int16_t)sx, (int16_t)sy, (int16_t)SB, (int16_t)ah };
    s_tmDn    = { (int16_t)sx, (int16_t)(sy + sh - ah), (int16_t)SB, (int16_t)ah };
    s_tmTrack = { (int16_t)sx, (int16_t)(sy + ah), (int16_t)SB, (int16_t)(sh - 2 * ah) };
    s_tmThumb = { (int16_t)sx, (int16_t)tyy, (int16_t)SB, (int16_t)th };

    // Scrolled up in the live log: a pill back to the newest, counting what arrived below.
    s_tmPill = { 0, 0, 0, 0 };
    if (live && off > 0) {
        char label[12];
        if (s_tmNew) snprintf(label, sizeof label, "%u NEW", (unsigned)(s_tmNew > 999 ? 999 : s_tmNew));
        else         snprintf(label, sizeof label, "NOW");
        const int pw = (int)strlen(label) * 6 + 22, px = r.x + (usable - pw) / 2, py = r.y + r.h - foot - 17;
        t.fillRect(px + 2, py + 2, pw, 14, Theme::BLACK);
        t.fillRect(px, py, pw, 14, T.ACT);
        t.drawRect(px, py, pw, 14, T.HI);
        for (int k = 0; k < 4; k++) t.drawFastHLine(px + 5 + k, py + 4 + k, 7 - 2 * k, Theme::BLACK);
        tmText(t, label, px + 15, py + 3, Theme::BLACK);
        s_tmPill = { (int16_t)(px - 6), (int16_t)(py - 6), (int16_t)(pw + 12), 26 };
    }

    // Which lines these are; live or paused, or what the order means in words.
    const int fy = r.y + r.h - foot + 1;
    t.drawFastHLine(r.x, fy - 1, r.w, T.LO);
    char st[24];
    snprintf(st, sizeof st, "%d-%d OF %d", n ? start + 1 : 0, start + shown, n);
    tmText(t, st, r.x + 1, fy + 1, T.FGM, false, f);
    if (!live) {
        static const char* const W[4][2] = { { "BY TIME", "NEWEST UP" }, { "BY KIND", "BY KIND" },
                                             { "A-Z", "Z-A" }, { "CLOSEST UP", "FARTHEST UP" } };
        tmText(t, W[s_tmSort][s_tmDir > 0 ? 0 : 1], r.x + r.w - 1, fy + 1, T.FG, true, f);
    } else if (off > 0) {
        tmText(t, "PAUSED", r.x + r.w - 1, fy + 1, TM_AMBER, true, f);
    } else {
        t.setTextFont(f);
        t.fillCircle(r.x + r.w - t.textWidth("LIVE") - 6, fy + RH / 2 - 1, 2, T.FG);
        tmText(t, "LIVE", r.x + r.w - 1, fy + 1, T.FGM, true, f);
    }
}

void tmAir(TFT_eSPI& t, TmRect r, const TmTheme& T) {
    const int gy = r.y + r.h - 10, top = r.y + 1, gh = gy - top;
    if (gh < 4) return;
    for (int x = r.x; x < r.x + r.w; x += 4) t.drawPixel(x, top + gh / 2, T.LO);
    int mx = 1;
    for (int i = 0; i < 60; i++) if (s_tmAir[i] > mx) mx = s_tmAir[i];
    mx += mx / 20 + 1;
    const int cols = r.w < 60 ? r.w : 60, bx = r.x + (r.w - cols) / 2;
    int py = -1;
    for (int i = 0; i < cols; i++) {
        const uint8_t v = s_tmAir[(s_tmAirHead + 1 + (60 - cols) + i) % 60];
        const int hh = v * gh / mx > 0 ? v * gh / mx : 1, x = bx + i, yy = gy - hh;
        t.drawFastVLine(x, yy, hh, T.LO);
        if (py >= 0) t.drawLine(x - 1, py, x, yy, T.FG);
        py = yy;
    }
    if (py >= 0) t.fillRect(bx + cols - 2, py - 1, 3, 3, (uint16_t)0xFFFF);
    t.drawFastHLine(r.x, gy, r.w, T.LO);
    tmText(t, "-1H", r.x + 1, gy + 2, T.FGD);
    tmText(t, "NOW", r.x + r.w - 1, gy + 2, T.FGD, true);
}

void tmCounts(TFT_eSPI& t, const DetectionEngine& eng, TmRect r, const TmTheme& T) {
    DetectionType cols[MAX_COUNTER_TYPES];
    const uint8_t nc = activeCounterTypes(cols);
    uint16_t cnt[MAX_COUNTER_TYPES] = {};
    for (uint8_t i = 0; i < eng.logCount(); i++) {
        const Detection* d = eng.logAt(i);
        if (!d) continue;
        for (uint8_t c = 0; c < nc; c++) if (uiClearColumnHolds(cols[c], d->type)) { cnt[c]++; break; }
    }
    // Busiest first; the empty kinds fill whatever room is left, dim.
    uint8_t ord[MAX_COUNTER_TYPES];
    for (uint8_t i = 0; i < nc; i++) ord[i] = i;
    for (uint8_t i = 1; i < nc; i++)
        for (uint8_t j = i; j > 0 && cnt[ord[j]] > cnt[ord[j - 1]]; j--) { const uint8_t s = ord[j]; ord[j] = ord[j - 1]; ord[j - 1] = s; }
    char b[8];
    int y = r.y + 1;
    tmText(t, "ALL", r.x + 1, y, T.FGD);
    snprintf(b, sizeof b, "%u", (unsigned)eng.logCount());
    tmText(t, b, r.x + r.w - 1, y, T.FG, true);
    y += 10;
    t.drawFastHLine(r.x, y, r.w, T.LO);
    y += 3;
    for (uint8_t k = 0; k < nc && y + 8 <= r.y + r.h; k++) {
        const uint8_t i = ord[k];
        if (k & 1) t.fillRect(r.x, y - 1, r.w, 9, T.ZEB);
        tmText(t, counterLabel(cols[i]), r.x + 1, y, cnt[i] ? Theme::colorFor(cols[i]) : T.LO);
        snprintf(b, sizeof b, "%u", (unsigned)cnt[i]);
        tmText(t, b, r.x + r.w - 1, y, cnt[i] ? T.FG : T.LO, true);
        y += 9;
    }
}

const Detection* tmPicked(const DetectionEngine& eng) {
    if (s_tmSelOn)
        for (uint8_t i = 0; i < eng.logCount(); i++) {
            const Detection* d = eng.logAt(i);
            if (d && !memcmp(d->mac, s_tmSel, 6)) return d;
        }
    s_tmSelOn = false;
    return eng.logCount() ? eng.logAt(0) : nullptr;
}

void tmDetails(TFT_eSPI& t, uint32_t now, const Detection* d, TmRect r, const TmTheme& T) {
    s_tmDetOn = d != nullptr;
    if (!d) { tmText(t, "NOTHING YET", r.x + 1, r.y + 1, T.FGD); return; }
    memcpy(s_tmDetMac, d->mac, 6);
    char b[24];
    tmName(*d, b, sizeof b);
    const int nmax = r.w / 6;
    if (nmax > 0 && nmax < (int)sizeof b) b[nmax] = 0;
    tmText(t, b, r.x + 1, r.y + 1, T.FG);
    tmText(t, Dex::shortName(d->type), r.x + 1, r.y + 11, Theme::colorFor(d->type));
    snprintf(b, sizeof b, "%d", (int)d->rssi);
    tmText(t, b, r.x + r.w - 1, r.y + 11, T.FGM, true);
    tmText(t, "SEEN", r.x + 1, r.y + 21, T.FGD);
    snprintf(b, sizeof b, "%uX", (unsigned)d->hits);
    tmText(t, b, r.x + r.w - 1, r.y + 21, T.FG, true);
    tmText(t, "FIRST", r.x + 1, r.y + 31, T.FGD);
    tmStamp(*d, b, sizeof b);
    tmText(t, b, r.x + r.w - 1, r.y + 31, T.FG, true);
    if (r.h >= 50) {
        tmText(t, "LAST", r.x + 1, r.y + 41, T.FGD);
        if (d->active)                 snprintf(b, sizeof b, "NOW");
        else if (d->restored || !d->lastSeen) snprintf(b, sizeof b, "--");
        else {
            const uint32_t s = (now - d->lastSeen) / 1000;
            if (s < 60)        snprintf(b, sizeof b, "%lus", (unsigned long)s);
            else if (s < 3600) snprintf(b, sizeof b, "%lum", (unsigned long)(s / 60));
            else               snprintf(b, sizeof b, "%luh", (unsigned long)(s / 3600));
        }
        tmText(t, b, r.x + r.w - 1, r.y + 41, d->active ? T.FG : T.FGM, true);
    }
    // Signal: ten segments from -100 to -40 dBm.
    const int sy = r.y + r.h - 8;
    if (sy > r.y + 50) {
        int v = (d->rssi + 100) * 10 / 60;
        if (v < 0) v = 0;
        if (v > 10) v = 10;
        const int sw = (r.w - 2) / 10;
        for (int i = 0; i < 10; i++) t.fillRect(r.x + 1 + i * sw, sy, sw - 2, 7, i < v ? (i >= 8 ? TM_AMBER : T.FG) : T.ZEB);
    }
}

}  // namespace

// The whole terminal, into rows top..bottom of the panel. Paints the desktop
// over the whole screen first: the title bar and the buttons go on top.
void uiTerminalDraw(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng, int top, int bottom) {
    const int w = t.width(), h = t.height();
    const TmTheme T = tmTheme();

    // AIR's history and the new-arrivals count are kept here, every frame.
    const uint32_t minute = now / 60000;
    if (minute != s_tmAirMinute) {
        if (s_tmAirMinute != 0xFFFFFFFF) { s_tmAirHead = (uint8_t)((s_tmAirHead + 1) % 60); s_tmAir[s_tmAirHead] = 0; }
        s_tmAirMinute = minute;
    }
    const uint32_t rate = advertRate();
    if (rate > s_tmAir[s_tmAirHead]) s_tmAir[s_tmAirHead] = (uint8_t)(rate > 255 ? 255 : rate);
    const uint32_t total = eng.lifetimeTotal();
    if (total > s_tmLastTotal && s_tmScroll > 0 && tmLive()) {
        s_tmScroll += (uint16_t)(total - s_tmLastTotal);
        s_tmNew    += (uint16_t)(total - s_tmLastTotal);
    }
    s_tmLastTotal = total;

    // The desktop: faint graph paper.
    t.fillRect(0, 0, w, h, T.ZEB);
    for (int x = 4; x < w; x += 8) t.drawFastVLine(x, 0, h, T.DOT);
    for (int y = 4; y < h; y += 8) t.drawFastHLine(0, y, w, T.DOT);

    for (auto& r : s_tmWin) r = { 0, 0, 0, 0 };
    const int H = bottom - top;
    const bool side  = w >= 300 && w > h && H >= 150;
    const bool below = !side && h > w && H >= 220;     // upright: the same windows, under the LOG
    const bool big   = (w > h ? w : h) >= 480;
    s_tmM = big ? TmM{ 2, 18, 21, 20 } : TmM{ 1, 10, 13, 14 };
    const int sideW = side ? (w >= 400 ? 120 : 90) : 0;
    const int logW  = side ? w - sideW - 8 : w - 4;
    const int botH  = below ? (H * 34 / 100 < 96 ? 96 : H * 34 / 100) : 0;
    const int logH  = below ? H - botH - 4 : H;

    char chip[8];
    snprintf(chip, sizeof chip, "%s", !tmLive() ? "SORTED" : s_tmScroll ? "PAUSED" : "LIVE");
    TmRect lr = tmWindow(t, 2, top, logW, logH, "LOG", s_tmFocus == 0, false, chip,
                         tmLive() && s_tmScroll ? TM_AMBER : T.FG, T);
    s_tmWin[0] = { 2, (int16_t)top, (int16_t)logW, (int16_t)logH };
    tmLog(t, now, eng, lr, T);

    // AIR, COUNTS and LATEST: down the right side, or in a row along the bottom.
    TmRect wa{}, wc{}, wd{};
    if (side) {
        const int sx = w - sideW - 4;
        const int airH = H * 24 / 100, cntH = H * 42 / 100, detH = H - airH - cntH - 8;
        wa = { (int16_t)sx, (int16_t)top, (int16_t)sideW, (int16_t)airH };
        wc = { (int16_t)sx, (int16_t)(top + airH + 4), (int16_t)sideW, (int16_t)cntH };
        wd = { (int16_t)sx, (int16_t)(top + airH + cntH + 8), (int16_t)sideW, (int16_t)detH };
    } else if (below) {
        // LATEST gets the most room: its name and its OPEN tag share a title bar.
        const int by = top + logH + 4, avail = w - 4 - 8, aw = avail * 30 / 100, cw = aw, dw = avail - aw - cw;
        wa = { 2, (int16_t)by, (int16_t)aw, (int16_t)botH };
        wc = { (int16_t)(2 + aw + 4), (int16_t)by, (int16_t)cw, (int16_t)botH };
        wd = { (int16_t)(2 + aw + cw + 8), (int16_t)by, (int16_t)dw, (int16_t)botH };
    }
    if (wa.w) {
        char title[16];
        snprintf(title, sizeof title, "AIR %lu/S", (unsigned long)rate);
        const bool close = wa.w >= 90;
        if ((int)strlen(title) * 6 + 8 > wa.w - (close ? 14 : 4)) snprintf(title, sizeof title, "AIR");
        TmRect a = tmWindow(t, wa.x, wa.y, wa.w, wa.h, title, s_tmFocus == 1, close, nullptr, 0, T);
        s_tmWin[1] = wa;
        tmAir(t, a, T);
        TmRect c = tmWindow(t, wc.x, wc.y, wc.w, wc.h, "COUNTS", s_tmFocus == 2, wc.w >= 90, nullptr, 0, T);
        s_tmWin[2] = wc;
        tmCounts(t, eng, c, T);
        const Detection* d = tmPicked(eng);
        // OPEN in its title bar: a tap anywhere on this window brings up the
        // device's menu (WATCH / HUNT / INFO / IGNORE).
        TmRect dr = tmWindow(t, wd.x, wd.y, wd.w, wd.h, s_tmSelOn ? "DETAILS" : "LATEST", s_tmFocus == 3, !d,
                             d ? "OPEN" : nullptr, T.FG, T);
        s_tmWin[3] = wd;
        tmDetails(t, now, d, dr, T);
    }
    s_tmDrawn = true;
}

// Touches that land on a window. Claims the whole gesture from a press inside
// one: drag the log to scroll it, tap a row to pick it (again to let go), tap
// the arrows / track / NOW pill, tap any window to bring its title up.
bool uiTerminalTouch(int x, int y, bool valid, bool justDown, uint32_t now) {
    static bool own = false, dragged = false, done = false, inBody = false, onDet = false, onHdr = false;
    static int  startY = 0, lastX = 0, lastY = 0;
    static int  startScroll = 0;
    auto setScroll = [](int v) {
        if (v < 0) v = 0;
        if (v > s_tmMax) v = s_tmMax;
        s_tmScroll = (uint16_t)v;
        if (!v) s_tmNew = 0;
    };
    if (justDown && valid) {
        own = false;
        if (!s_tmDrawn || !Settings::boringMode()) return false;
        int which = -1;
        for (int i = 0; i < 4; i++) if (s_tmWin[i].in(x, y)) { which = i; break; }
        if (which < 0) return false;
        own = true; dragged = false; done = false; onHdr = false;
        startY = lastY = y; lastX = x; startScroll = s_tmScroll;
        s_tmFocus = (uint8_t)which;
        inBody = which == 0 && s_tmBody.in(x, y);
        onDet  = which == 3 && s_tmDetOn;
        if (which == 0) {
            const int page = s_tmVis > 1 ? s_tmVis - 1 : 1;
            const int up = tmLive() ? 1 : -1;   // live counts up from the bottom, sorted down from the top
            // The headings, and the title bar above them, pick a column.
            if (y < s_tmBody.y && x < s_tmColX[4]) {
                for (int k = 0; k < 4; k++)
                    if (x >= s_tmColX[k] && x < s_tmColX[k + 1]) { s_tmPressCol = (int8_t)k; s_tmPressHeld = true; onHdr = true; }
                done = true;
            }
            else if (s_tmPill.in(x, y))  { setScroll(0); done = true; inBody = false; }
            else if (s_tmUp.in(x, y))    { setScroll(s_tmScroll + up); done = true; }
            else if (s_tmDn.in(x, y))    { setScroll(s_tmScroll - up); done = true; }
            else if (s_tmTrack.in(x, y) && !s_tmThumb.in(x, y)) {
                setScroll(y < s_tmThumb.y ? s_tmScroll + up * page : s_tmScroll - up * page); done = true;
            }
        }
        return true;
    }
    if (!own) return false;
    if (valid) {
        lastX = x; lastY = y;
        if (inBody) {
            const int dy = y - startY;
            if (!dragged && (dy > 8 || dy < -8)) dragged = true;
            if (dragged) setScroll(startScroll + (tmLive() ? dy : -dy) / s_tmRowH);
        }
        return true;
    }
    // Let go: a tap that did not drag picks the row under it; the picked
    // row tapped again, or the DETAILS window, asks for the device's menu;
    // the empty space under the rows lets the pick go.
    own = false;
    if (onHdr) {
        // Let go of a heading: sort by it, or flip it if it is already in charge.
        s_tmPressHeld = false;
        s_tmPressUntil = now + 180;
        const uint8_t k = (uint8_t)s_tmPressCol;
        if (s_tmSort == k) s_tmDir = (int8_t)-s_tmDir;
        else { s_tmSort = k; s_tmDir = 1; }
        s_tmScroll = 0; s_tmNew = 0;
        s_tmSortAt = now ? now : 1;
        return true;
    }
    if (!dragged && !done && onDet && s_tmWin[3].in(lastX, lastY)) {
        memcpy(s_tmOpenMac, s_tmDetMac, 6);
        s_tmOpenReq = true;
    } else if (!dragged && !done && inBody) {
        const int j = (lastY - s_tmRowY0 + 1) / s_tmRowH;
        if (lastY >= s_tmRowY0 - 1 && j >= 0 && j < s_tmRowN) {
            if (s_tmSelOn && !memcmp(s_tmSel, s_tmRowMac[j], 6)) {
                memcpy(s_tmOpenMac, s_tmSel, 6);
                s_tmOpenReq = true;
            } else {
                memcpy(s_tmSel, s_tmRowMac[j], 6);
                s_tmSelOn = true;
            }
        } else if (j >= s_tmRowN) {
            s_tmSelOn = false;
        }
    }
    return true;
}

// Where the StickS3 / Cardputer cursor stops on the terminal (they have no
// touch panel), as boxes for the cursor to light whole: the four headings,
// each row on screen, the scroll arrows, and LATEST when it has a device to
// open. Empty when the terminal is not up.
uint8_t uiTerminalStops(int16_t* rx, int16_t* ry, int16_t* rw, int16_t* rh, uint8_t cap) {
    uint8_t n = 0;
    if (!s_tmDrawn || !Settings::boringMode()) return 0;
    auto add = [&](int x, int y, int w, int h) {
        if (n < cap && w > 0 && h > 0) { rx[n] = (int16_t)x; ry[n] = (int16_t)y; rw[n] = (int16_t)w; rh[n] = (int16_t)h; n++; }
    };
    const int hy = s_tmBody.y - s_tmM.bh - 3;
    for (int k = 0; k < 4; k++) add(s_tmColX[k] + 1, hy, s_tmColX[k + 1] - s_tmColX[k] - 2, s_tmM.bh);
    for (int j = 0; j < s_tmRowN; j++) add(s_tmBody.x, s_tmRowY0 + j * s_tmRowH - 1, s_tmBody.w, s_tmRowH);
    add(s_tmUp.x, s_tmUp.y, s_tmUp.w, s_tmUp.h);
    add(s_tmDn.x, s_tmDn.y, s_tmDn.w, s_tmDn.h);
    if (s_tmDetOn) add(s_tmWin[3].x, s_tmWin[3].y, s_tmWin[3].w, s_tmWin[3].h);
    return n;
}

void uiTerminalSetSort(uint8_t col, int8_t dir) {
    s_tmSort = col > TM_S_RSSI ? TM_S_TIME : col;
    s_tmDir = dir < 0 ? -1 : 1;
    s_tmScroll = 0;
}

bool uiTerminalTakeOpen(uint8_t mac[6]) {
    if (!s_tmOpenReq) return false;
    s_tmOpenReq = false;
    memcpy(mac, s_tmOpenMac, 6);
    return true;
}
