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

// AIR: the busiest second of each of the last sixty minutes.
uint8_t  s_tmAir[60];
uint8_t  s_tmAirHead = 0;
uint32_t s_tmAirMinute = 0xFFFFFFFF;

void tmText(TFT_eSPI& t, const char* s, int x, int y, uint16_t c, uint8_t datum = TL_DATUM) {
    t.setTextDatum(datum);
    t.setTextColor(c);
    t.drawString(s, x, y, 1);
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

void tmBars(TFT_eSPI& t, int x, int y, int8_t rssi, uint16_t on, uint16_t off) {
    const int lv = rssi > -62 ? 3 : rssi > -74 ? 2 : 1;
    for (int k = 0; k < 3; k++) t.fillRect(x + k * 3, y + 5 - k * 2, 2, 2 + k * 2, k < lv ? on : off);
}

void tmLog(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng, TmRect r, const TmTheme& T) {
    const int SB = 14, foot = 11, usable = r.w - SB - 1, right = r.x + usable - 2;
    tmText(t, "TIME  KIND    DEVICE", r.x + 1, r.y + 1, T.FGD);
    tmText(t, "dBm", right, r.y + 1, T.FGD, TR_DATUM);
    t.drawFastHLine(r.x, r.y + 10, usable, T.LO);

    const int n = eng.logCount();
    int vis = (r.h - 12 - foot) / TM_ROW_H;
    if (vis > TM_ROWS_MAX) vis = TM_ROWS_MAX;
    if (vis < 1) vis = 1;
    const int max = n > vis ? n - vis : 0;
    if (s_tmScroll > max) s_tmScroll = (uint16_t)max;
    if (!s_tmScroll) s_tmNew = 0;
    const int off = s_tmScroll;
    const int start = n - vis - off > 0 ? n - vis - off : 0;
    const int shown = (n - off) - start;
    s_tmVis = (uint8_t)vis; s_tmMax = (uint8_t)(max > 255 ? 255 : max);
    s_tmBody = { r.x, (int16_t)(r.y + 12), (int16_t)usable, (int16_t)(r.h - 12 - foot) };
    s_tmRowY0 = (int16_t)(r.y + 13);
    s_tmRowN = 0;

    const int nameX = r.x + 85, rssiR = right - 11;
    const int nameMax = (rssiR - 22 - nameX) / 6;
    for (int j = 0; j < shown; j++) {
        const int y = r.y + 13 + j * TM_ROW_H;
        const Detection* d = eng.logAt((uint8_t)(n - 1 - (start + j)));
        if (!d) continue;
        if (j & 1) t.fillRect(r.x, y - 1, usable, TM_ROW_H, T.ZEB);
        const bool sel = s_tmSelOn && !memcmp(d->mac, s_tmSel, 6);
        if (sel) t.fillRect(r.x, y - 1, usable, TM_ROW_H, T.HI);
        uint16_t fg = d->active ? T.FG : T.FGM, fm = T.FGM;
        if (IgnoreList::silenced(d->mac)) fg = T.FGD;
        if (!d->restored && now - d->firstSeen < 2000) { fg = now - d->firstSeen < 1000 ? T.FGD : T.FGM; fm = T.FGD; }
        const uint16_t kc = sel ? Theme::BLACK : Theme::colorFor(d->type);
        if (sel) { fg = Theme::BLACK; fm = Theme::BLACK; }
        char buf[24];
        tmStamp(*d, buf, sizeof buf);
        tmText(t, buf, r.x + 1, y, fm);
        t.fillRect(r.x + 33, y, 3, 7, kc);
        tmText(t, Dex::shortName(d->type), r.x + 38, y, kc);
        tmName(*d, buf, sizeof buf);
        if (nameMax > 0 && nameMax < (int)sizeof buf) buf[nameMax] = 0;
        tmText(t, buf, nameX, y, fg);
        tmBars(t, right - 8, y, d->rssi, sel ? Theme::BLACK : fm, sel ? T.MID : T.LO);
        snprintf(buf, sizeof buf, "%d", (int)d->rssi);
        tmText(t, buf, rssiR, y, fg, TR_DATUM);
        if (s_tmRowN < TM_ROWS_MAX) memcpy(s_tmRowMac[s_tmRowN++], d->mac, 6);
    }
    if (!off) {
        const int cy = r.y + 13 + shown * TM_ROW_H;
        if (cy + 8 <= r.y + r.h - foot) {
            if ((now / 500) & 1) t.fillRect(r.x + 1, cy, 6, 8, T.FG);
            if (!n) tmText(t, "all quiet, still listening", r.x + 12, cy, T.FGD);
        }
    }

    // The scrollbar: arrows, a dotted track, a bevelled thumb with grip lines.
    const int sx = r.x + r.w - SB, sy = r.y, sh = r.h - foot;
    t.drawFastVLine(sx - 1, sy, sh, T.LO);
    t.fillRect(sx, sy, SB, sh, Theme::BLACK);
    for (int yy = sy + 16; yy < sy + sh - 16; yy += 2)
        for (int xx = sx + (((yy >> 1) & 1) ? 1 : 3); xx < sx + SB; xx += 4) t.drawPixel(xx, yy, T.ZEB);
    for (int a = 0; a < 2; a++) {
        const int y0 = a ? sy + sh - 15 : sy;
        tmBevel(t, sx, y0, SB, 15, true, T);
        const int cx = sx + 7, cy = y0 + 7;
        for (int k = 0; k < 4; k++) t.drawFastHLine(cx - k, a ? cy + 2 - k : cy - 2 + k, 2 * k + 1, T.FG);
    }
    const int track = sh - 32;
    const int th = max ? (track * vis / n > 10 ? track * vis / n : 10) : track;
    const int tyy = sy + 16 + (max ? (track - th) * (max - off) / max : 0);
    t.fillRect(sx + 1, tyy, SB - 2, th, T.ZEB);
    tmBevel(t, sx + 1, tyy, SB - 2, th, true, T);
    for (int k = -1; k <= 1; k++) t.drawFastHLine(sx + 4, tyy + th / 2 + k * 2, SB - 8, T.MID);
    s_tmUp    = { (int16_t)sx, (int16_t)sy, (int16_t)SB, 15 };
    s_tmDn    = { (int16_t)sx, (int16_t)(sy + sh - 15), (int16_t)SB, 15 };
    s_tmTrack = { (int16_t)sx, (int16_t)(sy + 15), (int16_t)SB, (int16_t)(sh - 30) };
    s_tmThumb = { (int16_t)sx, (int16_t)tyy, (int16_t)SB, (int16_t)th };

    // Scrolled up: a pill back to the newest, counting what arrived below.
    s_tmPill = { 0, 0, 0, 0 };
    if (off > 0) {
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

    // Which lines these are, and whether it is following the newest.
    const int fy = r.y + r.h - 10;
    t.drawFastHLine(r.x, fy - 1, r.w, T.LO);
    char st[24];
    snprintf(st, sizeof st, "%d-%d OF %d", n ? start + 1 : 0, n - off, n);
    tmText(t, st, r.x + 1, fy + 1, T.FGM);
    if (off > 0) tmText(t, "PAUSED", r.x + r.w - 1, fy + 1, TM_AMBER, TR_DATUM);
    else {
        t.fillCircle(r.x + r.w - 30, fy + 4, 2, T.FG);
        tmText(t, "LIVE", r.x + r.w - 1, fy + 1, T.FGM, TR_DATUM);
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
    tmText(t, "NOW", r.x + r.w - 1, gy + 2, T.FGD, TR_DATUM);
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
    tmText(t, b, r.x + r.w - 1, y, T.FG, TR_DATUM);
    y += 10;
    t.drawFastHLine(r.x, y, r.w, T.LO);
    y += 3;
    for (uint8_t k = 0; k < nc && y + 8 <= r.y + r.h; k++) {
        const uint8_t i = ord[k];
        if (k & 1) t.fillRect(r.x, y - 1, r.w, 9, T.ZEB);
        tmText(t, counterLabel(cols[i]), r.x + 1, y, cnt[i] ? Theme::colorFor(cols[i]) : T.LO);
        snprintf(b, sizeof b, "%u", (unsigned)cnt[i]);
        tmText(t, b, r.x + r.w - 1, y, cnt[i] ? T.FG : T.LO, TR_DATUM);
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
    tmText(t, b, r.x + r.w - 1, r.y + 11, T.FGM, TR_DATUM);
    tmText(t, "SEEN", r.x + 1, r.y + 21, T.FGD);
    snprintf(b, sizeof b, "%uX", (unsigned)d->hits);
    tmText(t, b, r.x + r.w - 1, r.y + 21, T.FG, TR_DATUM);
    tmText(t, "FIRST", r.x + 1, r.y + 31, T.FGD);
    tmStamp(*d, b, sizeof b);
    tmText(t, b, r.x + r.w - 1, r.y + 31, T.FG, TR_DATUM);
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
        tmText(t, b, r.x + r.w - 1, r.y + 41, d->active ? T.FG : T.FGM, TR_DATUM);
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
    if (total > s_tmLastTotal && s_tmScroll > 0) {
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
    const bool side = w >= 300 && w > h && H >= 150;
    const int sideW = side ? (w >= 400 ? 120 : 90) : 0;
    const int logW  = side ? w - sideW - 8 : w - 4;

    char chip[8];
    snprintf(chip, sizeof chip, "%s", s_tmScroll ? "PAUSED" : "LIVE");
    TmRect lr = tmWindow(t, 2, top, logW, H, "LOG", s_tmFocus == 0, false, chip,
                         s_tmScroll ? TM_AMBER : T.FG, T);
    s_tmWin[0] = { 2, (int16_t)top, (int16_t)logW, (int16_t)H };
    tmLog(t, now, eng, lr, T);

    if (side) {
        const int sx = w - sideW - 4;
        const int airH = H * 24 / 100, cntH = H * 42 / 100, detH = H - airH - cntH - 8;
        char title[16];
        snprintf(title, sizeof title, "AIR %lu/S", (unsigned long)rate);
        TmRect a = tmWindow(t, sx, top, sideW, airH, title, s_tmFocus == 1, true, nullptr, 0, T);
        s_tmWin[1] = { (int16_t)sx, (int16_t)top, (int16_t)sideW, (int16_t)airH };
        tmAir(t, a, T);
        const int cy = top + airH + 4;
        TmRect c = tmWindow(t, sx, cy, sideW, cntH, "COUNTS", s_tmFocus == 2, true, nullptr, 0, T);
        s_tmWin[2] = { (int16_t)sx, (int16_t)cy, (int16_t)sideW, (int16_t)cntH };
        tmCounts(t, eng, c, T);
        const int dy = cy + cntH + 4;
        const Detection* d = tmPicked(eng);
        // OPEN in its title bar: a tap anywhere on this window brings up the
        // device's menu (WATCH / HUNT / INFO / IGNORE).
        TmRect dr = tmWindow(t, sx, dy, sideW, detH, s_tmSelOn ? "DETAILS" : "LATEST", s_tmFocus == 3, !d,
                             d ? "OPEN" : nullptr, T.FG, T);
        s_tmWin[3] = { (int16_t)sx, (int16_t)dy, (int16_t)sideW, (int16_t)detH };
        tmDetails(t, now, d, dr, T);
    }
    t.setTextDatum(TL_DATUM);
    s_tmDrawn = true;
}

// Touches that land on a window. Claims the whole gesture from a press inside
// one: drag the log to scroll it, tap a row to pick it (again to let go), tap
// the arrows / track / NOW pill, tap any window to bring its title up.
bool uiTerminalTouch(int x, int y, bool valid, bool justDown, uint32_t now) {
    (void)now;
    static bool own = false, dragged = false, done = false, inBody = false, onDet = false;
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
        own = true; dragged = false; done = false;
        startY = lastY = y; lastX = x; startScroll = s_tmScroll;
        s_tmFocus = (uint8_t)which;
        inBody = which == 0 && s_tmBody.in(x, y);
        onDet  = which == 3 && s_tmDetOn;
        if (which == 0) {
            const int page = s_tmVis > 1 ? s_tmVis - 1 : 1;
            if (s_tmPill.in(x, y))       { setScroll(0); done = true; inBody = false; }
            else if (s_tmUp.in(x, y))    { setScroll(s_tmScroll + 1); done = true; }
            else if (s_tmDn.in(x, y))    { setScroll(s_tmScroll - 1); done = true; }
            else if (s_tmTrack.in(x, y) && !s_tmThumb.in(x, y)) {
                setScroll(y < s_tmThumb.y ? s_tmScroll + page : s_tmScroll - page); done = true;
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
            if (dragged) setScroll(startScroll + dy / TM_ROW_H);
        }
        return true;
    }
    // Let go: a tap that did not drag picks the row under it; the picked
    // row tapped again, or the DETAILS window, asks for the device's menu;
    // the empty space under the rows lets the pick go.
    own = false;
    if (!dragged && !done && onDet && s_tmWin[3].in(lastX, lastY)) {
        memcpy(s_tmOpenMac, s_tmDetMac, 6);
        s_tmOpenReq = true;
    } else if (!dragged && !done && inBody) {
        const int j = (lastY - s_tmRowY0 + 1) / TM_ROW_H;
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

bool uiTerminalTakeOpen(uint8_t mac[6]) {
    if (!s_tmOpenReq) return false;
    s_tmOpenReq = false;
    memcpy(mac, s_tmOpenMac, 6);
    return true;
}
