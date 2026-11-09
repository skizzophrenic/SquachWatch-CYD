// SquachWatch-CYD -- the HUMAN SENSING screen (SENSE_CYD_APP). See include/ui_sensing_cyd.h.
//
// 320x240, landscape:
//
//   title bar     the gear (back to Settings), LIVE, channel and readings a second
//   SPECTRUM.EXE  how fast the channel's shape changes, 0-2.5 Hz across: the
//                 newest spectrum on top and the last ~11 s sliding down below it
//   tiles         NOW / FLOOR / PEAK / MARGIN over the last 80 s
//   MOTION.EXE    the last 30 s: glyph rain from the traffic, the score fill and
//                 trace, the marching MOTION line, peaks, the value now
//   banner        CLEAR / PRESENCE / PLEASE STAND BY in Bangers, the presence
//                 hold ring, Squachy reacting; ADD WIFI (tap) with none saved
//   buttons       [ BACK ] Settings, [ RECAL ], [ OFF ] back to the detector
//
// One radio path, the router answering the CYD's pings, so there is no list of
// paths: the room goes to the two windows.
#if defined(SENSE_CYD_APP)
#include "ui_sensing_cyd.h"
#include "sense_store.h"
#include "sense_cyd.h"
#include "settings.h"
#include "theme.h"
#include "squachy.h"
#include <Arduino.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace {

const uint8_t  MOTION_LINE = 28;      // the motion model's MOTION threshold
const uint32_t HOLD_MS     = 15000;   // the engine's PRESENCE hold
const int      STRIP_SPAN  = 300;     // 30 s at 10 statuses a second

uint32_t s_enterMs = 0, s_lastMs = 0;
const char* s_lastWord = nullptr;
uint32_t s_wordAt = 0;

inline uint16_t mixc(uint16_t a, uint16_t b, int t256) { if (t256 < 0) t256 = 0; if (t256 > 256) t256 = 256; return Theme::blend(a, b, (uint16_t)t256); }
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
uint32_t hash32(uint32_t n) { n ^= 61; n ^= n >> 16; n += n << 3; n ^= n >> 4; n *= 0x27d4eb2d; n ^= n >> 15; return n; }
inline float hashf(uint32_t n) { return (hash32(n) & 0xFFFFFF) / 16777216.0f; }

