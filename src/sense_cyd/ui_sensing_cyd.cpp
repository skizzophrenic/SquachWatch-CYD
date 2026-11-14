// SquachWatch-CYD -- the HUMAN SENSING screen (SENSE_CYD_APP). See include/ui_sensing_cyd.h.
//
// Laid out per panel size (the Lay table below, drawn up in the Sensing Screen
// Workbench artifact): 320x240 is the 2.8" CYD's, and 400x240, 480x320,
// 240x240, 240x135 and 640x360 are the other boards'. On the 320x240 panel:
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
// size: 0 Bangers LG, 1 Bangers MD, 2 the built-in font at double size,
// outlined, for a banner too short for Bangers.
void headline(TFT_eSPI& t, int x, int y, const char* s, uint16_t col, int glitch, uint8_t size = 1) {
    if (size >= 2) {
        if (glitch > 0) { textT(t, x - glitch, y, s, Theme::CYAN, 2); textT(t, x + glitch, y + 1, s, Theme::PINK, 2); }
        for (int dx = -1; dx <= 1; dx++) for (int dy = -1; dy <= 1; dy++) if (dx || dy) textT(t, x + dx, y + dy, s, Theme::BLACK, 2);
        textT(t, x, y, s, col, 2);
        return;
    }
    const Theme::BangersSize bs = size == 0 ? Theme::BangersSize::LG : Theme::BangersSize::MD;
    if (glitch > 0) {
        Theme::drawBangersText(t, x - glitch, y, s, Theme::CYAN, bs);
        Theme::drawBangersText(t, x + glitch, y + 1, s, Theme::PINK, bs);
    }
    Theme::drawBangersOutline(t, x, y, s, Theme::BLACK, bs, 2);
    Theme::drawBangersText(t, x, y, s, col, bs);
}
int headlineWidth(const char* s, uint8_t size) {
    if (size >= 2) return (int)strlen(s) * 12;
    return Theme::bangersTextWidth(s, size == 0 ? Theme::BangersSize::LG : Theme::BangersSize::MD);
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
        // The driver's reason in words. A refused board waits, longer each
        // time, so the screen says so instead of looking stuck.
        const uint16_t why = senseCydReason(), wait = senseCydRetryIn();
        const char* said = why == 201 ? "network not found"
                         : (why == 2 || why == 15 || why == 202 || why == 204) ? "router said no"
                         : why == 203 ? "router busy"
                         : (why == 8 || why == 200) ? "signal lost" : nullptr;
        if (wait && said) snprintf(r.sub, sizeof r.sub, "%s, again in %u s", said, (unsigned)wait);
        else if (wait) snprintf(r.sub, sizeof r.sub, "no answer, again in %u s", (unsigned)wait);
        else if (why && said) snprintf(r.sub, sizeof r.sub, "%s, trying again", said);
        else if (why) snprintf(r.sub, sizeof r.sub, "%.24s (code %u)", shown, (unsigned)why);
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

// ---- layout, per panel size ---------------------------------------------------
// One row per screen size, from the Sensing Screen Workbench. The 320x240 row
// is the 2.8" CYD's layout as it shipped. Tiles name which of NOW / FLOOR /
// PEAK / MARGIN show, in order; a tile too short for its label over its
// number puts them side by side. HEAD_SM is the built-in font at double size,
// outlined: the small boards' banner has no room for Bangers. No button bar
// (btOn false) is for the boards with no touch screen.
enum : uint8_t { TL_NOW, TL_FLOOR, TL_PEAK, TL_MARGIN };
enum : uint8_t { HEAD_LG, HEAD_MD, HEAD_SM };
struct Lay {
    int16_t w, h;                                        // the panel it is for
    int16_t spX, spY, spW, spH; uint8_t spTop, spRowH;   // SPECTRUM.EXE: newest bars' height, waterfall row height
    int16_t tlX, tlY, tlW, tlH; uint8_t tlCols, tlRows, tlGap, tlKey, tlVal, tlN, tlWhich[4];
    int16_t stX, stY, stW, stH; bool stRain;             // MOTION.EXE
    int16_t bnX, bnY, bnW, bnH; uint8_t bnHead; bool bnSquachy, bnSub; float bnSq;
    bool btOn; uint8_t btH, btLabel;                     // the button bar
};
const Lay LAYS[] = {
    { 320, 240,   2, 22,  98, 153, 22, 1,  103, 22, 213, 34, 4, 1, 3, 1, 2, 4, { TL_NOW, TL_FLOOR, TL_PEAK, TL_MARGIN },
      103, 59, 215, 116, true,   2, 178, 316, 34, HEAD_MD, true,  true,  0.42f,  true,  20, 1 },
    { 400, 240,   2, 22, 130, 153, 22, 1,  135, 22, 263, 34, 4, 1, 3, 1, 2, 4, { TL_NOW, TL_FLOOR, TL_PEAK, TL_MARGIN },
      135, 59, 263, 116, true,   2, 178, 396, 34, HEAD_MD, true,  true,  0.42f,  true,  20, 1 },
    { 480, 320,   2, 22, 130, 224, 30, 2,  135, 22, 343, 42, 4, 1, 4, 1, 3, 4, { TL_NOW, TL_FLOOR, TL_PEAK, TL_MARGIN },
      135, 68, 343, 178, true,   2, 250, 476, 40, HEAD_LG, true,  true,  0.55f,  true,  22, 2 },
    { 240, 240,   2, 22,  66, 154, 18, 1,   71, 22, 167, 30, 2, 1, 3, 1, 2, 2, { TL_NOW, TL_MARGIN },
       71, 55, 167, 121, true,   2, 178, 236, 32, HEAD_MD, true,  false, 0.36f,  true,  20, 1 },
    { 240, 135,   2, 22,  66,  87, 14, 1,   71, 22, 167, 20, 2, 1, 3, 1, 1, 2, { TL_NOW, TL_MARGIN },
       71, 45, 167,  64, true,   2, 111, 236, 22, HEAD_SM, false, false, 0.30f,  false, 16, 1 },
    { 640, 360,   2, 22, 196, 264, 34, 2,  202, 22, 436, 50, 4, 1, 4, 2, 3, 4, { TL_NOW, TL_FLOOR, TL_PEAK, TL_MARGIN },
      202, 76, 436, 210, true,   2, 290, 636, 40, HEAD_LG, true,  true,  0.60f,  true,  22, 2 },
};
// The row for this panel: its own size, or else the class it falls in.
const Lay& layFor(int w, int h) {
    for (const Lay& l : LAYS) if (l.w == w && l.h == h) return l;
    if (h <= 160) return LAYS[4];
    if (w <= 260) return LAYS[3];
    if (w >= 600) return LAYS[5];
    if (w >= 460) return LAYS[2];
    if (w >= 380) return LAYS[1];
    return LAYS[0];
}
const Lay* s_L = &LAYS[0];

// The button bar: every SquachWatch bar's spacing (8 px margins and gaps, 6 px
// off the bottom), at this layout's height.
struct Bar { int x[3], y, w, h; };
Bar barGeom(int W, int H) {
    Bar b; b.h = s_L->btH; b.y = H - b.h - 6;
    b.w = (W - 2 * 8 - 2 * 8) / 3;
    b.x[0] = 8; b.x[1] = 8 + b.w + 8; b.x[2] = b.x[1] + b.w + 8;
    return b;
}

void backdrop(TFT_eSPI& t, uint32_t now) {
    const int W = t.width(), H = t.height();
    const uint16_t g = mixc(Theme::BG, Theme::PURPLE, 26);
    for (int x = (int)((now / 120) % 32); x < W; x += 32) t.drawFastVLine(x, 20, H - 48, g);
    for (int y = 36; y < H - 28; y += 32) t.drawFastHLine(0, y, W, g);
    const int stars = 24 * W * H / 76800;
    for (int q = 0; q < stars; q++) {
        const int x = (int)(hashf(q * 11) * W), y = 22 + (int)(hashf(q * 13) * (H - 52));
        const float tw = fabsf(sinf(now / (600.0f + q * 41) + q));
        t.drawPixel(x, y, mixc(Theme::BG, Theme::WHITE, (int)(30 + 120 * tw)));
    }
}

void statusLine(TFT_eSPI& t, uint32_t now) {
    const SenseReport& s = SenseStore::last();
    const bool fresh = SenseStore::fresh(now) && s.mode == SENSE_MODE_STA;
    char b[40];
    if (fresh && (now % 1000) < 600) t.fillCircle(36, 9, 3, Theme::GREEN);
#if defined(__EMSCRIPTEN__)
    // The web emulator plays a made-up room (sim/sense_cyd_sim.cpp): it says
    // so where a board says LIVE, so nobody takes it for their own room.
    textT(t, 42, 6, "DEMO", Theme::AMBER, 1);
#else
    textT(t, 42, 6, fresh ? "LIVE" : (senseCydNetwork()[0] ? "WAIT" : "NO WIFI"), fresh ? Theme::GREEN : Theme::AMBER, 1);
#endif
    if (SenseStore::heard() && s.chan) {
        const int W = t.width();
        snprintf(b, sizeof b, "2.4 GHz ch %u  %u/s", (unsigned)s.chan, (unsigned)s.rate);
        if (84 + (int)strlen(b) * 6 > W - 52) snprintf(b, sizeof b, "ch %u %u/s", (unsigned)s.chan, (unsigned)s.rate);
        textT(t, 84, 6, b, Theme::CYAN, 1);
        if (s.rssi > -127) { snprintf(b, sizeof b, "%d dBm", (int)s.rssi); textT(t, W - 32, 6, b, Theme::VAPOR_PURPLE, 1, 2); }
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
    const Lay& L = *s_L;
    const int SP_X = L.spX, SP_Y = L.spY, SP_W = L.spW, SP_H = L.spH;
    osWindow(t, SP_X, SP_Y, SP_W, SP_H, SP_W >= 84 ? "SPECTRUM.EXE" : "SPECTRUM", Theme::BG);
    const int bins = 32, bw = (SP_W - 2) / bins < 1 ? 1 : (SP_W - 2) / bins;   // 3 px a bin on the 2.8"
    const int x0 = SP_X + 1;
    const int cy0 = SP_Y + 11, ch = L.spTop;                               // the newest spectrum
    const int wy0 = cy0 + ch + 3, rowH = L.spRowH;                         // the waterfall below it
    int rows = (SP_Y + SP_H - 9 - wy0) / rowH;                             // 108 = ~11 s on the 2.8"
    if (rows > senseCydSpecRowsMax()) rows = senseCydSpecRowsMax();
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
        if (pk > 0 && bw >= 2) {   // the peak in Hz: a rhythm in the room, not a measurement of anyone
            const int px = x0 + pk * bw + 1, py = cy0 + ch - top[pk] * ch / 255;
            t.fillCircle(px, py, 1, Theme::WHITE);
            char lb[10]; snprintf(lb, sizeof lb, "%.2fHz", pk * binHz);
            const int lw = (int)strlen(lb) * 6;
            int lx = px + 4 + lw > SP_X + SP_W - 2 ? px - 4 - lw : px + 4;
            if (lx < SP_X + 2) lx = SP_X + 2;   // a narrow window: keep it inside
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
        const int my = wy0 + (rows * rowH) * 10 / 27;   // 40 px down the 2.8"'s 108
        textT(t, SP_X + SP_W / 2, my, s.mode == SENSE_MODE_STA ? "LISTENING" : "WAITING", mixc(Theme::BG, Theme::WHITE, 140), 1, 1);
        textT(t, SP_X + SP_W / 2, my + 12, SP_W >= 72 ? "to the room" : "...", mixc(Theme::BG, Theme::WHITE, 110), 1, 1);
    }
    for (int f = 0; f <= 2; f++) {
        const int ax = x0 + (int)lroundf(f / binHz) * bw;
        if (ax > SP_X + SP_W - 4) continue;
        t.drawFastVLine(ax, ay - 2, 2, Theme::VAPOR_PURPLE);
        textT(t, f == 2 ? ax - 6 : ax, ay, f == 2 ? "2Hz" : (f ? "1" : "0"), Theme::VAPOR_PURPLE, 1);
    }
}

void tileRow(TFT_eSPI& t, uint32_t now) {
    const Tiles n = tiles();
    const Lay& L = *s_L;
    static const char* const KEYS[4] = { "NOW", "FLOOR", "PEAK", "MARGIN" };
    const int vals[4] = { n.now, n.floor, n.peak, n.margin };
    const uint16_t dimW = mixc(Theme::BG, Theme::WHITE, 150);
    const uint16_t mcol = n.margin < 0 ? dimW : (n.margin >= 30 ? Theme::GREEN : (n.margin >= 12 ? Theme::AMBER : Theme::RED));
    const Word w = stateWord(now);
    const uint16_t nowc = SenseStore::fresh(now) ? (w.c == Theme::GREEN ? Theme::WHITE : w.c) : Theme::WHITE;
    const uint16_t colz[4] = { nowc, dimW, Theme::AMBER, mcol };
    const uint16_t fill = mixc(Theme::BG, Theme::VAPOR_BLUE, 26), edge = mixc(Theme::BG, Theme::VAPOR_BLUE, 70);
    const int cols = L.tlCols ? L.tlCols : 1, rws = L.tlRows ? L.tlRows : 1;
    const int TW = (L.tlW - (cols - 1) * L.tlGap) / cols, TH = (L.tlH - (rws - 1) * L.tlGap) / rws;
    const bool inl = TH < 3 + 8 * L.tlKey + 3 + 8 * L.tlVal;   // too short to stack: side by side
    for (int i = 0; i < L.tlN && i < cols * rws; i++) {
        const int k = L.tlWhich[i];
        const int x = L.tlX + (i % cols) * (TW + L.tlGap), y = L.tlY + (i / cols) * (TH + L.tlGap), wd = TW, h = TH;
        t.fillRect(x, y, wd, h, fill); t.drawRect(x, y, wd, h, edge);
        const uint16_t cc = k == 0 ? mixc(Theme::BG, w.c, 140 + (int)(100 * sinf(now / 260.0f))) : Theme::PURPLE;
        t.drawFastHLine(x, y, 6, cc); t.drawFastVLine(x, y, 6, cc);
        t.drawFastHLine(x + wd - 6, y + h - 1, 6, cc); t.drawFastVLine(x + wd - 1, y + h - 6, 6, cc);
        if (k == 0) t.drawRect(x + 1, y + 1, wd - 2, h - 2, cc);
        char v[8];
        if (vals[k] < 0) snprintf(v, sizeof v, "--"); else if (k == 3) snprintf(v, sizeof v, "%+d", vals[k]); else snprintf(v, sizeof v, "%d", vals[k]);
        if (inl) {
            text(t, x + 4, y + (h - 8 * L.tlKey) / 2, KEYS[k], mixc(Theme::BG, Theme::WHITE, 200), fill, L.tlKey, 0);
            text(t, x + wd - 4, y + (h - 8 * L.tlVal) / 2, v, colz[k], fill, L.tlVal, 2);
        } else {
            text(t, x + wd / 2, y + 3, KEYS[k], mixc(Theme::BG, Theme::WHITE, 200), fill, L.tlKey, 1);
            text(t, x + wd / 2, y + 3 + 8 * L.tlKey + 3, v, colz[k], fill, L.tlVal, 1);
        }
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
    const Lay& L = *s_L;
    const int sx = L.stX, sy = L.stY, sw = L.stW, sh = L.stH;
    const int iy = sy + 10, ih = sh - 11, gw = sw - 2;
    const uint16_t n = SenseStore::count();
    auto idxAt = [&](int c) -> int { const int i = (int)n - STRIP_SPAN + c * STRIP_SPAN / gw; return i < 0 ? -1 : i; };
    auto yAt = [&](int sc) { return iy + ih - 3 - sc * (ih - 6) / 100; };
    const int scoreNow = n && SenseStore::stateAt(n - 1) >= SENSE_STATE_STILL ? SenseStore::scoreAt(n - 1) : 0;
    const int rateNow = SenseStore::fresh(now) ? SenseStore::last().rate : 0;
    char ttl[40]; snprintf(ttl, sizeof ttl, "MOTION.EXE  30 s  TRAFFIC %d/s", rateNow);
    if ((int)strlen(ttl) * 6 > sw - 14) snprintf(ttl, sizeof ttl, "MOTION.EXE  30 s");
    if ((int)strlen(ttl) * 6 > sw - 14) snprintf(ttl, sizeof ttl, "MOTION");
    osWindow(t, sx, sy, sw, sh, ttl, Theme::BG);
    const uint32_t sm = SenseStore::sinceMotion(now);
    const SenseStore::Router& rt = SenseStore::router();
    const char* hotName = routerHot(rt) ? CRYPTIDS[hash32(rt.tag) % 16] : nullptr;
    if (L.stRain) stripRain(t, now, sx + 1, iy, gw, ih, scoreNow, rateNow, sm != UINT32_MAX && sm < 1500, hotName);
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
    const Lay& L = *s_L;
    const Word w = stateWord(now);
    const int bx = L.bnX, bw = L.bnW, by = L.bnY, bh = L.bnH;
    const uint16_t base = mixc(Theme::BG, w.c, 30);
    t.fillRect(bx, by, bw, bh, base);
    const int sxp = (int)((now / 6) % (uint32_t)(bw + 164)) - 80;   // a light sweeping across, scanlines through it
    for (int k = 0; k < 40; k++) { const int x = bx - 2 + sxp + k; if (x > bx + 1 && x < bx + bw - 1) t.drawFastVLine(x, by + 2, bh - 4, mixc(base, w.c, 60 - abs(k - 20) * 3)); }
    for (int y = by + 3; y < by + bh - 3; y += 3) t.drawFastHLine(bx + 2, y, bw - 4, mixc(Theme::BG, w.c, 18));
    t.drawRect(bx, by, bw, bh, w.c); t.drawRect(bx + 1, by + 1, bw - 2, bh - 2, w.c);
    // The 2.8"'s MD headline sits 3 px down, as it shipped; the others are centred.
    const int asc = L.bnHead == HEAD_LG ? 31 : L.bnHead == HEAD_MD ? 27 : 16;
    const int hy = L.bnHead == HEAD_MD ? by + 3 : by + ((bh - asc) / 2 > 1 ? (bh - asc) / 2 : 1);
    headline(t, bx + 7, hy, w.w, w.c, glitchFor(w, now), L.bnHead);
    const SenseReport& s = SenseStore::last();
    const uint32_t since = SenseStore::sinceMotion(now);
    const int right = L.bnSquachy ? bx + bw - 40 : bx + bw - 6;   // 278 on the 2.8": clear of Squachy
    const int rx = bx + 7 + headlineWidth(w.w, L.bnHead) + 14, ry = by + bh / 2;
    if (!L.bnSub) {
        // no line beside the headline
    } else if (SenseStore::fresh(now) && s.presence && s.state != SENSE_STATE_MOTION && since != UINT32_MAX) {
        const float left = 1.0f - clampf(since / (float)HOLD_MS, 0, 1);
        arc(t, rx, ry, 9, 0, 6.2832f, mixc(Theme::BG, Theme::PURPLE, 90), 3);
        arc(t, rx, ry, 9, -1.5708f, -1.5708f + 6.2832f * left, Theme::VAPOR_PINK, 3);
        subLine(t, rx + 14, ry, w.sub, Theme::VAPOR_PINK, right);
    } else {
        subLine(t, rx - 8, ry, w.sub, Theme::CYAN, right);
    }
    if (!L.bnSquachy) return;
    using VP = Squachy::VisitPose;
    static Startle startle;
    const bool moving = SenseStore::fresh(now) && s.state == SENSE_STATE_MOTION;
    const VP pose = startle(now, moving) ? VP::STARTLED
                  : moving ? VP::POINT
                  : s.presence ? VP::CROUCH
                  : (since == UINT32_MAX || since > 120000) ? VP::SLEEPY : VP::NONE;
    // He pops up out of the band at the right.
    Squachy::drawWaving(t, bx + bw - 22, by + bh - 2, now, L.bnSq, nullptr, false, 0, false, 34, false, false, false, pose);
}

void buttons(TFT_eSPI& t) {
    static const char* const LB[3] = { "[ BACK ]", "[ RECAL ]", "[ OFF ]" };
    if (!s_L->btOn) {   // no touch screen: what the keys do, under the banner
        const int y = s_L->bnY + s_L->bnH + 1;
        if (y + 8 <= t.height()) textT(t, t.width() / 2, y, "PRESS: RECAL   HOME: LEAVE", mixc(Theme::BG, Theme::WHITE, 120), 1, 1);
        return;
    }
    const Bar g = barGeom(t.width(), t.height());
    for (int k = 0; k < 3; k++) Theme::drawButton(t, g.x[k], g.y, g.w, g.h, LB[k], false, s_L->btLabel);
}

}  // namespace

void uiSensingInit(TFT_eSPI& t) {
    s_L = &layFor(t.width(), t.height());
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
    s_enterMs = s_lastMs = millis();
    s_lastWord = nullptr;
}

void uiSensingTick(TFT_eSPI& t, uint32_t now, bool advance) {
    (void)advance;
    const uint32_t dt = s_lastMs && now > s_lastMs ? (now - s_lastMs > 200 ? 200 : now - s_lastMs) : 16;
    s_lastMs = now;
    s_L = &layFor(t.width(), t.height());   // the panel can turn while this is up
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
    s_L = &layFor(screenW, screenH);
    const Bar g = barGeom(screenW, screenH);
    if (s_L->btOn && y >= g.y - 2 && y <= g.y + g.h + 4) {
        for (int i = 0; i < 3; i++) if (x >= g.x[i] && x <= g.x[i] + g.w) {
            if (i == 0) return SensingTap::BACK;
            if (i == 1) return SensingTap::RECAL;
            return SensingTap::OFF;
        }
        return SensingTap::NONE;
    }
    // No saved network: the banner is the way to add one. With no button bar
    // (the boards with no touch screen), a press on the banner is RECAL.
    if (y >= s_L->bnY && y < s_L->bnY + s_L->bnH) {
        if (!senseCydNetwork()[0]) return SensingTap::ADD_WIFI;
        if (!s_L->btOn) return SensingTap::RECAL;
    }
    return SensingTap::NONE;
}
void uiSensingBannerStop(int screenW, int screenH, int16_t& x, int16_t& y) {
    s_L = &layFor(screenW, screenH);
    x = (int16_t)(s_L->bnX + s_L->bnW / 2);
    y = (int16_t)(s_L->bnY + s_L->bnH / 2);
}
#endif  // SENSE_CYD_APP