void text(TFT_eSPI& t, int x, int y, const char* s, uint16_t fg, uint16_t bg, uint8_t size, uint8_t align = 0) {
    t.setTextFont(1); t.setTextSize(size); t.setTextWrap(false);
    const int w = t.textWidth(s);
    t.setTextColor(fg, bg);
    t.setCursor(align == 1 ? x - w / 2 : (align == 2 ? x - w : x), y);
    t.print(s);
}
void textT(TFT_eSPI& t, int x, int y, const char* s, uint16_t fg, uint8_t size, uint8_t align = 0) {
    t.setTextFont(1); t.setTextSize(size); t.setTextWrap(false);
    const int w = t.textWidth(s);
    t.setTextColor(fg);
    t.setCursor(align == 1 ? x - w / 2 : (align == 2 ? x - w : x), y);
    t.print(s);
}
// The banner's line beside the headline, in the 16 px body font (font 2): the
// 6x8 system font looked wrong there next to Bangers. A line too long to clear
// Squachy at `right` falls back to the small font.
void subLine(TFT_eSPI& t, int x, int midY, const char* s, uint16_t fg, int right) {
    t.setTextWrap(false); t.setTextSize(1); t.setTextColor(fg);
    t.setTextFont(2);
    if (x + t.textWidth(s) <= right) { t.setCursor(x, midY - 8); t.print(s); t.setTextFont(1); return; }
    t.setTextFont(1);
    t.setCursor(x, midY - 4); t.print(s);
}
void headline(TFT_eSPI& t, int x, int y, const char* s, uint16_t col, int glitch) {
    if (glitch > 0) {
        Theme::drawBangersText(t, x - glitch, y, s, Theme::CYAN, Theme::BangersSize::MD);
        Theme::drawBangersText(t, x + glitch, y + 1, s, Theme::PINK, Theme::BangersSize::MD);
    }
    Theme::drawBangersOutline(t, x, y, s, Theme::BLACK, Theme::BangersSize::MD, 2);
    Theme::drawBangersText(t, x, y, s, col, Theme::BangersSize::MD);
}
void arc(TFT_eSPI& t, int cx, int cy, int r, float a0, float a1, uint16_t col, int thick = 1) {
    if (a1 < a0) return;
    const int steps = 8 + (int)((a1 - a0) * r / 5);
    for (int k = 0; k < thick; k++) {
        const int rr = r - k;
        int px = cx + (int)lroundf(cosf(a0) * rr), py = cy + (int)lroundf(sinf(a0) * rr);
        for (int i = 1; i <= steps; i++) {
            const float a = a0 + (a1 - a0) * i / steps;
            const int x = cx + (int)lroundf(cosf(a) * rr), y = cy + (int)lroundf(sinf(a) * rr);
            t.drawLine(px, py, x, y, col); px = x; py = y;
        }
    }
}
void osWindow(TFT_eSPI& t, int x, int y, int w, int h, const char* name, uint16_t body) {
    t.fillRect(x, y, w, h, body);
    for (int i = 0; i < w; i++) {
        const int f = i * 512 / w;
        const uint16_t c = f < 256 ? mixc(Theme::PURPLE, Theme::CYAN, f) : mixc(Theme::CYAN, Theme::PINK, f - 256);
        t.drawFastVLine(x + i, y, 9, mixc(Theme::BG, c, 150));
    }
    textT(t, x + 3, y + 1, name, Theme::WHITE, 1);
    t.drawRect(x, y, w, h, Theme::PURPLE);
    t.fillRect(x + w - 8, y + 2, 5, 5, Theme::BG); t.drawRect(x + w - 8, y + 2, 5, 5, Theme::WHITE);
}

struct Startle {   // a startle is a moment, not a state
    uint32_t lastMotion = 0, until = 0, lastFire = 0; bool was = false, fired = false;
    bool operator()(uint32_t now, bool motion) {
        if (motion && !was && (!lastMotion || now - lastMotion > 20000) && (!fired || now - lastFire > 30000)) {
            until = now + 1200; lastFire = now; fired = true;
        }
        if (motion) lastMotion = now;
        was = motion;
        return (int32_t)(until - now) > 0;
    }
};

// The router keeps one name, picked from its address.
const char* const CRYPTIDS[16] = { "HEFFER", "ROCKO", "KRUMM", "GROMBLE", "ICKIS", "OBLINA", "STIMPY", "HELGA",
                                   "ARTIE", "DOUG", "DOGMEAT", "CODSWORTH", "VAULT BOY", "DEATHCLAW", "MR HANDY", "BRAHMIN" };
inline bool routerHot(const SenseStore::Router& p) { return p.live && p.score >= MOTION_LINE && p.cal >= 100; }

struct Word { const char* w; uint16_t c; char sub[44]; };
Word stateWord(uint32_t now) {
    Word r; r.sub[0] = 0;
    const SenseReport& s = SenseStore::last();
    const char* net = senseCydNetwork();
    if (!net[0]) { r.w = "ADD WIFI"; r.c = Theme::AMBER; snprintf(r.sub, sizeof r.sub, "tap here to save a network"); }
    else if (!SenseStore::heard()) { r.w = "STARTING"; r.c = Theme::AMBER; snprintf(r.sub, sizeof r.sub, "waking the radio"); }
    else if (!SenseStore::fresh(now)) { r.w = "RADIO LOST"; r.c = Theme::AMBER; snprintf(r.sub, sizeof r.sub, "nothing for %lu s", (unsigned long)((now - SenseStore::lastAt()) / 1000)); }
    else if (s.mode != SENSE_MODE_STA) {
        r.w = "JOINING"; r.c = Theme::AMBER;
        const char* shown = Settings::privacyMode() ? "a saved network" : net;   // Privacy Mode: no name
        const uint16_t why = senseCydReason();
        if (why) snprintf(r.sub, sizeof r.sub, "%.24s (code %u)", shown, (unsigned)why);
        else snprintf(r.sub, sizeof r.sub, "%.32s", shown);
    }
    else if (s.state == SENSE_STATE_CAL) { r.w = "PLEASE STAND BY"; r.c = Theme::AMBER; snprintf(r.sub, sizeof r.sub, "calibrating %u%%", (unsigned)s.cal); }
    else if (s.state == SENSE_STATE_LOWDATA) { r.w = "LOW SIGNAL"; r.c = Theme::AMBER; snprintf(r.sub, sizeof r.sub, "too little from the router"); }
    else if (s.presence) {
        r.w = "PRESENCE"; r.c = Theme::VAPOR_PINK;
        const uint32_t since = SenseStore::sinceMotion(now);
        if (s.state == SENSE_STATE_MOTION || since < 1500) snprintf(r.sub, sizeof r.sub, "something is moving");
        else if (since != UINT32_MAX) snprintf(r.sub, sizeof r.sub, "last movement %lu s ago", (unsigned long)(since / 1000));
        else snprintf(r.sub, sizeof r.sub, "holding after movement");
    } else { r.w = "CLEAR"; r.c = Theme::GREEN; snprintf(r.sub, sizeof r.sub, "the room is still"); }
    return r;
}
int glitchFor(const Word& w, uint32_t now) {
    if (w.w != s_lastWord) { s_lastWord = w.w; s_wordAt = now; }
    const uint32_t age = now - s_wordAt;
    return age < 420 ? (int)(4 - age / 105) : 0;
}

struct Tiles { int now, floor, peak, margin; };
Tiles tiles() {
    Tiles r = { -1, -1, -1, -1 };
    const uint16_t n = SenseStore::count();
    if (!n) return r;
    if (SenseStore::stateAt(n - 1) >= SENSE_STATE_STILL) r.now = SenseStore::scoreAt(n - 1);
    uint16_t hist[34] = {0}; uint16_t still = 0;
    for (uint16_t i = 0; i < n; i++) {
        const uint8_t st = SenseStore::stateAt(i), sc = SenseStore::scoreAt(i);
        if (st >= SENSE_STATE_STILL && (int)sc > r.peak) r.peak = sc;
        if (st == SENSE_STATE_STILL) { hist[sc / 3 > 33 ? 33 : sc / 3]++; still++; }
    }
    if (still) { uint16_t acc = 0; for (int b = 0; b <= 33; b++) { acc += hist[b]; if (acc * 2 >= still) { r.floor = b * 3 + 1; break; } } }
    if (r.peak >= 0 && r.floor >= 0) r.margin = r.peak - r.floor;
    return r;
}

// ---- layout (320x240) -------------------------------------------------------
const int SP_X = 2,   SP_Y = 22,  SP_W = 98,  SP_H = 153;   // SPECTRUM.EXE
const int TL_X = 103, TL_Y = 22,  TL_W = 51,  TL_H = 34, TL_GAP = 3;   // tiles
const int ST_X = 103, ST_Y = 59,  ST_W = 215, ST_H = 116;   // MOTION.EXE
const int BN_Y = 178, BN_H = 34;                            // banner

void backdrop(TFT_eSPI& t, uint32_t now) {
    const uint16_t g = mixc(Theme::BG, Theme::PURPLE, 26);
    for (int x = (int)((now / 120) % 32); x < 320; x += 32) t.drawFastVLine(x, 20, 192, g);
    for (int y = 36; y < 212; y += 32) t.drawFastHLine(0, y, 320, g);
    for (int q = 0; q < 24; q++) {
        const int x = (int)(hashf(q * 11) * 320), y = 22 + (int)(hashf(q * 13) * 188);
        const float tw = fabsf(sinf(now / (600.0f + q * 41) + q));
        t.drawPixel(x, y, mixc(Theme::BG, Theme::WHITE, (int)(30 + 120 * tw)));
    }
}

void statusLine(TFT_eSPI& t, uint32_t now) {
    const SenseReport& s = SenseStore::last();
    const bool fresh = SenseStore::fresh(now) && s.mode == SENSE_MODE_STA;
    char b[40];
    if (fresh && (now % 1000) < 600) t.fillCircle(36, 9, 3, Theme::GREEN);
    textT(t, 42, 6, fresh ? "LIVE" : (senseCydNetwork()[0] ? "WAIT" : "NO WIFI"), fresh ? Theme::GREEN : Theme::AMBER, 1);
    if (SenseStore::heard() && s.chan) {
        snprintf(b, sizeof b, "2.4 GHz ch %u  %u/s", (unsigned)s.chan, (unsigned)s.rate); textT(t, 84, 6, b, Theme::CYAN, 1);
        if (s.rssi > -127) { snprintf(b, sizeof b, "%d dBm", (int)s.rssi); textT(t, 288, 6, b, Theme::VAPOR_PURPLE, 1, 2); }
    }
}

// The waterfall's colours: BG -> purple -> pink -> pale yellow -> white.
uint16_t ramp(uint8_t v) {
    if (v < 64)  return mixc(Theme::BG, mixc(Theme::BG, Theme::PURPLE, 170), v * 4);
    if (v < 128) return mixc(mixc(Theme::BG, Theme::PURPLE, 170), Theme::PINK, (v - 64) * 4);
    if (v < 192) return mixc(Theme::PINK, Theme::VAPOR_YELLOW, (v - 128) * 4);
    return mixc(Theme::VAPOR_YELLOW, Theme::WHITE, (v - 192) * 4);
}

void spectrum(TFT_eSPI& t) {
    osWindow(t, SP_X, SP_Y, SP_W, SP_H, "SPECTRUM.EXE", Theme::BG);
    const int x0 = SP_X + 1, gw = 96, bins = 32, bw = gw / bins;          // 3 px a bin
    const int cy0 = SP_Y + 11, ch = 22;                                    // the newest spectrum
    const int wy0 = cy0 + ch + 3, rowH = 1, rows = 108;                    // the waterfall, ~11 s
    const int ay = wy0 + rows * rowH + 2;                                  // the Hz axis
    const float binHz = senseCydSpecBinHz();
    const uint8_t* top = senseCydSpecRow(0);
    if (top) {
        for (int b = 1; b < bins; b++) {
            const int hgt = top[b] * ch / 255;
            if (hgt <= 0) continue;
            t.fillRect(x0 + b * bw, cy0 + ch - hgt, bw, hgt, mixc(Theme::BG, Theme::VAPOR_PINK, 80));
            t.drawFastHLine(x0 + b * bw, cy0 + ch - hgt, bw, Theme::VAPOR_PINK);
        }
        const int pk = senseCydSpecPeak();
        if (pk > 0) {   // the peak in Hz: a rhythm in the room, not a measurement of anyone
            const int px = x0 + pk * bw + 1, py = cy0 + ch - top[pk] * ch / 255;
            t.fillCircle(px, py, 1, Theme::WHITE);
            char lb[10]; snprintf(lb, sizeof lb, "%.2fHz", pk * binHz);
            const int lw = (int)strlen(lb) * 6;
            const int lx = px + 4 + lw > SP_X + SP_W - 2 ? px - 4 - lw : px + 4;
            textT(t, lx, py - 4 < cy0 ? cy0 : py - 4, lb, Theme::WHITE, 1);
        }
    }
    t.drawFastHLine(SP_X + 1, cy0 + ch + 1, SP_W - 2, mixc(Theme::BG, Theme::PURPLE, 120));
    const int n = senseCydSpecRows();
    for (int r = 0; r < rows && r < n; r++) {
        const uint8_t* row = senseCydSpecRow(r);
        if (!row) break;
        for (int b = 1; b < bins; b++) if (row[b] > 5) t.fillRect(x0 + b * bw, wy0 + r * rowH, bw, rowH, ramp(row[b]));
    }
    if (!n) {   // nothing yet: the first spectrum needs 12.8 s of readings
        const SenseReport& s = SenseStore::last();
        textT(t, SP_X + SP_W / 2, wy0 + 40, s.mode == SENSE_MODE_STA ? "LISTENING" : "WAITING", mixc(Theme::BG, Theme::WHITE, 140), 1, 1);
        textT(t, SP_X + SP_W / 2, wy0 + 52, "to the room", mixc(Theme::BG, Theme::WHITE, 110), 1, 1);
    }
    for (int f = 0; f <= 2; f++) {
        const int ax = x0 + (int)lroundf(f / binHz) * bw;
        t.drawFastVLine(ax, ay - 2, 2, Theme::VAPOR_PURPLE);
        textT(t, f == 2 ? ax - 6 : ax, ay, f == 2 ? "2Hz" : (f ? "1" : "0"), Theme::VAPOR_PURPLE, 1);
    }
}

void tileRow(TFT_eSPI& t, uint32_t now) {
    const Tiles n = tiles();
    static const char* const KEYS[4] = { "NOW", "FLOOR", "PEAK", "MARGIN" };
    const int vals[4] = { n.now, n.floor, n.peak, n.margin };
    const uint16_t dimW = mixc(Theme::BG, Theme::WHITE, 150);
    const uint16_t mcol = n.margin < 0 ? dimW : (n.margin >= 30 ? Theme::GREEN : (n.margin >= 12 ? Theme::AMBER : Theme::RED));
    const Word w = stateWord(now);
    const uint16_t nowc = SenseStore::fresh(now) ? (w.c == Theme::GREEN ? Theme::WHITE : w.c) : Theme::WHITE;
    const uint16_t cols[4] = { nowc, dimW, Theme::AMBER, mcol };
    const uint16_t fill = mixc(Theme::BG, Theme::VAPOR_BLUE, 26), edge = mixc(Theme::BG, Theme::VAPOR_BLUE, 70);
    for (int k = 0; k < 4; k++) {
        const int x = TL_X + k * (TL_W + TL_GAP), y = TL_Y, wd = TL_W, h = TL_H;
        t.fillRect(x, y, wd, h, fill); t.drawRect(x, y, wd, h, edge);
        const uint16_t cc = k == 0 ? mixc(Theme::BG, w.c, 140 + (int)(100 * sinf(now / 260.0f))) : Theme::PURPLE;
        t.drawFastHLine(x, y, 6, cc); t.drawFastVLine(x, y, 6, cc);
        t.drawFastHLine(x + wd - 6, y + h - 1, 6, cc); t.drawFastVLine(x + wd - 1, y + h - 6, 6, cc);
        if (k == 0) t.drawRect(x + 1, y + 1, wd - 2, h - 2, cc);
        text(t, x + wd / 2, y + 3, KEYS[k], mixc(Theme::BG, Theme::WHITE, 200), fill, 1, 1);
        char v[8];
        if (vals[k] < 0) snprintf(v, sizeof v, "--"); else if (k == 3) snprintf(v, sizeof v, "%+d", vals[k]); else snprintf(v, sizeof v, "%d", vals[k]);
        text(t, x + wd / 2, y + 14, v, cols[k], fill, 2, 1);
    }
}

void stripRain(TFT_eSPI& t, uint32_t now, int x0, int y0, int w, int h, int score, int rate, bool surge, const char* word) {
    static const char GLYPHS[] = "01ABCDEFGHIJKLMNOPQRSTUVWXYZ!@#$%^&*<>{}[]/|+=~SASQUACH";
    const int GLN = sizeof(GLYPHS) - 1, cw = 7, ch = 9;
    const int cols = w / cw, rows = h / ch;
    const float speed = 0.004f + score / 9000.0f;
    t.setTextFont(1); t.setTextSize(1); t.setTextWrap(false);
    const float busy = clampf(0.08f + rate / 160.0f + (surge ? 0.25f : 0.0f), 0.08f, 0.6f);
    const int wordCol = word ? (int)(hash32((uint32_t)(now / 2600)) % (uint32_t)cols) : -1;
    for (int c = 0; c < cols; c++) {
        const float h0 = hashf(c * 7919u + 3);
        if (c != wordCol && hashf(c * 31u + 17) > busy) continue;
        const float colSpeed = speed * (0.6f + h0) * (surge ? 1.8f : 1.0f);
        const int trail = 3 + (int)(h0 * 4) + score / 30;
        const float head = fmodf(now * colSpeed + h0 * 97.0f, (float)(rows + trail + 6));
        for (int r = 0; r < rows; r++) {
            const float d = head - r;
            if (d < 0 || d > trail) continue;
            const int g = (int)(hash32(c * 131u + r * 17u + (uint32_t)(now / (90 + (int)(h0 * 200)))) % GLN);
            const bool hot = r >= rows - score * rows / 100;
            const uint16_t hue = hot ? Theme::PINK : (h0 > 0.55f ? Theme::GREEN : Theme::CYAN);
            uint16_t col;
            if (d < 1) col = mixc(Theme::BG, hue, 120);
            else { const int a = 80 - (int)(d * 64 / trail); col = mixc(Theme::BG, hue, a < 14 ? 14 : a); }
            char gch = GLYPHS[(size_t)g];
            if (c == wordCol) {   // the router's name, drifting down one column while it is hot
                const int wl = (int)strlen(word), li = r - (int)(head - trail);
                if (li >= 0 && li < wl && word[li] != ' ') { gch = word[li]; col = mixc(Theme::BG, Theme::VAPOR_PINK, d < 1 ? 220 : 150); }
            }
            t.setTextColor(col);
            t.setCursor(x0 + c * cw, y0 + r * ch);
            const char gs[2] = { gch, 0 };
            t.print(gs);
        }
    }
}

void strip(TFT_eSPI& t, uint32_t now, uint32_t dt) {
    const int sx = ST_X, sy = ST_Y, sw = ST_W, sh = ST_H;
    const int iy = sy + 10, ih = sh - 11, gw = sw - 2;
    const uint16_t n = SenseStore::count();
    auto idxAt = [&](int c) -> int { const int i = (int)n - STRIP_SPAN + c * STRIP_SPAN / gw; return i < 0 ? -1 : i; };
    auto yAt = [&](int sc) { return iy + ih - 3 - sc * (ih - 6) / 100; };
    const int scoreNow = n && SenseStore::stateAt(n - 1) >= SENSE_STATE_STILL ? SenseStore::scoreAt(n - 1) : 0;
    const int rateNow = SenseStore::fresh(now) ? SenseStore::last().rate : 0;
    char ttl[40]; snprintf(ttl, sizeof ttl, "MOTION.EXE  30 s  TRAFFIC %d/s", rateNow);
    osWindow(t, sx, sy, sw, sh, ttl, Theme::BG);
    const uint32_t sm = SenseStore::sinceMotion(now);
    const SenseStore::Router& rt = SenseStore::router();
    const char* hotName = routerHot(rt) ? CRYPTIDS[hash32(rt.tag) % 16] : nullptr;
    stripRain(t, now, sx + 1, iy, gw, ih, scoreNow, rateNow, sm != UINT32_MAX && sm < 1500, hotName);
    const int ty = yAt(MOTION_LINE);
    for (int c = 0; c < gw; c++) {   // the fill, pink while PRESENCE held, heat rising through it
        const int i = idxAt(c); if (i < 0) continue;
        const int y = yAt(SenseStore::scoreAt((uint16_t)i));
        const bool pres = SenseStore::presenceAt((uint16_t)i);
        const uint16_t base = pres ? Theme::VAPOR_PINK : Theme::VAPOR_BLUE;
        t.drawFastVLine(sx + 1 + c, y + 1, iy + ih - 2 - y, mixc(Theme::BG, base, pres ? 70 : 45));
        const int phase = (int)((now / 45 + c / 3) % 9);
        for (int yy = iy + ih - 3 - phase; yy > y + 1; yy -= 9) t.drawPixel(sx + 1 + c, yy, mixc(Theme::BG, base, pres ? 120 : 80));
    }
    {   // the MOTION line marches, so the threshold reads as live
        const int off = (int)((now / 70) % 7);
        for (int x = sx + 1 + off; x < sx + sw - 1; x += 7) t.drawFastHLine(x, ty, 3, Theme::AMBER);
    }
    int lastPk = -100;
    for (int c = 2; c < gw - 2; c++) {   // peaks over the line: the moments that counted
        const int i0 = idxAt(c - 2), i1 = idxAt(c), i2 = idxAt(c + 2);
        if (i0 < 0 || i1 < 0 || i2 < 0) continue;
        const int v0 = SenseStore::scoreAt((uint16_t)i0), v1 = SenseStore::scoreAt((uint16_t)i1), v2 = SenseStore::scoreAt((uint16_t)i2);
        if (v1 < MOTION_LINE || v1 < v0 || v1 < v2 || c - lastPk < 16) continue;
        lastPk = c;
        const int px = sx + 1 + c, py = yAt(v1) - 5, r = 2 + (int)((now / 120 + c) % 3);
        t.drawLine(px, py - r, px + r, py, Theme::VAPOR_PINK); t.drawLine(px + r, py, px, py + r, Theme::VAPOR_PINK);
        t.drawLine(px, py + r, px - r, py, Theme::VAPOR_PINK); t.drawLine(px - r, py, px, py - r, Theme::VAPOR_PINK);
    }
    int lpx = -1, lpy = 0;
    for (int c = 0; c < gw; c += 2) {
        const int i = idxAt(c); if (i < 0) continue;
        const int y = yAt(SenseStore::scoreAt((uint16_t)i));
        if (lpx >= 0) { t.drawLine(lpx, lpy, sx + 1 + c, y, Theme::CYAN); t.drawLine(lpx, lpy + 1, sx + 1 + c, y + 1, mixc(Theme::BG, Theme::CYAN, 170)); }
        lpx = sx + 1 + c; lpy = y;
    }
    if (lpx >= 0) {
        t.fillCircle(lpx, lpy, 3, mixc(Theme::BG, Theme::CYAN, 90)); t.fillRect(lpx - 1, lpy - 1, 2, 2, Theme::WHITE);
        static float shown = 0; static bool wasOver = false; static uint32_t crossAt = 0;
        shown += (scoreNow - shown) * clampf(dt / 180.0f, 0.05f, 1.0f);
        const bool over = scoreNow >= MOTION_LINE;
        if (over != wasOver) { wasOver = over; crossAt = now; }
        char v[6]; snprintf(v, sizeof v, "%d", (int)lroundf(shown));
        const int bw = (int)strlen(v) * 12 + 6, by = lpy - 9 < iy ? iy : (lpy - 9 > iy + ih - 18 ? iy + ih - 18 : lpy - 9);
        const bool flash = now - crossAt < 300;
        t.fillRect(sx + sw - bw - 3, by, bw, 18, flash ? Theme::WHITE : Theme::BG);
        t.drawRect(sx + sw - bw - 3, by, bw, 18, over ? Theme::PINK : Theme::CYAN);
        text(t, sx + sw - 6, by + 2, v, flash ? Theme::BLACK : Theme::WHITE, flash ? Theme::WHITE : Theme::BG, 2, 2);
    }
    text(t, sx + 5, ty - 9, "MOTION", Theme::AMBER, Theme::BG, 1);
}

void banner(TFT_eSPI& t, uint32_t now) {
    const Word w = stateWord(now);
    const int by = BN_Y, bh = BN_H;
    const uint16_t base = mixc(Theme::BG, w.c, 30);
    t.fillRect(2, by, 316, bh, base);
    const int sxp = (int)((now / 6) % 480) - 80;   // a light sweeping across, scanlines through it
    for (int k = 0; k < 40; k++) { const int x = sxp + k; if (x > 3 && x < 317) t.drawFastVLine(x, by + 2, bh - 4, mixc(base, w.c, 60 - abs(k - 20) * 3)); }
    for (int y = by + 3; y < by + bh - 3; y += 3) t.drawFastHLine(4, y, 312, mixc(Theme::BG, w.c, 18));
    t.drawRect(2, by, 316, bh, w.c); t.drawRect(3, by + 1, 314, bh - 2, w.c);
    headline(t, 9, by + 3, w.w, w.c, glitchFor(w, now));
    const SenseReport& s = SenseStore::last();
    const uint32_t since = SenseStore::sinceMotion(now);
    const int rx = 9 + Theme::bangersTextWidth(w.w, Theme::BangersSize::MD) + 14, ry = by + bh / 2;
    if (SenseStore::fresh(now) && s.presence && s.state != SENSE_STATE_MOTION && since != UINT32_MAX) {
        const float left = 1.0f - clampf(since / (float)HOLD_MS, 0, 1);
        arc(t, rx, ry, 9, 0, 6.2832f, mixc(Theme::BG, Theme::PURPLE, 90), 3);
        arc(t, rx, ry, 9, -1.5708f, -1.5708f + 6.2832f * left, Theme::VAPOR_PINK, 3);
        subLine(t, rx + 14, ry, w.sub, Theme::VAPOR_PINK, 278);
    } else {
        subLine(t, rx - 8, ry, w.sub, Theme::CYAN, 278);
    }
    using VP = Squachy::VisitPose;
    static Startle startle;
    const bool moving = SenseStore::fresh(now) && s.state == SENSE_STATE_MOTION;
    const VP pose = startle(now, moving) ? VP::STARTLED
                  : moving ? VP::POINT
                  : s.presence ? VP::CROUCH
                  : (since == UINT32_MAX || since > 120000) ? VP::SLEEPY : VP::NONE;
    // He pops up out of the band at the right.
    Squachy::drawWaving(t, 296, by + bh - 2, now, 0.42f, nullptr, false, 0, false, 34, false, false, false, pose);
}

void buttons(TFT_eSPI& t) {
    const Theme::ButtonBarGeom g = Theme::computeButtonBar(t.width(), t.height());
    static const char* const L[3] = { "[ BACK ]", "[ RECAL ]", "[ OFF ]" };
    for (int k = 0; k < 3; k++) Theme::drawButton(t, g.x[k], g.y, g.w[k], g.h, L[k], false);
}

}  // namespace

void uiSensingInit(TFT_eSPI& t) {
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
    s_enterMs = s_lastMs = millis();
    s_lastWord = nullptr;
}

void uiSensingTick(TFT_eSPI& t, uint32_t now, bool advance) {
    (void)advance;
    const uint32_t dt = s_lastMs && now > s_lastMs ? (now - s_lastMs > 200 ? 200 : now - s_lastMs) : 16;
    s_lastMs = now;
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
    if (t.width() < t.height()) {   // laid out for landscape only
        Theme::drawTitleBar(t, ">> HUMAN SENSING <<");
        text(t, t.width() / 2, t.height() / 2 - 20, "HUMAN SENSING", Theme::CYAN, Theme::BG, 2, 1);
        text(t, t.width() / 2, t.height() / 2 + 4, "turn the board sideways", Theme::WHITE, Theme::BG, 1, 1);
        buttons(t);
        return;
    }
    backdrop(t, now);
    spectrum(t);
    tileRow(t, now);
    strip(t, now, dt);
    banner(t, now);
    Theme::drawTitleBar(t, ">> HUMAN SENSING <<");
    statusLine(t, now);
    buttons(t);
}

SensingTap uiSensingTap(int x, int y, int screenW, int screenH, uint32_t now) {
    (void)now;
    const Theme::ButtonBarGeom g = Theme::computeButtonBar(screenW, screenH);
    if (y >= g.y - 2 && y <= g.y + g.h + 4) {
        for (int i = 0; i < 3; i++) if (x >= g.x[i] && x <= g.x[i] + g.w[i]) {
            if (i == 0) return SensingTap::BACK;
            if (i == 1) return SensingTap::RECAL;
            return SensingTap::OFF;
        }
        return SensingTap::NONE;
    }
    // No saved network: the banner is the way to add one.
    if (!senseCydNetwork()[0] && y >= BN_Y && y < BN_Y + BN_H) return SensingTap::ADD_WIFI;
    return SensingTap::NONE;
}
#endif  // SENSE_CYD_APP
