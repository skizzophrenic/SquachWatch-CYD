// SquachWatch-CYD -- HUMAN SENSING's screens on the M5Stack Tab5 (ui_sensing_cyd.h).
//
// The Tab5 sensing app's own screens (I:/GitHub/Tab5-Monster-C5-CSI-Human-
// Sensing, src/app/ui_sensing.cpp), brought into SquachWatch whole when the
// two were merged (2026-11-17): SquachWatch's smaller-board screens, scaled up
// to the Tab5, lost the DISH radar, the PATHS / EVENTS / RADIO panel, Squachy
// on the moving path, SNEAK's cameras and high scores and the big VITALS. The
// data now comes from SquachWatch's engine (sense_tab5.cpp) through
// SenseStore and Vitals, by the thin Tab5Sense adapter below, and the bar is
// SquachWatch's three: BACK, the page's action, SLEEP (SNEAK: LEVEL).
//
// Drawn for the Tab5's 640x360 logical frame (8-bit colour: no alpha, so a
// glow is a stepped blend toward the background, never a transparency).
// Designs: the direction-check page (2026-11-07), then ten more passes on
// SCOPE and SNEAK and five on SENSING, each rendered from this file in the
// emulator. P(n) gates a pass so the emulator can show any one of them; on
// the board every pass is on and the gate compiles away.
#include "sense_cyd.h"
#if defined(SENSE_CYD_APP) && (defined(TAB5) || defined(SENSE_TAB5_SIM))   // SENSE_TAB5_SIM: the emulator (sim/build_tab5_sim.sh)
#include "ui_sensing_cyd.h"
#include "sense_store.h"
#include "sense_vitals.h"
#include "theme.h"
#include "squachy.h"
#include "settings.h"
#include <Arduino.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#if defined(TAB5)
#include <esp_heap_caps.h>
#endif

// ---- The adapter: the sensing app's data calls, onto SquachWatch's ----------
namespace Tab5Sense {
static const uint8_t PATHS = SenseStore::PATHS;
using Path  = SenseStore::Router;
using Event = SenseStore::Event;
inline bool fresh(uint32_t now) { return SenseStore::fresh(now); }
inline bool heard() { return SenseStore::heard(); }
inline const SenseReport& last() { return SenseStore::last(); }
inline uint32_t lastAt() { return SenseStore::lastAt(); }
inline uint32_t sinceMotion(uint32_t now) { return SenseStore::sinceMotion(now); }
inline uint16_t count() { return SenseStore::count(); }
inline uint8_t scoreAt(uint16_t i) { return SenseStore::scoreAt(i); }
inline uint8_t stateAt(uint16_t i) { return SenseStore::stateAt(i); }
inline bool presenceAt(uint16_t i) { return SenseStore::presenceAt(i); }
inline const Path& path(uint8_t i) { return SenseStore::path(i); }
inline uint8_t livePaths(uint32_t) { return SenseStore::livePaths(); }
inline uint8_t events(Event* out, uint8_t cap) { return SenseStore::events(out, cap); }
inline const char* info() { return senseCydRadioInfo()[0] ? senseCydRadioInfo() : "the Tab5's own radio (C6)"; }
inline bool monster() { return !strcmp(senseCydRadio(), "MONSTER C5"); }
inline const char* ghz(const SenseReport& s) { return s.band == 5 ? "5" : "2.4"; }
inline uint32_t lines() { return SenseStore::statuses(); }
inline uint32_t badLines() { return 0; }
}  // namespace Tab5Sense

namespace {

// The app's VITALS link state, from what SquachWatch knows.
enum class VLink : uint8_t { OFF, ASKING, JOINING, LIVE, NO_SIGNAL };
VLink vitalsLink(uint32_t now) {
    if (Vitals::metrics().live) return VLink::LIVE;
    if (!SenseStore::fresh(now)) return SenseStore::heard() ? VLink::NO_SIGNAL : VLink::ASKING;
    return senseModeLive(SenseStore::last().mode) ? VLink::ASKING : VLink::JOINING;
}

constexpr uint8_t s_pass = 255;
inline bool P(uint8_t n) { return s_pass >= n; }

const uint8_t  MOTION_LINE = 28;      // the radio's MOTION threshold (csi_motion_model.h)
const uint32_t HOLD_MS     = 15000;   // the radio's PRESENCE hold
enum Mode : uint8_t { M_SENSING, M_SCOPE, M_SNEAK, M_VITALS, M_COUNT };

uint8_t  s_mode = M_SENSING;
uint8_t  s_tab = 0;                    // SENSING's list: 0 PATHS, 1 EVENTS, 2 RADIO
uint32_t s_enterMs = 0, s_lastMs = 0;
const char* s_lastWord = nullptr;
uint32_t s_wordAt = 0;

// ---- helpers ------------------------------------------------------------------
inline uint16_t mixc(uint16_t a, uint16_t b, int t256) { if (t256 < 0) t256 = 0; if (t256 > 256) t256 = 256; return Theme::blend(a, b, (uint16_t)t256); }
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
uint32_t hash32(uint32_t n) { n ^= 61; n ^= n >> 16; n += n << 3; n ^= n >> 4; n *= 0x27d4eb2d; n ^= n >> 15; return n; }
inline float hashf(uint32_t n) { return (hash32(n) & 0xFFFFFF) / 16777216.0f; }
uint32_t s_rng = 0x5EED;
inline float rnd() { s_rng = s_rng * 1664525u + 1013904223u; return (s_rng >> 8) / 16777216.0f; }

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
void headline(TFT_eSPI& t, int x, int y, const char* s, uint16_t col, uint8_t align, int glitch) {
    const int w = Theme::bangersTextWidth(s, Theme::BangersSize::LG);
    const int lx = align == 1 ? x - w / 2 : (align == 2 ? x - w : x);
    if (glitch > 0) {
        Theme::drawBangersText(t, lx - glitch, y, s, Theme::CYAN, Theme::BangersSize::LG);
        Theme::drawBangersText(t, lx + glitch, y + 1, s, Theme::PINK, Theme::BangersSize::LG);
    }
    Theme::drawBangersOutline(t, lx, y, s, Theme::BLACK, Theme::BangersSize::LG, 2);
    Theme::drawBangersText(t, lx, y, s, col, Theme::BangersSize::LG);
}
// Arcs as chords (TFT_eSPI::drawArc and the emulator's disagree on 0 degrees).
void arc(TFT_eSPI& t, int cx, int cy, int r, float a0, float a1, uint16_t col, int thick = 1) {
    if (a1 < a0) return;
    const int steps = 8 + (int)((a1 - a0) * r / 6);
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
// A vaporwave window frame: gradient title strip with a name, border, body.
void osWindow(TFT_eSPI& t, int x, int y, int w, int h, const char* name, uint16_t body) {
    t.fillRect(x, y, w, h, body);
    for (int i = 0; i < w; i++) {
        const int f = i * 512 / w;
        const uint16_t c = f < 256 ? mixc(Theme::PURPLE, Theme::CYAN, f) : mixc(Theme::CYAN, Theme::PINK, f - 256);
        t.drawFastVLine(x + i, y, 10, mixc(Theme::BG, c, 150));
    }
    textT(t, x + 4, y + 1, name, Theme::WHITE, 1);
    t.drawRect(x, y, w, h, Theme::PURPLE);
    t.fillRect(x + w - 10, y + 2, 6, 6, Theme::BG); t.drawRect(x + w - 10, y + 2, 6, 6, Theme::WHITE);
}

// A startle is a moment, not a state: true for 1.2 s when motion starts after
// 20 s of quiet, and no more than once every 30 s. Without this he wore the
// shocked face for as long as anyone kept moving. One slot per screen.
struct Startle {
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

void squachy(TFT_eSPI& t, int8_t outfit, Squachy::VisitPose pose, int cx, int feetY, float scale, uint32_t now, const char* line = nullptr) {
    if (outfit >= 0) Squachy::setOutfitPreview(outfit);
    Squachy::drawWaving(t, cx, feetY, now, scale, line, line != nullptr, 0, false, 34, false, false, line != nullptr, pose);
    if (outfit >= 0) Squachy::setOutfitPreview(-1);
}

// Every transmitter keeps one outfit and one name, from its address (the
// LORA page names its nodes the same way). The names are the owner's own
// picks: 90s Nickelodeon (Rocko, Real Monsters, Ren & Stimpy, Hey Arnold,
// Pete & Pete, Doug) and Fallout.
inline int8_t pathOutfit(uint16_t tag) { return (int8_t)(1 + tag % 19); }
const char* const CRYPTIDS[16] = { "HEFFER", "ROCKO", "KRUMM", "GROMBLE", "ICKIS", "OBLINA", "STIMPY", "HELGA",
                                   "ARTIE", "DOUG", "DOGMEAT", "CODSWORTH", "VAULT BOY", "DEATHCLAW", "MR HANDY", "BRAHMIN" };
const char* pathName(uint8_t k) {
    const Tab5Sense::Path& p = Tab5Sense::path(k);
    if (!P(6) || !p.live) { static char ab[4][8]; snprintf(ab[k & 3], 8, "PATH %c", 'A' + k); return ab[k & 3]; }
    // Four slots can never share a name: step past any the other slots took.
    uint8_t idx = (uint8_t)(hash32(p.tag) % 16);
    for (int tries = 0; tries < 16; tries++) {
        bool taken = false;
        for (uint8_t j = 0; j < k; j++) { const Tab5Sense::Path& q = Tab5Sense::path(j); if (q.live && (hash32(q.tag) % 16) == idx) taken = true; }
        if (!taken) break;
        idx = (uint8_t)((idx + 1) % 16);
    }
    return CRYPTIDS[idx];
}
const uint16_t* pathColour(uint8_t k) {
    static const uint16_t* const C[4] = { &Theme::CYAN, &Theme::VAPOR_PINK, &Theme::VAPOR_YELLOW, &Theme::GREEN };
    return C[k & 3];
}
inline bool pathHot(const Tab5Sense::Path& p) { return p.live && p.score >= MOTION_LINE && p.cal >= 100; }

struct Word { const char* w; uint16_t c; char sub[48]; };
Word stateWord(uint32_t now) {
    Word r;
    const SenseReport& s = Tab5Sense::last();
    r.sub[0] = 0;
    if (!Tab5Sense::heard()) { r.w = "NO RADIO"; r.c = Theme::AMBER; snprintf(r.sub, sizeof r.sub, now - s_enterMs < 3000 ? "waking the radio" : "the Tab5's radio is not answering"); }
    else if (!Tab5Sense::fresh(now)) { r.w = "RADIO LOST"; r.c = Theme::AMBER; snprintf(r.sub, sizeof r.sub, "nothing for %lu s", (unsigned long)((now - Tab5Sense::lastAt()) / 1000)); }
    else if (s.state == SENSE_STATE_CAL) { r.w = "PLEASE STAND BY"; r.c = Theme::AMBER; snprintf(r.sub, sizeof r.sub, "calibrating the room %u%%", (unsigned)s.cal); }
    else if (s.state == SENSE_STATE_LOWDATA) { r.w = "LOW SIGNAL"; r.c = Theme::AMBER; snprintf(r.sub, sizeof r.sub, "too little WiFi on ch %u", (unsigned)s.chan); }
    else if (s.presence) {
        r.w = "PRESENCE"; r.c = Theme::VAPOR_PINK;
        const uint32_t since = Tab5Sense::sinceMotion(now);
        if (s.state == SENSE_STATE_MOTION || since < 1500) snprintf(r.sub, sizeof r.sub, "something is moving");
        else if (since != UINT32_MAX) snprintf(r.sub, sizeof r.sub, "last movement %lu s ago", (unsigned long)(since / 1000));
        else snprintf(r.sub, sizeof r.sub, "holding after movement");
    } else { r.w = "CLEAR"; r.c = Theme::GREEN; snprintf(r.sub, sizeof r.sub, "the room is still"); }
    return r;
}
int glitchFor(const Word& w, uint32_t now) {
    if (w.w != s_lastWord) { s_lastWord = w.w; s_wordAt = now; }
    const uint32_t age = now - s_wordAt;
    return age < 420 ? (int)(6 - age / 70) : 0;
}

void modeDots(TFT_eSPI& t) {
    for (uint8_t k = 0; k < M_COUNT; k++) {
        const int x = 320 - (M_COUNT - 1) * 7 + k * 14;
        if (k == s_mode) t.fillCircle(x, 8, 4, Theme::VAPOR_PINK);
        else t.fillCircle(x, 8, 3, mixc(Theme::BG, Theme::PURPLE, 110));
    }
}
void buttons(TFT_eSPI& t) {
    const Theme::ButtonBarGeom g = Theme::computeButtonBar(t.width(), t.height());
    static const char* const SENSE[3] = { "[ BACK ]", "[ RECAL ]", "[ SLEEP ]" };
    static const char* const SNEAKB[3] = { "[ BACK ]", "[ START RUN ]", "[ LEVEL ]" };
    static const char* const VITB[3] = { "[ BACK ]", "[ NEW SESSION ]", "[ SLEEP ]" };
    const char* const* l = s_mode == M_SNEAK ? SNEAKB : (s_mode == M_VITALS ? VITB : SENSE);
    // The page may leave a free font selected; drawButton measures the first
    // label with whatever is set, and BACK came out small on VITALS.
    t.setTextFont(1);   // also drops a free font (gfxFont = NULL)
    for (int k = 0; k < 3; k++) Theme::drawButton(t, g.x[k], g.y, g.w[k], g.h, l[k], false);
}

struct Tiles { int now, floor, peak, margin; };
Tiles tiles() {
    Tiles r = { -1, -1, -1, -1 };
    const uint16_t n = Tab5Sense::count();
    if (!n) return r;
    if (Tab5Sense::stateAt(n - 1) >= SENSE_STATE_STILL) r.now = Tab5Sense::scoreAt(n - 1);
    uint16_t hist[33] = {0}; uint16_t still = 0;
    for (uint16_t i = 0; i < n; i++) {
        const uint8_t st = Tab5Sense::stateAt(i), sc = Tab5Sense::scoreAt(i);
        if (st >= SENSE_STATE_STILL && (int)sc > r.peak) r.peak = sc;
        if (st == SENSE_STATE_STILL) { hist[sc / 3 > 32 ? 32 : sc / 3]++; still++; }
    }
    if (still) { uint16_t acc = 0; for (int b = 0; b <= 32; b++) { acc += hist[b]; if (acc * 2 >= still) { r.floor = b * 3 + 1; break; } } }
    if (r.peak >= 0 && r.floor >= 0) r.margin = r.peak - r.floor;
    return r;
}

// ============================================================================
// SENSING
// ============================================================================
const int ST_X = 196, ST_Y = 82, ST_W = 434, ST_H = 92;
const int GL_W = ST_W - 2;
const int STRIP_SPAN = 300;          // samples in MOTION.EXE: 30 s at the radio's 10 a second
uint8_t* s_phos = nullptr;           // the LORA page's PHOSPHOR afterglow, sized GL_W x ST_H
uint16_t s_beamIdx = 0;
struct Spark { float x, y, vx, vy; int16_t life, full; };
Spark s_sparks[40];

void backdrop(TFT_eSPI& t, uint32_t now) {
    const uint16_t g = mixc(Theme::BG, Theme::PURPLE, 26);
    for (int x = 0; x < 640; x += 32) t.drawFastVLine(x + (int)((now / 120) % 32), 18, 314, g);
    for (int y = 30; y < 332; y += 32) t.drawFastHLine(0, y, 640, g);
    for (int q = 0; q < 40; q++) {
        const int x = (int)(hashf(q * 11) * 640), y = 20 + (int)(hashf(q * 13) * 300);
        const float tw = fabsf(sinf(now / (600.0f + q * 41) + q));
        t.drawPixel(x, y, mixc(Theme::BG, Theme::WHITE, (int)(30 + 120 * tw)));
    }
}

// SPECTRUM.EXE, where the app had DISH.EXE (asked for 2026-11-17: "a way
// better graphic"). The same picture as the other boards' (ui_sensing_cyd.cpp):
// how fast the room's readings swing, 0-2.4 Hz across, the newest spectrum as
// bars on top and the last ~11 s falling below it as a waterfall. The engine
// builds the rows (sense_tab5.cpp) from the radio's ten lines a second.
uint16_t spRamp(uint8_t v) {
    if (v < 64)  return mixc(Theme::BG, mixc(Theme::BG, Theme::PURPLE, 170), v * 4);
    if (v < 128) return mixc(mixc(Theme::BG, Theme::PURPLE, 170), Theme::PINK, (v - 64) * 4);
    if (v < 192) return mixc(Theme::PINK, Theme::VAPOR_YELLOW, (v - 128) * 4);
    return mixc(Theme::VAPOR_YELLOW, Theme::WHITE, (v - 192) * 4);
}
void spectrumPanel(TFT_eSPI& t) {
    const int SP_X = 6, SP_Y = 20, SP_W = 186, SP_H = 158;
    osWindow(t, SP_X, SP_Y, SP_W, SP_H, "SPECTRUM.EXE", Theme::BG);
    const int bins = 32, bw = (SP_W - 2) / bins;                  // 5 px a bin
    const int x0 = SP_X + 1 + ((SP_W - 2) - bins * bw) / 2;
    const int cy0 = SP_Y + 12, ch = 30;                           // the newest spectrum
    const int wy0 = cy0 + ch + 3, rowH = 1;                       // the waterfall below it
    int rows = (SP_Y + SP_H - 10 - wy0) / rowH;
    if (rows > senseCydSpecRowsMax()) rows = senseCydSpecRowsMax();
    const int ay = wy0 + rows * rowH + 2;                         // the Hz axis
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
            const int px = x0 + pk * bw + bw / 2, py = cy0 + ch - top[pk] * ch / 255;
            t.fillCircle(px, py, 1, Theme::WHITE);
            char lb[10]; snprintf(lb, sizeof lb, "%.2fHz", pk * binHz);
            const int lw = (int)strlen(lb) * 6;
            int lx = px + 4 + lw > SP_X + SP_W - 2 ? px - 4 - lw : px + 4;
            if (lx < SP_X + 2) lx = SP_X + 2;
            textT(t, lx, py - 4 < cy0 ? cy0 : py - 4, lb, Theme::WHITE, 1);
        }
    }
    t.drawFastHLine(SP_X + 1, cy0 + ch + 1, SP_W - 2, mixc(Theme::BG, Theme::PURPLE, 120));
    const int n = senseCydSpecRows();
    for (int r = 0; r < rows && r < n; r++) {
        const uint8_t* row = senseCydSpecRow(r);
        if (!row) break;
        for (int b = 1; b < bins; b++) if (row[b] > 5) t.fillRect(x0 + b * bw, wy0 + r * rowH, bw, rowH, spRamp(row[b]));
    }
    if (!n) {   // nothing yet: the first spectrum needs 12.8 s of readings
        const SenseReport& s = Tab5Sense::last();
        const int my = wy0 + rows * rowH / 3;
        textT(t, SP_X + SP_W / 2, my, senseModeLive(s.mode) ? "LISTENING" : "WAITING", mixc(Theme::BG, Theme::WHITE, 140), 1, 1);
        textT(t, SP_X + SP_W / 2, my + 12, "to the room", mixc(Theme::BG, Theme::WHITE, 110), 1, 1);
    }
    for (int f = 0; f <= 2; f++) {
        const int ax = x0 + (int)lroundf(f / binHz) * bw;
        if (ax > SP_X + SP_W - 4) continue;
        t.drawFastVLine(ax, ay - 2, 2, Theme::VAPOR_PURPLE);
        textT(t, f == 2 ? ax - 12 : ax, ay, f == 2 ? "2Hz" : (f ? "1" : "0"), Theme::VAPOR_PURPLE, 1);
    }
}

void tileRow(TFT_eSPI& t, uint32_t now) {
    const Tiles n = tiles();
    static const char* const KEYS[4] = { "NOW", "FLOOR", "PEAK", "MARGIN" };
    const int vals[4] = { n.now, n.floor, n.peak, n.margin };
    const uint16_t dimW = mixc(Theme::BG, Theme::WHITE, 150);
    const uint16_t mcol = n.margin < 0 ? dimW : (n.margin >= 30 ? Theme::GREEN : (n.margin >= 12 ? Theme::AMBER : Theme::RED));
    const Word w = stateWord(now);
    const uint16_t nowc = P(9) && Tab5Sense::fresh(now) ? (w.c == Theme::GREEN ? Theme::WHITE : w.c) : Theme::WHITE;
    const uint16_t cols[4] = { nowc, dimW, Theme::AMBER, mcol };
    const uint16_t fill = mixc(Theme::BG, Theme::VAPOR_BLUE, 26), edge = mixc(Theme::BG, Theme::VAPOR_BLUE, 70);
    for (int k = 0; k < 4; k++) {
        const int x = 196 + k * 110, y = 20, wd = 104, h = 58;
        t.fillRect(x, y, wd, h, fill); t.drawRect(x, y, wd, h, edge);
        if (P(9)) {   // corner ticks; the NOW tile's border breathes in the state colour
            const uint16_t cc = k == 0 ? mixc(Theme::BG, w.c, 140 + (int)(100 * sinf(now / 260.0f))) : Theme::PURPLE;
            t.drawFastHLine(x, y, 10, cc); t.drawFastVLine(x, y, 10, cc);
            t.drawFastHLine(x + wd - 10, y + h - 1, 10, cc); t.drawFastVLine(x + wd - 1, y + h - 10, 10, cc);
            if (k == 0) t.drawRect(x + 1, y + 1, wd - 2, h - 2, cc);
        }
        text(t, x + wd / 2, y + 5, KEYS[k], mixc(Theme::BG, Theme::WHITE, 200), fill, 1, 1);
        char v[8];
        if (vals[k] < 0) snprintf(v, sizeof v, "--"); else if (k == 3) snprintf(v, sizeof v, "%+d", vals[k]); else snprintf(v, sizeof v, "%d", vals[k]);
        text(t, x + wd / 2, y + 19, v, cols[k], fill, 4, 1);
    }
}

// vaporwave glyph rain inside the strip, driven by the air itself: how many
// columns run is the radio's traffic (CSI samples a second: a quiet channel
// leaves a few sparse drops, a busy one fills the strip), how fast and how
// long they fall is the motion score, and it runs pink above the MOTION line.
void stripRain(TFT_eSPI& t, uint32_t now, int x0, int y0, int w, int h, int score, int rate, bool surge, const char* word) {
    static const char GLYPHS[] = "01ABCDEFGHIJKLMNOPQRSTUVWXYZ!@#$%^&*<>{}[]/|+=~SASQUACH";
    const int GLN = sizeof(GLYPHS) - 1, cw = 7, ch = 9;
    const int cols = w / cw, rows = h / ch;
    const float speed = 0.004f + score / 9000.0f;          // rows per ms
    t.setTextFont(1); t.setTextSize(1); t.setTextWrap(false);
    const float busy = clampf(0.08f + rate / 160.0f + (surge ? 0.25f : 0.0f), 0.08f, 0.6f);    // share of columns that rain
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
            uint16_t col;
            const uint16_t hue = hot ? Theme::PINK : (h0 > 0.55f ? Theme::GREEN : Theme::CYAN);
            if (d < 1) col = mixc(Theme::BG, hue, 120);
            else { const int a = 80 - (int)(d * 64 / trail); col = mixc(Theme::BG, hue, a < 14 ? 14 : a); }
            t.setTextColor(col);
            char gch = GLYPHS[(size_t)g];
            if (c == wordCol) {   // the hot path's name, drifting down one column
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
    if (!P(9)) { t.fillRect(sx, sy, sw, sh, Theme::BG); t.drawRect(sx, sy, sw, sh, mixc(Theme::BG, Theme::VAPOR_BLUE, 70)); }
    const int iy = sy + (P(9) ? 11 : 1), ih = sh - (P(9) ? 12 : 2);
    const uint16_t n = Tab5Sense::count();
    // The last 30 s (the tiles still read the whole 80): at 10 samples a
    // second that is ~15 px a second across the strip, a trace you can see move.
    const int span = STRIP_SPAN;
    auto idxAt = [&](int c) -> int { const int i = (int)n - span + c * span / (sw - 2); return i < 0 ? -1 : i; };
    auto yAt = [&](int sc) { return iy + ih - 3 - sc * (ih - 6) / 100; };
    const int scoreNow = n && Tab5Sense::stateAt(n - 1) >= SENSE_STATE_STILL ? Tab5Sense::scoreAt(n - 1) : 0;
    const int rateNow = Tab5Sense::fresh(now) ? Tab5Sense::last().rate : 0;
    if (P(9)) {   // the window's own title says how busy the air is
        char ttl[40]; snprintf(ttl, sizeof ttl, "MOTION.EXE  30 s  TRAFFIC %d/s", rateNow);
        osWindow(t, sx, sy, sw, sh, ttl, Theme::BG);
    }
    if (P(7)) {
        const uint32_t sm = Tab5Sense::sinceMotion(now);
        const char* hotName = nullptr;
        for (uint8_t k = 0; k < Tab5Sense::PATHS; k++) if (pathHot(Tab5Sense::path(k))) hotName = pathName(k);
        stripRain(t, now, sx + 1, iy, sw - 2, ih, scoreNow, rateNow, sm != UINT32_MAX && sm < 1500, hotName);
    }
    else if (P(2)) {
        const int cw = 7, rows = 15, cols = (sw - 2) / cw, drift = (int)(n / 15);
        for (int c = 0; c < cols; c++) {
            const int i = idxAt(c * cw); const int sc = i < 0 ? 0 : Tab5Sense::scoreAt((uint16_t)i);
            for (int r = 0; r < rows; r++) {
                const float h0 = hashf((uint32_t)((c + drift) * 977 + r * 31));
                const float tw = 0.5f + 0.5f * sinf(now / (300.0f + h0 * 900.0f) + h0 * 40.0f);
                const bool warm = r >= rows - (sc * rows / 100);
                const uint16_t base = warm ? Theme::PINK : (h0 > 0.6f ? Theme::PURPLE : Theme::VAPOR_BLUE);
                t.fillRect(sx + 1 + c * cw, iy + r * ih / rows, cw - 2, ih / rows - 1, mixc(Theme::BG, base, (int)(14 + 26 * tw * h0 + (warm ? 70 : 0))));
            }
        }
    }
    const int ty = yAt(MOTION_LINE);
    const uint32_t total = Tab5Sense::lines();     // every line ever heard: the grid's clock
    if (P(10)) {
        for (int c = 0; c < sw - 2; c++) {
            const int i = idxAt(c); if (i < 0) continue;
            const int i2 = idxAt(c + 1);
            const uint32_t g = total - (uint32_t)(n - 1 - i), g2 = i2 < 0 ? g : total - (uint32_t)(n - 1 - i2);
            if (g / 50 != g2 / 50) t.drawFastVLine(sx + 1 + c, iy, ih, mixc(Theme::BG, Theme::PURPLE, (g2 / 50) % 2 ? 70 : 110));
        }
    }
    {   // the MOTION line marches, so the threshold reads as live
        const int off = (int)((now / 70) % 7);
        for (int x = sx + 1 + off; x < sx + sw - 1; x += 7) t.drawFastHLine(x, ty, 3, Theme::AMBER);
    }

    const uint32_t since = Tab5Sense::sinceMotion(now);
    if (since != UINT32_MAX && since < (uint32_t)span * 100)
        for (int e = 1; e <= 3; e++) {
            const int xg = sx + sw - 2 - (int)(since * (sw - 2) / ((uint32_t)span * 100)) - e * 22;
            if (xg > sx) t.drawFastVLine(xg, ty - 18 / e, iy + ih - 3 - (ty - 18 / e), mixc(Theme::BG, Theme::VAPOR_PINK, 120 / e));
        }
    if (!P(8)) {
        int px = -1, py = 0;
        for (int c = 0; c < sw - 2; c += 2) {
            const int i = idxAt(c); if (i < 0) continue;
            const int y = yAt(Tab5Sense::scoreAt((uint16_t)i));
            if (px >= 0) { t.drawLine(px, py, sx + 1 + c, y, Theme::CYAN); t.drawLine(px, py + 1, sx + 1 + c, y + 1, Theme::CYAN); }
            px = sx + 1 + c; py = y;
        }
        return;
    }
    // PHOSPHOR, as on the LORA page: a dim solid line, an afterglow layer the
    // beam redraws as it sweeps, a gliding white-hot head, sparks with weight.
    const int gw = sw - 2, gh = ih;
    if (!s_phos) {
#if defined(TAB5)
        s_phos = (uint8_t*)heap_caps_calloc((size_t)GL_W * ST_H, 1, MALLOC_CAP_SPIRAM);
#else
        s_phos = (uint8_t*)calloc((size_t)GL_W * ST_H, 1);
#endif
    }
    for (int c = 0; c < gw; c++) {
        const int i = idxAt(c); if (i < 0) continue;
        const int y = yAt(Tab5Sense::scoreAt((uint16_t)i));
        const bool pres = Tab5Sense::presenceAt((uint16_t)i);
        const uint16_t base = pres ? Theme::VAPOR_PINK : Theme::VAPOR_BLUE;
        t.drawFastVLine(sx + 1 + c, y + 1, iy + ih - 2 - y, mixc(Theme::BG, base, pres ? 70 : 45));
        // heat rising through the fill: bands drifting upward, a little out of
        // step column to column so it ripples rather than strobes
        const int phase = (int)((now / 45 + c / 3) % 9);
        for (int yy = iy + ih - 3 - phase; yy > y + 1; yy -= 9) t.drawFastHLine(sx + 1 + c, yy, 1, mixc(Theme::BG, base, pres ? 120 : 80));
    }
    // peaks over the MOTION line get a pulsing marker: the moments that counted
    {
        int lastPk = -100;
        for (int c = 2; c < gw - 2; c++) {
            const int i0 = idxAt(c - 2), i1 = idxAt(c), i2 = idxAt(c + 2);
            if (i0 < 0 || i1 < 0 || i2 < 0) continue;
            const int v0 = Tab5Sense::scoreAt((uint16_t)i0), v1 = Tab5Sense::scoreAt((uint16_t)i1), v2 = Tab5Sense::scoreAt((uint16_t)i2);
            if (v1 < MOTION_LINE || v1 < v0 || v1 < v2 || c - lastPk < 24) continue;
            lastPk = c;
            const int px = sx + 1 + c, py = yAt(v1) - 6;
            const int r = 2 + (int)((now / 120 + c) % 4);
            t.drawLine(px, py - r, px + r, py, Theme::VAPOR_PINK); t.drawLine(px + r, py, px, py + r, Theme::VAPOR_PINK);
            t.drawLine(px, py + r, px - r, py, Theme::VAPOR_PINK); t.drawLine(px - r, py, px, py - r, Theme::VAPOR_PINK);
        }
    }
    int lpx = -1, lpy = 0;
    for (int c = 0; c < gw; c += 2) {
        const int i = idxAt(c); if (i < 0) continue;
        const int y = yAt(Tab5Sense::scoreAt((uint16_t)i));
        if (lpx >= 0) { t.drawLine(lpx, lpy, sx + 1 + c, y, Theme::CYAN); t.drawLine(lpx, lpy + 1, sx + 1 + c, y + 1, mixc(Theme::BG, Theme::CYAN, 170)); }
        lpx = sx + 1 + c; lpy = y;
    }
    if (!s_phos || n < 2) return;
    {
        static uint32_t lastTotal = 0; static float carry = 0;
        const uint32_t tot = Tab5Sense::lines();
        if (lastTotal && tot > lastTotal) {
            carry += (tot - lastTotal) * (float)gw / STRIP_SPAN;
            const int shift = (int)carry;
            if (shift > 0) {
                carry -= shift;
                for (int y = 0; y < gh; y++) {
                    uint8_t* row = s_phos + y * gw;
                    if (shift >= gw) memset(row, 0, gw);
                    else { memmove(row, row + shift, gw - shift); memset(row + gw - shift, 0, shift); }
                }
            }
        }
        lastTotal = tot;
    }
    const uint16_t k8 = (uint16_t)(expf(-(float)dt / 350.0f) * 256);
    for (int i = 0; i < gw * gh; i++) if (s_phos[i]) s_phos[i] = (uint8_t)((s_phos[i] * k8) >> 8);
    auto plot = [&](int x, int y, uint8_t v) { y -= iy; if (x >= 0 && x < gw && y >= 0 && y < gh && s_phos[y * gw + x] < v) s_phos[y * gw + x] = v; };
    const uint32_t SWEEP = (uint32_t)(2600 - clampf(rateNow / 60.0f, 0, 1) * 1400);   // 2.6 s quiet, 1.2 s busy
    const int col = (int)((now % SWEEP) * (uint32_t)(gw - 1) / SWEEP);
    int from = s_beamIdx <= col ? s_beamIdx : 0;
    if (col - from > 30) from = col - 30;
    for (int c = from; c < col; c++) {
        const int ia = idxAt(c), ib = idxAt(c + 1); if (ia < 0 || ib < 0) continue;
        int xa = c, ya = yAt(Tab5Sense::scoreAt((uint16_t)ia)); const int xb = c + 1, yb = yAt(Tab5Sense::scoreAt((uint16_t)ib));
        const int ddx = abs(xb - xa), ddy = -abs(yb - ya), stx = xa < xb ? 1 : -1, sty = ya < yb ? 1 : -1;
        int err = ddx + ddy;
        for (;;) { plot(xa, ya, 170); plot(xa, ya + 1, 150); if (xa == xb && ya == yb) break; const int e2 = 2 * err; if (e2 >= ddy) { err += ddy; xa += stx; } if (e2 <= ddx) { err += ddx; ya += sty; } }
    }
    s_beamIdx = (uint16_t)col;
    const int hi = idxAt(col);
    const int headX = col, headY = hi < 0 ? iy + gh - 3 : yAt(Tab5Sense::scoreAt((uint16_t)hi));
    for (int ddy = -1; ddy <= 1; ddy++) for (int ddx = -2; ddx <= 1; ddx++) plot(headX + ddx, headY + ddy, (ddx == 0 && ddy == 0) ? 255 : 215);
    int spawn = 1 + (int)(dt / 14);
    for (Spark& sp : s_sparks) {
        if (!spawn) break;
        if (sp.life > 0) continue;
        sp.x = (float)headX; sp.y = (float)(headY - iy);
        sp.vx = -(0.015f + rnd() * 0.05f); sp.vy = (rnd() - 0.5f) * 0.06f;
        sp.full = sp.life = (int16_t)(350 + rnd() * 550);
        spawn--;
    }
    const uint16_t hotc = mixc(Theme::CYAN, Theme::WHITE, 170);
    for (int y = 0; y < gh; y++) for (int x = 0; x < gw; x++) {
        const uint8_t v = s_phos[y * gw + x];
        if (v <= 8) continue;
        const uint16_t c = v > 200 ? mixc(Theme::CYAN, hotc, (int)((v - 200) * 4.6f)) : mixc(Theme::BG, Theme::CYAN, v * 256 / 200);
        t.drawPixel(sx + 1 + x, iy + y, c);
    }
    for (Spark& sp : s_sparks) {
        if (sp.life <= 0) continue;
        sp.life -= (int16_t)dt;
        sp.x += sp.vx * dt; sp.y += sp.vy * dt; sp.vy += 0.00012f * dt;
        if (sp.life <= 0 || sp.x < 0 || sp.x >= gw || sp.y < 0 || sp.y >= gh) { sp.life = 0; continue; }
        const float lf = (float)sp.life / sp.full;
        const uint16_t c = lf > 0.7f ? mixc(Theme::CYAN, Theme::WHITE, (int)((lf - 0.7f) * 850)) : mixc(Theme::BG, lf > 0.4f ? Theme::VAPOR_PINK : Theme::PURPLE, (int)(lf * 300));
        t.drawPixel(sx + 1 + (int)sp.x, iy + (int)sp.y, c);
        if (lf > 0.75f) t.drawPixel(sx + 2 + (int)sp.x, iy + (int)sp.y, mixc(Theme::BG, Theme::CYAN, (int)(lf * 128)));
    }
    t.fillCircle(sx + 1 + headX, headY, 3, mixc(Theme::BG, Theme::CYAN, 90));
    t.fillRect(sx + headX, headY - 1, 2, 2, Theme::WHITE);
    // the value now, where the line ends: it rolls toward the score rather
    // than jumping, and flashes white for a moment as it crosses the line
    if (lpx >= 0) {
        static float shown = 0; static bool wasOver = false; static uint32_t crossAt = 0;
        shown += (scoreNow - shown) * clampf(dt / 180.0f, 0.05f, 1.0f);
        const bool over = scoreNow >= MOTION_LINE;
        if (over != wasOver) { wasOver = over; crossAt = now; }
        char v[6]; snprintf(v, sizeof v, "%d", (int)lroundf(shown));
        const int bw = (int)strlen(v) * 12 + 6, by = lpy - 9 < iy ? iy : (lpy - 9 > iy + ih - 18 ? iy + ih - 18 : lpy - 9);
        const bool flash = now - crossAt < 300;
        t.fillRect(sx + sw - bw - 3, by, bw, 18, flash ? Theme::WHITE : Theme::BG);
        t.drawRect(sx + sw - bw - 3, by, bw, 18, over ? Theme::PINK : Theme::CYAN);
        text(t, sx + sw - 6, by + 1, v, flash ? Theme::BLACK : Theme::WHITE, flash ? Theme::WHITE : Theme::BG, 2, 2);
    }
    if (P(10)) {
        for (int c = 0; c < sw - 2; c++) {
            const int i = idxAt(c), i2 = idxAt(c + 1); if (i < 0 || i2 < 0) continue;
            const uint32_t g = total - (uint32_t)(n - 1 - i), g2 = total - (uint32_t)(n - 1 - i2);
            if (g / 100 == g2 / 100) continue;
            const int ago = (int)((total - g2) / 10);
            if (ago < 4 || ago > 28) continue;
            char lb[8]; snprintf(lb, sizeof lb, "-%ds", ago);
            text(t, sx + 1 + c + 2, iy + ih - 10, lb, mixc(Theme::BG, Theme::WHITE, 170), Theme::BG, 1);
        }
    }
    text(t, sx + 6, ty - 10, "MOTION", Theme::AMBER, Theme::BG, 1);
}

void banner(TFT_eSPI& t, uint32_t now) {
    const Word w = stateWord(now);
    const int by = 180, bh = 46;
    const uint16_t base = mixc(Theme::BG, w.c, 30);
    t.fillRect(8, by, 624, bh, base);
    if (P(10)) {   // a light sweeping across the band, and scanlines through it
        const int sxp = (int)((now / 4) % 900) - 130;
        for (int k = 0; k < 60; k++) { const int x = sxp + k; if (x > 10 && x < 630) t.drawFastVLine(x, by + 3, bh - 6, mixc(base, w.c, 70 - abs(k - 30) * 2)); }
        for (int y = by + 3; y < by + bh - 3; y += 3) t.drawFastHLine(11, y, 618, mixc(Theme::BG, w.c, 18));
    }
    t.drawRect(9, by + 1, 622, bh - 2, w.c); t.drawRect(10, by + 2, 620, bh - 4, w.c);
    headline(t, 24, by + 5, w.w, w.c, 0, glitchFor(w, now));
    const SenseReport& s = Tab5Sense::last();
    const uint32_t since = Tab5Sense::sinceMotion(now);
    const int rx = 24 + Theme::bangersTextWidth(w.w, Theme::BangersSize::LG) + 40, ry = by + bh / 2;
    if (Tab5Sense::fresh(now) && s.presence && s.state != SENSE_STATE_MOTION && since != UINT32_MAX) {
        const float left = 1.0f - clampf(since / (float)HOLD_MS, 0, 1);
        arc(t, rx, ry, 16, 0, 6.2832f, mixc(Theme::BG, Theme::PURPLE, 90), 4);
        arc(t, rx, ry, 16, -1.5708f, -1.5708f + 6.2832f * left, Theme::VAPOR_PINK, 4);
        textT(t, rx + 26, ry - 8, w.sub, Theme::VAPOR_PINK, 2);
    } else {
        textT(t, rx - 16, ry - 8, w.sub, Theme::CYAN, 2);
    }
    using VP = Squachy::VisitPose;
    static Startle startle;
    const bool moving = Tab5Sense::fresh(now) && s.state == SENSE_STATE_MOTION;
    // LOOK_AROUND and HANDS_UP borrow his detection reactions, which wear
    // the shocked face, so the quiet poses are plain ones: standing easy,
    // lying low while someone is still in the room, dozing off after 2 min.
    const VP pose = startle(now, moving) ? VP::STARTLED
                  : moving ? VP::POINT                      // pointing at the trace, normal face
                  : s.presence ? VP::CROUCH
                  : (since == UINT32_MAX || since > 120000) ? VP::SLEEPY : VP::NONE;
    // He stands inside the band, at its right end: taller, he poked up into
    // MOTION.EXE's graph above it (2026-11-17).
    if (P(10)) squachy(t, -1, pose, 604, by + bh - 3, 0.55f, now);
    else squachy(t, 7, pose, 604, by + bh - 2, 0.6f, now);
}

void tabsAndList(TFT_eSPI& t, uint32_t now) {
    const int ly = 232;
    static const char* const TABS[3] = { "PATHS", "EVENTS", "RADIO" };
    const uint16_t on = mixc(Theme::BG, Theme::VAPOR_BLUE, 90), off = mixc(Theme::BG, Theme::PURPLE, 40);
    for (int k = 0; k < 3; k++) {
        const int x = 8 + k * 122;
        t.fillRect(x, ly, 118, 18, k == s_tab ? on : off);
        char l[16]; if (k == 0) snprintf(l, sizeof l, "%s %u", TABS[k], (unsigned)Tab5Sense::livePaths(now)); else snprintf(l, sizeof l, "%s", TABS[k]);
        text(t, x + 59, ly + 1, l, k == s_tab ? Theme::CYAN : mixc(Theme::BG, Theme::WHITE, 140), k == s_tab ? on : off, 2, 1);
    }
    t.drawFastHLine(8, ly + 18, 624, on);
    const int y0 = ly + 21;
    if (s_tab == 0) {
        for (uint8_t k = 0; k < Tab5Sense::PATHS; k++) {
            const int y = y0 + k * 18;
            const Tab5Sense::Path& p = Tab5Sense::path(k);
            const char* nm = pathName(k);
            const uint16_t pc = P(6) ? *pathColour(k) : Theme::CYAN;
            if (!p.live) { text(t, 12, y, nm, mixc(Theme::BG, Theme::WHITE, 110), Theme::BG, 2); text(t, 150, y, "--", mixc(Theme::BG, Theme::WHITE, 110), Theme::BG, 2); continue; }
            const bool hot = pathHot(p);
            char b[24];
            if (P(6)) t.fillRect(8, y + 2, 3, 12, pc);
            text(t, 14, y, nm, hot ? Theme::VAPOR_PINK : pc, Theme::BG, 2);
            snprintf(b, sizeof b, "%d dBm", (int)p.rssi); text(t, 150, y, b, mixc(Theme::BG, Theme::WHITE, 200), Theme::BG, 2);
            snprintf(b, sizeof b, "%u/s", (unsigned)p.rate); text(t, 252, y, b, mixc(Theme::BG, Theme::WHITE, 200), Theme::BG, 2);
            const int bx = 312, bw = 266;
            t.fillRect(bx, y + 3, bw, 10, mixc(Theme::BG, Theme::PURPLE, 50));
            if (p.cal < 100) {
                t.fillRect(bx, y + 3, bw * p.cal / 100, 10, mixc(Theme::BG, Theme::AMBER, 140));
                text(t, bx + 4, y + 4, "CALIBRATING", Theme::BLACK, mixc(Theme::BG, Theme::AMBER, 140), 1);
            } else if (P(10)) {           // segmented, like the LORA page's meters
                for (int sgm = 0; sgm < 26; sgm++) {
                    const int v = (sgm + 1) * 100 / 26;
                    if (v > p.score) break;
                    t.fillRect(bx + sgm * 10 + 1, y + 3, 8, 10, v <= MOTION_LINE ? pc : Theme::PINK);
                }
            } else {
                t.fillRect(bx, y + 3, bw * p.score / 100, 10, hot ? Theme::PINK : Theme::VAPOR_BLUE);
            }
            t.fillRect(bx + bw * MOTION_LINE / 100, y + 1, 2, 14, Theme::AMBER);
            snprintf(b, sizeof b, "%u", (unsigned)p.score); text(t, 628, y, b, Theme::WHITE, Theme::BG, 2, 2);
        }
    } else if (s_tab == 1) {
        Tab5Sense::Event ev[4];
        const uint8_t n = Tab5Sense::events(ev, 4);
        if (!n) text(t, 12, y0 + 4, "No movement yet.", mixc(Theme::BG, Theme::WHITE, 160), Theme::BG, 2);
        for (uint8_t k = 0; k < n; k++) {
            char b[64];
            const uint32_t ago = (now - ev[k].at) / 1000;
            if (ago < 120) snprintf(b, sizeof b, "%lu s ago", (unsigned long)ago); else snprintf(b, sizeof b, "%lu min ago", (unsigned long)(ago / 60));
            text(t, 12, y0 + k * 18, b, k == 0 ? Theme::WHITE : Theme::CYAN, Theme::BG, 2);
            if (ev[k].open) snprintf(b, sizeof b, "moving now, %lu s so far", (unsigned long)(ev[k].ms / 1000));
            else if (ev[k].ms < 10000) snprintf(b, sizeof b, "movement for %.1f s", ev[k].ms / 1000.0f);
            else snprintf(b, sizeof b, "movement for %lu s", (unsigned long)((ev[k].ms + 500) / 1000));
            text(t, 200, y0 + k * 18, b, ev[k].open ? Theme::VAPOR_PINK : mixc(Theme::BG, Theme::WHITE, 200), Theme::BG, 2);
        }
    } else {
        const SenseReport& s = Tab5Sense::last();
        char b[64];
        char fw[24] = "--"; const char* inf = Tab5Sense::info();
        if (inf[0]) { size_t k = 0; while (inf[k] && inf[k] != ',' && k < sizeof fw - 1) { fw[k] = inf[k]; k++; } fw[k] = 0; }
        if (Tab5Sense::monster()) snprintf(b, sizeof b, "MONSTER C5 on the M5-Bus, firmware %s", fw);
        else snprintf(b, sizeof b, "the Tab5's own radio (C6)");
        text(t, 12, y0, b, Tab5Sense::monster() ? Theme::VAPOR_PINK : Theme::CYAN, Theme::BG, 2);
        snprintf(b, sizeof b, "%s GHz channel %u, %s", Tab5Sense::ghz(s), (unsigned)s.chan,
                 s.mode == SENSE_MODE_STA ? "joined, pinging the router" : (s.mode == SENSE_MODE_PARK ? "listening on one channel" : "starting"));
        text(t, 12, y0 + 18, b, Theme::CYAN, Theme::BG, 2);
        snprintf(b, sizeof b, "%u samples a second, %lu lost", (unsigned)s.rate, (unsigned long)s.drop); text(t, 12, y0 + 36, b, Theme::CYAN, Theme::BG, 2);
        snprintf(b, sizeof b, "%lu lines heard, %lu garbled", (unsigned long)Tab5Sense::lines(), (unsigned long)Tab5Sense::badLines()); text(t, 12, y0 + 54, b, Theme::CYAN, Theme::BG, 2);
    }
}

void statusLine(TFT_eSPI& t, uint32_t now) {
    const SenseReport& s = Tab5Sense::last();
    const bool fresh = Tab5Sense::fresh(now);
    char b[40];
    if (fresh && (now % 1000) < 600) t.fillCircle(42, 8, 3, Theme::GREEN);
    textT(t, 50, 4, fresh ? "LIVE" : "WAIT", fresh ? Theme::GREEN : Theme::AMBER, 1);
    if (Tab5Sense::heard()) {
        snprintf(b, sizeof b, "%s GHz ch %u  %u/s", Tab5Sense::ghz(s), (unsigned)s.chan, (unsigned)s.rate); textT(t, 84, 4, b, Theme::CYAN, 1);
        snprintf(b, sizeof b, "%u PATHS", (unsigned)Tab5Sense::livePaths(now)); textT(t, 220, 4, b, Theme::VAPOR_PINK, 1);
        snprintf(b, sizeof b, "%d dBm", (int)s.rssi); textT(t, 548, 4, b, Theme::VAPOR_PURPLE, 1, 2);
    }
}

// Which radio is sensing, top right on every page: the Monster C5 filled in
// pink (it pulses while new), the Tab5's own C6 outlined.
void radioBadge(TFT_eSPI& t, uint32_t now) {
    const char* r = senseCydRadio();
    if (!r[0]) return;
    const int w = (int)strlen(r) * 6 + 10, x = 636 - w, y = 1, h = 14;
    static uint32_t monAt = 0;
    if (Tab5Sense::monster()) {
        if (!monAt) monAt = now ? now : 1;
        const bool pulse = now - monAt < 3000 && ((now - monAt) / 250) % 2;
        t.fillRoundRect(x, y, w, h, 3, pulse ? Theme::WHITE : Theme::VAPOR_PINK);
        textT(t, x + 5, y + 3, r, Theme::BG, 1);
    } else {
        monAt = 0;
        const uint16_t c = !strcmp(r, "TAB5 C6") ? Theme::CYAN : Theme::AMBER;
        t.drawRoundRect(x, y, w, h, 3, c);
        textT(t, x + 5, y + 3, r, c, 1);
    }
}

void drawSensing(TFT_eSPI& t, uint32_t now, uint32_t dt) {
    if (P(10)) backdrop(t, now);
    spectrumPanel(t);
    tileRow(t, now);
    strip(t, now, dt);
    banner(t, now);
    tabsAndList(t, now);
}

// ============================================================================
// SCOPE
// ============================================================================
const int SC_CX = 236, SC_CY = 318, SC_R = 226;
const int GC = 4;
const int G_X0 = SC_CX - SC_R, G_Y0 = SC_CY - SC_R, G_W = (SC_R * 2) / GC + 1, G_H = SC_R / GC + 1;
uint8_t* s_glass = nullptr;
uint8_t* s_glow = nullptr;
uint8_t* s_cellA = nullptr;
float    s_sweep = 3.14159f;
uint32_t s_pingAt = 0;          // when the sweep last finished a pass: the sonar ring

bool glassReady() {
    if (s_glass) return true;
    const size_t n = (size_t)G_W * G_H;
#if defined(TAB5)
    s_glass = (uint8_t*)heap_caps_calloc(n, 3, MALLOC_CAP_SPIRAM);
#else
    s_glass = (uint8_t*)calloc(n, 3);
#endif
    if (!s_glass) return false;
    s_glow = s_glass + n; s_cellA = s_glow + n;
    for (int gy = 0; gy < G_H; gy++) for (int gx = 0; gx < G_W; gx++) {
        const float dx = G_X0 + gx * GC + GC / 2 - SC_CX, dy = G_Y0 + gy * GC + GC / 2 - SC_CY;
        const float r = sqrtf(dx * dx + dy * dy);
        uint8_t a = 255;
        if (r < SC_R && dy <= 0) { float ang = atan2f(-dy, -dx); a = (uint8_t)clampf(ang / 3.14159f * 254.0f, 0, 254); }
        s_cellA[gy * G_W + gx] = a;
    }
    return true;
}
inline float spokeAngle(uint8_t k) { return 3.14159f + (k + 0.5f) * 3.14159f / 4.0f; }
// SCOPE's readability fixes, one bit each (all on).
enum ScopeFix { FX_BLIP, FX_BEAMS, FX_RINGS, FX_VOICE, FX_STATUS, FX_SUN, FX_CONSOLE, FX_LEGEND };
static uint16_t s_fix = 0xFF;
inline bool FX(ScopeFix b) { return s_fix & (1u << b); }
// Motion strength to distance from the centre: the further out, the more
// motion. The same scale draws the rings, so the rings read as strength.
inline float strengthRadius(float score) { return SC_R * (0.14f + 0.82f * clampf(score / 90.0f, 0, 1)); }
inline int sunRadius() { return FX(FX_SUN) ? 78 : 120; }
// The one motion contact, kept between frames so it glides.
static float s_blipA = 4.712f, s_blipR = 0; static int s_blipX = -1, s_blipY = -1;
inline float spokeRadius(int rssi) { return SC_R * (0.18f + 0.8f * clampf((rssi + 45) / -47.0f, 0, 1)); }

void scopeSky(TFT_eSPI& t, uint32_t now, bool cal) {
    if (P(15)) {   // aurora ribbons, slowly waving behind the stars
        static const uint16_t* const RC[3] = { &Theme::GREEN, &Theme::VAPOR_BLUE, &Theme::VAPOR_PURPLE };
        for (int r = 0; r < 3; r++) {
            for (int x = 0; x < 640; x += 3) {
                const float y = 44 + r * 22 + 14 * sinf(x / 90.0f + now / (2400.0f + r * 700) + r * 2) + 6 * sinf(x / 37.0f + now / 900.0f);
                for (int k = 0; k < 12; k++) t.drawFastHLine(x, (int)y + k, 3, mixc(Theme::BG, *RC[r], FX(FX_SUN) ? 38 - k * 3 : 70 - k * 5));
            }
        }
    }
    for (int q = 0; q < 70; q++) {
        const int x = (int)(hashf(q * 3) * 640), y = 18 + (int)(hashf(q * 5) * 220);
        const float tw = 0.4f + 0.6f * fabsf(sinf(now / (500.0f + q * 37) + q));
        const uint16_t c = mixc(Theme::BG, Theme::WHITE, (int)(40 + 150 * tw * hashf(q * 7)));
        if (hashf(q) > 0.85f) t.fillRect(x, y, 2, 2, c); else t.drawPixel(x, y, c);
    }
    if (P(15)) {   // a shooting star every few seconds
        const uint32_t cyc = now % 6500;
        if (cyc < 700) {
            const uint32_t seed = now / 6500;
            const float sx = 120 + hashf(seed) * 400, sy = 24 + hashf(seed + 1) * 60, f = cyc / 700.0f;
            const float hx = sx + f * 160, hy = sy + f * 50;
            for (int k = 0; k < 14; k++) t.fillRect((int)(hx - k * 6), (int)(hy - k * 1.9f), 2, 2, mixc(Theme::BG, Theme::WHITE, 230 - k * 16));
        }
    }
    const uint16_t grid = mixc(Theme::BG, Theme::PURPLE, 120);
    for (int k = -10; k <= 10; k++) t.drawLine(SC_CX + k * 18, SC_CY, SC_CX + k * 60, 332, grid);
    for (int k = 0; k < 3; k++) t.drawFastHLine(0, SC_CY + 3 + k * k * 3 + (now / 200) % 3, 470, grid);
    if (P(16)) {   // the sun's reflection shimmering on the floor
        for (int k = 0; k < 6; k++) {
            const int y = SC_CY + 2 + k * 2;
            const int hw = (FX(FX_SUN) ? 50 : 80) - k * (FX(FX_SUN) ? 7 : 10) + (int)(6 * sinf(now / 150.0f + k));
            for (int x = SC_CX - hw; x < SC_CX + hw; x += 6) if (hash32(x * 7 + k + (now / 120) * 3) & 1)
                t.drawFastHLine(x, y, 4, mixc(Theme::BG, cal ? Theme::AMBER : (k < 3 ? Theme::VAPOR_YELLOW : Theme::VAPOR_PINK), 150 - k * 18));
        }
    }
}

// Top left: what the radar is working from, the SENSING status line's facts.
void scopeStatus(TFT_eSPI& t, uint32_t now) {
    const SenseReport& s = Tab5Sense::last();
    const bool fresh = Tab5Sense::fresh(now);
    char b[32];
    if (fresh && (now % 1000) < 600) t.fillCircle(12, 26, 3, Theme::GREEN);
    textT(t, 20, 22, fresh ? "LIVE" : "NO SIGNAL", fresh ? Theme::GREEN : Theme::AMBER, 1);
    if (!Tab5Sense::heard()) return;
    if (FX(FX_STATUS)) {   // one line, beside LIVE
        snprintf(b, sizeof b, "%u WIFI PATHS, %u READS/S", (unsigned)Tab5Sense::livePaths(now), (unsigned)s.rate);
        textT(t, 50, 22, b, Theme::CYAN, 1);
        return;
    }
    snprintf(b, sizeof b, "%s GHz  CH %u", "2.4", (unsigned)s.chan); textT(t, 8, 34, b, Theme::CYAN, 1);
    snprintf(b, sizeof b, "%u SAMPLES/S", (unsigned)s.rate); textT(t, 8, 44, b, Theme::CYAN, 1);
    snprintf(b, sizeof b, "%u PATHS", (unsigned)Tab5Sense::livePaths(now)); textT(t, 8, 54, b, Theme::VAPOR_PINK, 1);
}

void scopeSun(TFT_eSPI& t, uint32_t now, bool cal) {
    const int sunR = sunRadius(), sunY = SC_CY - 4;
    for (int y = sunY - sunR; y < sunY; y += 2) {
        const float dy = (float)(sunY - y), half = sqrtf((float)sunR * sunR - dy * dy);
        const float f = 1.0f - dy / sunR;
        const uint16_t c = f < 0.5f ? mixc(Theme::VAPOR_YELLOW, Theme::VAPOR_PINK, (int)(f * 512)) : mixc(Theme::VAPOR_PINK, Theme::PURPLE, (int)((f - 0.5f) * 512));
        const int band = (int)(dy + (now / 90) % 8);
        const bool gap = f > 0.35f && (band % 8) < (int)(f * 5);
        const int gy = (y - G_Y0) / GC;
        for (int x = SC_CX - (int)half; x < SC_CX + (int)half; x += GC) {
            const int gx = (x - G_X0) / GC;
            const int gl = (s_glass && gx >= 0 && gy >= 0 && gx < G_W && gy < G_H) ? s_glass[gy * G_W + gx] : 0;
            const int w = (x + GC > SC_CX + (int)half) ? SC_CX + (int)half - x : GC;
            if (cal && P(20)) {   // an eclipse while it calibrates: dark disc, amber rim
                const float rr = sqrtf((x - SC_CX) * (float)(x - SC_CX) + dy * dy);
                t.fillRect(x, y, w, 2, rr > sunR - 3 ? mixc(Theme::BG, Theme::AMBER, 150) : mixc(Theme::BG, Theme::PURPLE, 18 + gl / 5));
                continue;
            }
            if (gap) { if (gl > 40) t.fillRect(x, y, w, 2, mixc(Theme::BG, Theme::PURPLE, gl / 3)); continue; }
            t.fillRect(x, y, w, 2, mixc(mixc(Theme::BG, c, FX(FX_SUN) ? 95 : 150), Theme::VAPOR_YELLOW, gl / 2));
        }
    }
}

void drawScope(TFT_eSPI& t, uint32_t now, uint32_t dt) {
    const SenseReport& s = Tab5Sense::last();
    const bool fresh = Tab5Sense::fresh(now), cal = fresh && s.state == SENSE_STATE_CAL;
    const float speed = 0.9f + (fresh && s.state >= SENSE_STATE_STILL ? s.score / 80.0f : 0);
    s_sweep += speed * dt / 1000.0f;
    if (s_sweep >= 6.2831853f) { s_sweep -= 3.14159f; s_pingAt = now; }
    if (s_sweep < 3.14159f) s_sweep += 3.14159f;
    scopeSky(t, now, cal);
    scopeStatus(t, now);

    if (glassReady()) {
        const int keep = (int)(256.0f * expf(-(float)dt / 650.0f)), keepG = (int)(256.0f * expf(-(float)dt / 900.0f));
        const int sa = (int)((s_sweep - 3.14159f) / 3.14159f * 254.0f), sw = 5;
        const size_t n = (size_t)G_W * G_H;
        for (size_t c = 0; c < n; c++) {
            s_glass[c] = (uint8_t)((s_glass[c] * keep) >> 8);
            s_glow[c] = (uint8_t)((s_glow[c] * keepG) >> 8);
            const int a = s_cellA[c];
            if (a != 255 && a <= sa && a > sa - sw) s_glass[c] = cal ? 110 : 230;
        }
        if (FX(FX_BLIP) && s_blipX >= 0) {
            const int aa = (int)((s_blipA - 3.14159f) / 3.14159f * 254.0f);
            if (aa > sa - 8 && aa <= sa) {
                const int cx = (s_blipX - G_X0) / GC, cy = (s_blipY - G_Y0) / GC;
                for (int dy = -6; dy <= 6; dy++) for (int dx = -6; dx <= 6; dx++) {
                    const int gx = cx + dx, gy = cy + dy;
                    if (gx < 0 || gy < 0 || gx >= G_W || gy >= G_H) continue;
                    const int v = 255 - (dx * dx + dy * dy) * 7;
                    if (v > s_glow[gy * G_W + gx]) s_glow[gy * G_W + gx] = (uint8_t)(v < 0 ? 0 : v);
                }
            }
        }
        if (!FX(FX_BLIP)) for (uint8_t k = 0; k < Tab5Sense::PATHS; k++) {
            const Tab5Sense::Path& p = Tab5Sense::path(k);
            if (!p.live || !p.everHot || now - p.hotAt > HOLD_MS) continue;
            const float a = spokeAngle(k);
            const int aa = (int)((a - 3.14159f) / 3.14159f * 254.0f);
            if (aa > sa - 8 && aa <= sa) {
                const float r = spokeRadius(p.rssi);
                const int cx = (int)((SC_CX + cosf(a) * r - G_X0) / GC), cy = (int)((SC_CY + sinf(a) * r - G_Y0) / GC);
                for (int dy = -6; dy <= 6; dy++) for (int dx = -6; dx <= 6; dx++) {
                    const int gx = cx + dx, gy = cy + dy;
                    if (gx < 0 || gy < 0 || gx >= G_W || gy >= G_H) continue;
                    const int v = 255 - (dx * dx + dy * dy) * 7;
                    if (v > s_glow[gy * G_W + gx]) s_glow[gy * G_W + gx] = (uint8_t)(v < 0 ? 0 : v);
                }
            }
        }
        for (int gy = 0; gy < G_H; gy++) for (int gx = 0; gx < G_W; gx++) {
            const size_t c = (size_t)gy * G_W + gx;
            const int g = s_glass[c] / 3, f = s_glow[c];
            { const int px = G_X0 + gx * GC + GC / 2 - SC_CX, py = G_Y0 + gy * GC + GC / 2 - (SC_CY - 4);
              if (py < 0 && px * px + py * py < sunRadius() * sunRadius() && f < 10) continue; }
            if (g < 12 && f < 10) continue;
            const uint16_t sweepC = cal ? Theme::AMBER : Theme::PURPLE;
            const uint16_t col = f >= 10 ? mixc(mixc(Theme::BG, sweepC, g), Theme::PINK, f) : mixc(Theme::BG, sweepC, g);
            t.fillRect(G_X0 + gx * GC, G_Y0 + gy * GC, GC, GC, col);
        }
    }
    scopeSun(t, now, cal);
    const uint16_t ring = cal ? mixc(Theme::BG, Theme::AMBER, 90) : mixc(Theme::BG, Theme::PURPLE, 150);
    // Strength rings: STILL, MOTION (the amber line, as on SENSING), MOVING, and the rim.
    static const uint8_t RS[3] = { 14, (uint8_t)MOTION_LINE, 55 };
    auto ringR = [&](int r) { return FX(FX_RINGS) && r < 4 ? (int)strengthRadius(RS[r - 1]) : SC_R * r / 4; };
    for (int r = 1; r <= 4; r++) {
        const int rr = ringR(r);
        if (P(11)) arc(t, SC_CX, SC_CY, rr + 1, 3.14159f, 6.28318f, mixc(Theme::BG, cal ? Theme::AMBER : Theme::VAPOR_PURPLE, 70));
        const uint16_t rc = FX(FX_RINGS) && r == 2 && !cal ? mixc(Theme::BG, Theme::AMBER, 170) : (P(11) && r == 4 ? (cal ? Theme::AMBER : Theme::VAPOR_PURPLE) : ring);
        arc(t, SC_CX, SC_CY, rr, 3.14159f, 6.28318f, rc);
    }
    t.drawFastHLine(SC_CX - SC_R, SC_CY, SC_R * 2, ring);
    // Plain ticks on the rim: no degree numbers, because the radar has no
    // compass. Each path gets its own spoke in its colour, named at the rim.
    for (int d = 0; d <= 180; d += 5) {
        const float a = 3.14159f + d / 180.0f * 3.14159f; const int l = d % 30 == 0 ? 8 : 4;
        t.drawLine(SC_CX + (int)(cosf(a) * SC_R), SC_CY + (int)(sinf(a) * SC_R), SC_CX + (int)(cosf(a) * (SC_R + l)), SC_CY + (int)(sinf(a) * (SC_R + l)), mixc(Theme::BG, Theme::PURPLE, d % 30 == 0 ? 220 : 150));
    }
    for (uint8_t k = 0; k < Tab5Sense::PATHS; k++) {
        const Tab5Sense::Path& p = Tab5Sense::path(k);
        const float a = spokeAngle(k);
        const uint16_t pc = p.live ? *pathColour(k) : mixc(Theme::BG, Theme::WHITE, 70);
        const float nameR = FX(FX_BEAMS) ? SC_R + 20 : SC_R + 12;
        if (FX(FX_BEAMS)) {
            // The path is the line from the router to the radio in the middle;
            // it glows with how much it is being disturbed, and pulses travel
            // down it while it is.
            const int sc = p.live ? (p.score > 100 ? 100 : p.score) : 0;
            const bool hot = pathHot(p);
            const int ex = SC_CX + (int)(cosf(a) * (SC_R + 4)), ey = SC_CY + (int)(sinf(a) * (SC_R + 4));
            t.drawWideLine(SC_CX, SC_CY, ex, ey, hot ? 3 : (sc >= 14 ? 2 : 1), mixc(Theme::BG, pc, 60 + sc * 19 / 10), Theme::BG);
            if (sc >= 14) for (int q = 0; q < 3; q++) {
                const float f = fmodf(now / (hot ? 600.0f : 1100.0f) + q / 3.0f + k * 0.17f, 1.0f), r = SC_R * (1.0f - f);
                t.fillRect(SC_CX + (int)(cosf(a) * r) - 2, SC_CY + (int)(sinf(a) * r) - 2, 4, 4, mixc(Theme::BG, hot ? Theme::WHITE : pc, 255 - (int)(f * 150)));
            }
            // a little router: box, two antennas, a status light
            const int rx = SC_CX + (int)(cosf(a) * (SC_R + 9)), ry = SC_CY + (int)(sinf(a) * (SC_R + 9));
            t.fillRect(rx - 6, ry - 3, 13, 7, Theme::BG); t.drawRect(rx - 6, ry - 3, 13, 7, pc);
            t.drawFastVLine(rx - 4, ry - 7, 4, pc); t.drawFastVLine(rx + 4, ry - 7, 4, pc);
            t.fillRect(rx - 3, ry, 2, 2, hot && (now / 150) % 2 ? Theme::WHITE : pc);
        } else {
            for (int r = 12; r < SC_R; r += 8) t.drawFastHLine(SC_CX + (int)(cosf(a) * r), SC_CY + (int)(sinf(a) * r), 3, mixc(Theme::BG, pc, 110));
        }
        int lx = SC_CX + (int)(cosf(a) * nameR);
        const int ly = SC_CY + (int)(sinf(a) * nameR);
        const char* nm = p.live ? pathName(k) : "--";
        const int tw = (int)strlen(nm) * 6;
        uint8_t al = lx < SC_CX - 10 ? 2 : (lx > SC_CX + 10 ? 0 : 1);
        if (al == 2 && lx - tw < 4) { lx = 4; al = 0; }              // the far-left spoke: keep the name on screen
        if (al == 0 && lx + tw > 466) { lx = 466; al = 2; }
        textT(t, lx, ly - 4, nm, pc, 1, al);
    }
    static const char* const RL[4] = { "-50", "-62", "-74", "-86 dBm" };
    static const char* const RW[4] = { "STILL", "MOTION", "MOVING", "WALKING" };
    for (int k = 0; k < 4; k++) {
        if (FX(FX_RINGS)) {
            // on the floor at each ring's left end, clear of the contact
            const int lw = (int)strlen(RW[k]) * 6 + 4;
            int lx = SC_CX - ringR(k + 1) - lw / 2; if (lx < 4) lx = 4;
            const int ly = SC_CY - (k % 2 ? 22 : 11);   // alternate rows: the inner rings sit close
            const uint16_t lc = k == 1 ? Theme::AMBER : mixc(Theme::BG, Theme::WHITE, 170);
            t.fillRect(lx, ly - 1, lw, 10, Theme::BG); t.drawRect(lx, ly - 1, lw, 10, mixc(Theme::BG, k == 1 ? Theme::AMBER : Theme::PURPLE, 140));
            textT(t, lx + 2, ly, RW[k], lc, 1);
            continue;
        }
        const int ly = SC_CY - SC_R * (k + 1) / 4 + 2;
        if (P(11)) { t.fillRect(SC_CX + 2, ly - 1, (int)strlen(RL[k]) * 6 + 4, 10, Theme::BG); t.drawRect(SC_CX + 2, ly - 1, (int)strlen(RL[k]) * 6 + 4, 10, mixc(Theme::BG, Theme::PURPLE, 120)); }
        textT(t, SC_CX + 4, ly, RL[k], mixc(Theme::BG, Theme::WHITE, 150), 1);
    }
    if (P(18)) {   // the sonar ring, once a pass
        const uint32_t pa = now - s_pingAt;
        if (pa < 1100) {
            const int r = (int)(pa * SC_R / 1100);
            arc(t, SC_CX, SC_CY, r, 3.14159f, 6.28318f, mixc(Theme::BG, cal ? Theme::AMBER : Theme::CYAN, 230 - (int)(pa * 200 / 1100)), 2);
        }
    }
    const uint16_t beamC = cal ? Theme::AMBER : mixc(Theme::VAPOR_PURPLE, Theme::WHITE, 120);
    if (P(12)) {   // a glowing trailing edge and sparkles riding the beam
        t.drawWideLine(SC_CX, SC_CY, SC_CX + cosf(s_sweep - 0.03f) * SC_R, SC_CY + sinf(s_sweep - 0.03f) * SC_R, 3, mixc(Theme::BG, cal ? Theme::AMBER : Theme::PURPLE, 170), Theme::BG);
        for (int q = 0; q < 10; q++) {
            const float rr = hashf(q + now / 40) * SC_R;
            t.fillRect(SC_CX + (int)(cosf(s_sweep) * rr) - 1, SC_CY + (int)(sinf(s_sweep) * rr) - 1, 2, 2, q % 3 ? Theme::WHITE : Theme::VAPOR_PINK);
        }
    }
    t.drawWideLine(SC_CX, SC_CY, SC_CX + cosf(s_sweep) * SC_R, SC_CY + sinf(s_sweep) * SC_R, 2, beamC, Theme::BG);
    if (!fresh && P(20)) {   // no radio: static across the dome
        for (int q = 0; q < 900; q++) {
            const float a = 3.14159f + hashf(q + now / 50 * 7) * 3.14159f, r = hashf(q * 3 + now / 50) * SC_R;
            t.drawPixel(SC_CX + (int)(cosf(a) * r), SC_CY + (int)(sinf(a) * r), (q & 1) ? Theme::WHITE : mixc(Theme::BG, Theme::WHITE, 90));
        }
    }
    using VP = Squachy::VisitPose;
    int hotK = -1, freshK = -1;
    if (fresh && !cal) for (uint8_t k = 0; k < Tab5Sense::PATHS; k++) {
        const Tab5Sense::Path& p = Tab5Sense::path(k);
        if (!p.live || !p.everHot) continue;
        const uint32_t ago = now - p.hotAt;
        if (ago > HOLD_MS) continue;
        const bool hot = pathHot(p);
        // the busiest path speaks for the room, not whichever came last
        if (hot && (hotK < 0 || p.score > Tab5Sense::path((uint8_t)hotK).score)) hotK = k;
        if (hot && ago < 1500 && (freshK < 0 || p.score > Tab5Sense::path((uint8_t)freshK).score)) freshK = k;
        if (FX(FX_BLIP)) continue;
        const float a = spokeAngle(k), r = spokeRadius(p.rssi);
        const int x = SC_CX + (int)(cosf(a) * r), y = SC_CY + (int)(sinf(a) * r);
        const uint16_t pc = *pathColour(k);
        if (P(19)) {   // a comet tail back down the spoke
            for (int q = 1; q < 4; q++) {
                const float r0 = spokeRadius(p.trail[q - 1]), r1 = spokeRadius(p.trail[q]);
                for (int s2 = 0; s2 < 5; s2++) {
                    const float rr = r0 + (r1 - r0) * s2 / 5.0f;
                    const int sz = 5 - q;
                    t.fillRect(SC_CX + (int)(cosf(a) * rr) - sz / 2, SC_CY + (int)(sinf(a) * rr) - sz / 2, sz, sz, mixc(Theme::BG, pc, 200 - q * 50 - s2 * 8));
                }
            }
        } else {
            for (int q = 1; q < 4; q++) { const float rq = spokeRadius(p.trail[q]); t.fillRect(SC_CX + (int)(cosf(a) * rq) - 2, SC_CY + (int)(sinf(a) * rq) - 2, 4, 4, mixc(Theme::BG, Theme::VAPOR_PINK, 150 - q * 40)); }
        }
        if (P(13) && ago < 2000) {   // arrival: ping rings
            for (int q = 0; q < 3; q++) { const int aq = (int)ago - q * 300; if (aq < 0) continue; t.drawCircle(x, y - 8, 10 + aq / 25, mixc(Theme::BG, pc, 230 - aq * 230 / 2000)); }
        }
        if (hot) {   // lock-on brackets; they close in on arrival
            int b = 16 + (int)(3 * sinf(now / 90.0f));
            if (P(19) && ago < 450) b = 44 - (int)(ago * 28 / 450);
            for (int sx = -1; sx <= 1; sx += 2) for (int sy = -1; sy <= 1; sy += 2) {
                const int cx = x + sx * b, cy = y - 8 + sy * b;
                t.drawFastHLine(sx < 0 ? cx : cx - 6, cy, 7, Theme::PINK);
                t.drawFastVLine(cx, sy < 0 ? cy : cy - 6, 7, Theme::PINK);
            }
        }
        squachy(t, pathOutfit(p.tag), hot ? VP::CHEER : VP::NONE, x, y + 12, 0.55f, now + k * 90);
        if (P(13)) {   // a name tag
            char l[24]; snprintf(l, sizeof l, "%s %d", pathName(k), (int)p.rssi);
            const int w = (int)strlen(l) * 6 + 6;
            t.fillRect(x - w / 2, y + 14, w, 11, Theme::BG); t.drawRect(x - w / 2, y + 14, w, 11, pc);
            textT(t, x, y + 16, l, pc, 1, 1);
        } else {
            char l[12]; snprintf(l, sizeof l, "%c %d", 'A' + k, (int)p.rssi);
            textT(t, x, y + 16, l, ago < HOLD_MS / 2 ? Theme::CYAN : mixc(Theme::BG, Theme::CYAN, 150), 1, 1);
        }
    }
    if (FX(FX_BLIP)) {
        // ONE contact for the room's motion. Its direction leans toward the
        // paths being disturbed most (a score-weighted blend of their spokes);
        // its distance is how strong the motion is, on the rings' scale.
        const bool show = fresh && !cal && (s.state == SENSE_STATE_MOTION || s.presence);   // the same moments the headline says MOTION/PRESENCE
        float vx = 0, vy = 0, ws = 0;
        for (uint8_t k = 0; k < Tab5Sense::PATHS; k++) {
            const Tab5Sense::Path& p = Tab5Sense::path(k);
            if (!p.live || p.score <= 8) continue;
            const float wk = (float)(p.score - 8) * (p.score - 8);   // squared: the busiest path leads
            vx += cosf(spokeAngle(k)) * wk; vy += sinf(spokeAngle(k)) * wk; ws += wk;
        }
        const float ease = clampf(dt / 450.0f, 0.02f, 1.0f);
        if (ws > 0) { float ta = atan2f(vy, vx); if (ta < 0) ta += 6.2831853f; if (ta < 3.3f) ta = 3.3f; if (ta > 6.12f) ta = 6.12f; s_blipA += (ta - s_blipA) * ease; }
        s_blipR += (strengthRadius(show ? s.score : 0) - s_blipR) * ease;
        static int16_t tx[8], ty[8]; static uint8_t tn = 0; static uint32_t tAt = 0;
        if (!show) { s_blipX = s_blipY = -1; tn = 0; }
        else {
            const int x = SC_CX + (int)(cosf(s_blipA) * s_blipR), y = SC_CY + (int)(sinf(s_blipA) * s_blipR);
            s_blipX = x; s_blipY = y;
            if (now - tAt > 140) { tAt = now; memmove(tx + 1, tx, sizeof tx - sizeof tx[0]); memmove(ty + 1, ty, sizeof ty - sizeof ty[0]); tx[0] = (int16_t)x; ty[0] = (int16_t)y; if (tn < 8) tn++; }
            for (int q = 1; q < tn; q++) { const int sz = 6 - q / 2; t.fillRect(tx[q] - sz / 2, ty[q] - 8 - sz / 2, sz, sz, mixc(Theme::BG, Theme::VAPOR_PINK, 210 - q * 24)); }
            const bool moving = s.state == SENSE_STATE_MOTION;
            if (moving) {   // a ping ring and lock-on brackets while it moves
                const int pr = 14 + (int)((now / 35) % 24);
                t.drawCircle(x, y - 8, pr, mixc(Theme::BG, Theme::PINK, 230 - pr * 6));
                const int b = 20 + (int)(3 * sinf(now / 90.0f));
                for (int sx = -1; sx <= 1; sx += 2) for (int sy = -1; sy <= 1; sy += 2) {
                    const int cx = x + sx * b, cy = y - 8 + sy * b;
                    t.drawFastHLine(sx < 0 ? cx : cx - 6, cy, 7, Theme::PINK);
                    t.drawFastVLine(cx, sy < 0 ? cy : cy - 6, 7, Theme::PINK);
                }
            }
            squachy(t, 4, moving ? VP::CHEER : VP::CROUCH, x, y + 12, 0.7f, now);
            char l[20]; if (moving) snprintf(l, sizeof l, "MOTION %u", (unsigned)s.score); else snprintf(l, sizeof l, "STILL HERE");
            const int lw = (int)strlen(l) * 6 + 6;
            t.fillRect(x - lw / 2, y + 15, lw, 11, Theme::BG); t.drawRect(x - lw / 2, y + 15, lw, 11, moving ? Theme::PINK : Theme::VAPOR_PINK);
            textT(t, x, y + 17, l, moving ? Theme::PINK : Theme::VAPOR_PINK, 1, 1);
        }
    }
    t.fillRect(SC_CX - 4, SC_CY - 4, 8, 8, Theme::CYAN);
    const Word w = stateWord(now);
    static Startle startle;
    const bool jump = startle(now, hotK >= 0);
    const uint32_t quiet = Tab5Sense::sinceMotion(now);
    const VP pose = !fresh ? VP::SAD : (jump ? VP::STARTLED : (freshK >= 0 ? VP::POINT : (hotK >= 0 ? VP::CHEER
                  : (quiet == UINT32_MAX || quiet > 120000) ? VP::SLEEPY : VP::NONE)));
    // Lines of 17 characters at most: his bubble is right-aligned on the
    // screen, and a longer one reached over the radar's outer ring (2026-11-17).
    char say[40] = "";
    if (P(14)) {
        if (!fresh) snprintf(say, sizeof say, "Lost the signal!");
        else if (cal) snprintf(say, sizeof say, "Shh, calibrating");
        else if (FX(FX_VOICE) && freshK >= 0) snprintf(say, sizeof say, "%s's path!", pathName((uint8_t)freshK));
        else if (FX(FX_VOICE) && hotK >= 0) snprintf(say, sizeof say, "Eyes on %s", pathName((uint8_t)hotK));
        else if (FX(FX_VOICE) && s.presence) snprintf(say, sizeof say, "Someone's here.");
        else if (freshK >= 0) snprintf(say, sizeof say, "%s moved!", pathName((uint8_t)freshK));
        else if (hotK >= 0) snprintf(say, sizeof say, "Eyes on %s", pathName((uint8_t)hotK));
        else if (Tab5Sense::sinceMotion(now) > 60000) snprintf(say, sizeof say, "All quiet.");
    }
    // Feet clear of the PATH ACTIVITY panel below him (drawn after him, it
    // cut his feet off at 252).
    squachy(t, -1, pose, 552, 238, 1.55f, now, say[0] ? say : nullptr);
    const uint16_t panel = mixc(Theme::BG, Theme::PURPLE, 34);
    t.fillRect(470, 244, 162, 86, panel); t.drawRect(470, 244, 162, 86, Theme::PURPLE);
    if (FX(FX_CONSOLE)) {
        // The room's state and score on top; under it every path's activity
        // as a tall bar with its name, so you can see which ones you're crossing.
        t.setTextFont(2); t.setTextSize(1); t.setTextColor(w.c, panel);
        t.setCursor(478, 248); t.print(w.w);
        char sb[8]; if (fresh && s.state >= SENSE_STATE_STILL) snprintf(sb, sizeof sb, "%u", (unsigned)s.score); else snprintf(sb, sizeof sb, "--");
        text(t, 626, 248, sb, Theme::WHITE, panel, 2, 2);
        text(t, 478, 268, "PATH ACTIVITY", mixc(panel, Theme::WHITE, 150), panel, 1);
        for (uint8_t k = 0; k < Tab5Sense::PATHS; k++) {
            const Tab5Sense::Path& p = Tab5Sense::path(k);
            const bool hot = pathHot(p);
            const int x0 = 476 + k * 38, h = p.live ? 1 + (p.score > 100 ? 100 : p.score) * 34 / 100 : 1;
            t.fillRect(x0 + 4, 280, 26, 36, mixc(panel, Theme::BG, 120));
            t.fillRect(x0 + 4, 316 - h, 26, h, hot ? Theme::PINK : *pathColour(k));
            t.drawFastHLine(x0 + 4, 316 - MOTION_LINE * 34 / 100, 26, Theme::AMBER);
            char l[4] = { 0 }; strncpy(l, pathName(k), 3);
            text(t, x0 + 17, 319, l, p.live ? (hot ? Theme::PINK : *pathColour(k)) : mixc(panel, Theme::WHITE, 90), panel, 1, 1);
        }
    } else {
    for (int q = 0; q < 8; q++) t.fillRect(478 + q * 9, 250, 6, 4, (hash32(q + now / 300) & 1) ? Theme::GREEN : mixc(Theme::BG, Theme::GREEN, 60));
    t.setTextFont(2); t.setTextSize(1); t.setTextColor(w.c, panel);
    t.setCursor(551 - t.textWidth(w.w) / 2, 258); t.print(w.w);
    char sb[8]; if (fresh && s.state >= SENSE_STATE_STILL) snprintf(sb, sizeof sb, "%u", (unsigned)s.score); else snprintf(sb, sizeof sb, "--");
    text(t, 551, 276, sb, Theme::WHITE, panel, 3, 1);
    for (uint8_t k = 0; k < Tab5Sense::PATHS; k++) {
        const Tab5Sense::Path& p = Tab5Sense::path(k);
        const bool hot = pathHot(p);
        // three letters, coloured: two paths that start alike never look the same
        char l[4] = { 0 }; strncpy(l, pathName(k), 3);
        text(t, 478 + k * 38, 320, l, p.live ? (hot ? Theme::VAPOR_PINK : *pathColour(k)) : mixc(panel, Theme::WHITE, 90), panel, 1);
        const int h = p.live ? 1 + p.score / 8 : 1;
        t.fillRect(482 + k * 38, 318 - h, 10, h, hot ? Theme::PINK : *pathColour(k));
    }
    }
    if (FX(FX_LEGEND) && FX(FX_BLIP)) {   // what you are looking at
        const uint16_t lc = mixc(Theme::BG, Theme::WHITE, 160);
        const int ly0 = FX(FX_STATUS) ? 34 : 64;
        t.fillRect(9, ly0 + 1, 6, 6, Theme::PINK); textT(t, 20, ly0, "SQUACHY = MOTION", lc, 1);
        t.drawFastHLine(8, ly0 + 14, 8, Theme::CYAN); textT(t, 20, ly0 + 10, "LINE = WIFI PATH", lc, 1);
        textT(t, 8, ly0 + 20, "FARTHER = STRONGER", lc, 1);
    }
    if (P(17)) {   // HUD frame: corner brackets
        const uint16_t hc = mixc(Theme::BG, Theme::CYAN, 180);
        t.drawFastHLine(2, 20, 18, hc); t.drawFastVLine(2, 20, 18, hc);
        t.drawFastHLine(620, 20, 18, hc); t.drawFastVLine(637, 20, 18, hc);
        t.drawFastHLine(2, 329, 18, hc); t.drawFastVLine(2, 312, 18, hc);
    }
    if (cal) {
        headline(t, SC_CX, 30, "PLEASE STAND BY", Theme::AMBER, 1, 0);
        char b[32]; snprintf(b, sizeof b, "calibrating the room %u%%", (unsigned)s.cal);
        textT(t, SC_CX, 70, b, Theme::AMBER, 1, 1);
    } else if (FX(FX_VOICE) && fresh) {
        headline(t, SC_CX, 36, w.w, w.c, 1, s.state == SENSE_STATE_MOTION ? 3 : 0);
    } else if (freshK >= 0) {
        char b[24];
        if (P(13)) snprintf(b, sizeof b, "%s!", pathName((uint8_t)freshK)); else snprintf(b, sizeof b, "CONTACT %c", (char)('A' + freshK));
        headline(t, SC_CX, 36, b, Theme::PINK, 1, 3);
    } else if (!fresh) {
        headline(t, SC_CX, 36, w.w, w.c, 1, 0);
    }
}

// ============================================================================
// SNEAK
// ============================================================================
enum SPhase : uint8_t { S_IDLE, S_COUNT, S_RUN, S_CAUGHT, S_CLEAN };
// The countdown is also the room reading: hold still for 5 s while the
// score settles, and the last 3 s of it are the player's own baseline.
const uint32_t COUNT_MS = 5000, BASE_FROM_MS = 2000, RUN_MS = 10000, RESULT_MS = 6000;
// The line to stay under is the baseline plus a margin, never below a floor.
// From the Nov 7 recordings: an empty room peaks ~26, someone sitting still
// 33-43 (breathing, shifting), walking 65-90. The old fixed lines (28/20/12)
// sat inside "sitting still", so breathing alone got you caught.
struct Level { const char* name; uint8_t floor, margin; };
const Level LEVELS[3] = { { "EASY", 70, 40 }, { "NORMAL", 55, 28 }, { "HARD", 42, 18 } };
uint8_t levelTarget(uint8_t level, float base) {
    const float t = base + LEVELS[level].margin;
    const float f = t < LEVELS[level].floor ? LEVELS[level].floor : t;
    return (uint8_t)(f > 95 ? 95 : f);
}
struct SRun { uint8_t peak; uint8_t level; uint8_t target; uint8_t base; };
struct Sneak {
    SPhase  phase = S_IDLE;
    uint32_t at = 0;
    uint8_t level = 0;
    uint8_t peak = 0;
    uint8_t target = 70;        // the line for this run (or, idle, what it would be now)
    float   base = 0;           // the room with the player holding still
    float   baseSum = 0; uint16_t baseN = 0;
    uint8_t trace[100] = {0};
    uint8_t best[3] = { 255, 255, 255 };
    uint8_t bestTrace[3][100] = {{0}};
    bool    newBest = false;
    SRun    runs[5] = {};
    uint8_t runN = 0;
    uint32_t caughtAt = 0;
    float   caughtProg = 0;
} s_sn;

void sneakStart(uint32_t now) {
    if (s_sn.phase == S_COUNT || s_sn.phase == S_RUN) return;
    s_sn.phase = S_COUNT; s_sn.at = now; s_sn.peak = 0; s_sn.newBest = false;
    s_sn.baseSum = 0; s_sn.baseN = 0;
    memset(s_sn.trace, 0, sizeof s_sn.trace);
}

void sneakStep(uint32_t now) {
    const SenseReport& s = Tab5Sense::last();
    const bool live = Tab5Sense::fresh(now) && s.state >= SENSE_STATE_STILL;
    if (s_sn.phase == S_IDLE && live) {   // a preview of the line, from the room as it is
        s_sn.base += (s.score - s_sn.base) * 0.02f;
        s_sn.target = levelTarget(s_sn.level, s_sn.base);
    }
    if (s_sn.phase == S_COUNT) {
        if (live && now - s_sn.at >= BASE_FROM_MS) { s_sn.baseSum += s.score; s_sn.baseN++; }
        if (s_sn.baseN) s_sn.target = levelTarget(s_sn.level, s_sn.baseSum / s_sn.baseN);
        if (now - s_sn.at >= COUNT_MS) {
            s_sn.base = s_sn.baseN ? s_sn.baseSum / s_sn.baseN : s_sn.base;
            s_sn.target = levelTarget(s_sn.level, s_sn.base);
            s_sn.phase = S_RUN; s_sn.at = now;
        }
    }
    if (s_sn.phase == S_RUN) {
        const uint32_t into = now - s_sn.at;
        if (live && s.score > s_sn.peak) s_sn.peak = s.score;
        const uint32_t slot = into / 100; if (slot < 100 && live) s_sn.trace[slot] = s.score;
        const uint8_t target = s_sn.target;
        if (s_sn.peak >= target || into >= RUN_MS) {
            s_sn.phase = s_sn.peak >= target ? S_CAUGHT : S_CLEAN; s_sn.at = now;
            if (s_sn.phase == S_CAUGHT) { s_sn.caughtAt = now; s_sn.caughtProg = clampf(into / (float)RUN_MS, 0, 1); }
            memmove(s_sn.runs + 1, s_sn.runs, sizeof(SRun) * 4);
            s_sn.runs[0] = { s_sn.peak, s_sn.level, s_sn.target, (uint8_t)s_sn.base };
            if (s_sn.runN < 5) s_sn.runN++;
            if (s_sn.phase == S_CLEAN && s_sn.peak < s_sn.best[s_sn.level]) {
                s_sn.best[s_sn.level] = s_sn.peak; memcpy(s_sn.bestTrace[s_sn.level], s_sn.trace, 100); s_sn.newBest = true;
            }
        }
    }
    if ((s_sn.phase == S_CAUGHT || s_sn.phase == S_CLEAN) && now - s_sn.at >= RESULT_MS) s_sn.phase = S_IDLE;
}

char grade(uint8_t peak, uint8_t target, uint8_t base) {
    if (peak >= target) return 'D';
    const float f = peak <= base ? 0 : (peak - base) / (float)(target - base);
    return f < 0.25f ? 'S' : (f < 0.5f ? 'A' : (f < 0.75f ? 'B' : 'C'));
}

void drawSneak(TFT_eSPI& t, uint32_t now) {
    sneakStep(now);
    const SenseReport& s = Tab5Sense::last();
    const bool live = Tab5Sense::fresh(now) && s.state >= SENSE_STATE_STILL;
    const Level& L = LEVELS[s_sn.level];
    const uint8_t T = s_sn.target;
    int ox = 0, oy = 0;
    if (s_sn.phase == S_CAUGHT && now - s_sn.caughtAt < 500) {
        const int k = 6 - (int)((now - s_sn.caughtAt) / 90);
        ox = (int)(hash32(now / 30) % (2 * k + 1)) - k; oy = (int)(hash32(now / 30 + 9) % (2 * k + 1)) - k;
    }
    const int hz = 150 + oy;
    for (int y = 18; y < hz; y += 2) t.fillRect(0, y, 640, 2, mixc(Theme::BG, Theme::PURPLE, (y - 18) * 60 / (hz - 18)));
    t.fillRect(0, hz, 640, 334 - hz, mixc(Theme::BG, Theme::PURPLE, 22));
    const uint16_t grid = mixc(Theme::BG, Theme::PURPLE, 140);
    for (int k = -14; k <= 14; k++) t.drawLine(320 + ox + k * 14, hz, 320 + ox + k * 90, 334, grid);
    for (int k = 0; k < 9; k++) { const float f = (k + (now % 700) / 700.0f) / 9.0f; t.drawFastHLine(0, hz + (int)(f * f * (334 - hz)), 640, grid); }
    const uint16_t wall = mixc(Theme::BG, Theme::PURPLE, 45);
    t.fillTriangle(0, 18, 120 + ox, hz, 0, 333, wall);
    t.fillTriangle(639, 18, 520 + ox, hz, 639, 333, wall);
    if (P(11)) {   // wall panels, light strips; ceiling lamps
        for (int k = 0; k < 4; k++) {
            const float f = (k + 1) / 5.0f;
            const int xl = (int)(f * 120) + ox, xr = 639 - (int)(f * 119) + ox;
            const int yt = 18 + (int)(f * (hz - 18)), yb = 333 - (int)(f * (333 - hz));
            t.drawLine(xl, yt, xl, yb, mixc(Theme::BG, Theme::PURPLE, 90));
            t.drawLine(xr, yt, xr, yb, mixc(Theme::BG, Theme::PURPLE, 90));
            const uint16_t lc = mixc(Theme::BG, k % 2 ? Theme::CYAN : Theme::VAPOR_PINK, 150 + (int)(80 * sinf(now / 300.0f + k)));
            t.fillRect(xl + 3, yt + (yb - yt) / 2 - 10, 2, 20, lc);
            t.fillRect(xr - 5, yt + (yb - yt) / 2 - 10, 2, 20, lc);
        }
        for (int k = 0; k < 3; k++) { const int x = 220 + k * 100 + ox; t.fillRect(x - 12, 18, 24, 6, mixc(Theme::BG, Theme::WHITE, 70)); t.fillRect(x - 8, 24, 16, 3, Theme::VAPOR_YELLOW); }
    }
    if (P(13)) {   // spotlight cones sweeping the floor
        for (int k = 0; k < 2; k++) {
            const int cx = 220 + k * 200 + ox;
            const float sw = sinf(now / (1300.0f + k * 400) + k * 2);
            const int fx = cx + (int)(sw * 180), fy = 296;
            t.fillTriangle(cx, 27, fx - 46, fy, fx + 46, fy, mixc(Theme::BG, Theme::VAPOR_BLUE, 40));
            t.fillEllipse(fx, fy, 50, 10, mixc(Theme::BG, Theme::VAPOR_BLUE, 80));
        }
    }
    const float rate = 0.003f + (live ? s.score : 0) / 5000.0f;
    static const float DEPTH[3] = { 0.25f, 0.55f, 0.85f };
    for (int b = 0; b < 3; b++) {
        const float d = DEPTH[b];
        int y = hz + (int)(d * d * (334 - hz) - 30 * (1 - d));
        if (P(12)) y += (int)(10 * sinf(now / (700.0f + b * 230) + b));
        const int pulse = (int)(110 + 140 * fabsf(sinf(now * rate * 2 + b)));
        const int x0 = 120 - (int)(d * 120) + ox, x1 = 520 + (int)(d * 120) + ox, y1 = y + (b % 2 ? 10 : -10);
        if (P(12)) { t.drawLine(x0, y - 2, x1, y1 - 2, mixc(Theme::BG, Theme::PINK, pulse / 3)); t.drawLine(x0, y + 2, x1, y1 + 2, mixc(Theme::BG, Theme::PINK, pulse / 3)); }
        t.drawWideLine(x0, y, x1, y1, 1.0f + d * 2.5f, mixc(Theme::BG, Theme::PINK, pulse), Theme::BG);
        t.fillRect(x0 - 3, y - 3, 6, 6, Theme::PINK); t.fillRect(x1 - 3, y1 - 3, 6, 6, Theme::PINK);
    }
    if (P(15)) {   // a security camera, panning, its LED blinking
        const int cx = 590 + ox, cy = 40;
        const float pan = sinf(now / 1500.0f) * 0.6f;
        t.fillRect(cx - 3, 18, 6, 14, mixc(Theme::BG, Theme::WHITE, 120));
        const int ex = cx + (int)(cosf(2.5f + pan) * 24), ey = cy + (int)(sinf(2.5f + pan) * 24);
        t.drawWideLine(cx, cy, ex, ey, 10, mixc(Theme::BG, Theme::WHITE, 170), Theme::BG);
        t.fillCircle(ex, ey, 5, Theme::BLACK); t.fillCircle(ex, ey, 2, mixc(Theme::BG, Theme::VAPOR_BLUE, 200));
        if ((now / 500) % 2) t.fillCircle(cx + 4, cy - 4, 2, Theme::RED);
    }
    if (s_sn.phase == S_CAUGHT) {
        const float a = now / 160.0f;
        for (int k = 0; k < 2; k++) for (int q = 0; q < 6; q++) {
            const float aq = a + k * 3.14159f + q * 0.11f;
            t.drawLine(320, 22, 320 + (int)(cosf(aq) * 520), 22 + (int)(sinf(aq) * 520), k == 0 ? mixc(Theme::BG, Theme::RED, 120) : mixc(Theme::BG, Theme::VAPOR_BLUE, 120));
        }
        if (P(18) && (now / 180) % 2) for (int k = 0; k < 4; k++) t.drawRect(k, 18 + k, 640 - 2 * k, 316 - 2 * k, Theme::RED);
        if (P(20)) {   // and you get slimed, the 90s Nickelodeon way
            const uint32_t ca = now - s_sn.caughtAt;
            const int pour = ca > 700 ? 700 : (int)ca;
            const uint16_t sld = mixc(Theme::BG, Theme::GREEN, 150);
            for (int x = 0; x < 640; x += 4) {
                const int drip = 6 + (int)(hashf(x * 13) * 18) + (hashf(x * 7) > 0.8f ? (int)(hashf(x * 3) * pour / 6) : 0);
                int len = drip * pour / 700 + (int)(3 * sinf(x / 23.0f + now / 300.0f));
                if (len < 1) len = 1;
                t.fillRect(x, 18, 4, len, Theme::GREEN);
                t.fillRect(x, 18 + (len > 2 ? len - 2 : 0), 4, 2, sld);
            }
        }
    }
    if (P(19)) {   // the idol at the end of the vault
        const int ix = 520 + ox, iy = 300 + oy;
        t.fillRect(ix - 16, iy - 18, 32, 18, mixc(Theme::BG, Theme::WHITE, 90));
        if (s_sn.phase != S_CLEAN) {
            t.fillCircle(ix, iy - 30, 9, Theme::VAPOR_YELLOW); t.fillRect(ix - 6, iy - 24, 12, 6, Theme::VAPOR_YELLOW);
            t.fillRect(ix - 4, iy - 33, 2, 2, Theme::BLACK); t.fillRect(ix + 2, iy - 33, 2, 2, Theme::BLACK);
            if ((now / 300) % 3 == 0) t.drawPixel(ix + 6, iy - 38, Theme::WHITE);
        }
    }
    using VP = Squachy::VisitPose;
    float prog = 0;
    if (s_sn.phase == S_RUN) prog = clampf((now - s_sn.at) / (float)RUN_MS, 0, 1);
    else if (s_sn.phase == S_CLEAN) prog = 1.0f;
    else if (s_sn.phase == S_CAUGHT) prog = s_sn.caughtProg;
    const VP pose = s_sn.phase == S_CAUGHT ? VP::STARTLED : (s_sn.phase == S_CLEAN ? VP::CHEER : (s_sn.phase == S_RUN || s_sn.phase == S_COUNT ? VP::CROUCH : VP::NONE));
    // His run ends short of the HIGH SCORES panel (x 487 on): at 470 his
    // right half stood in it.
    const int sqx = 150 + (int)(prog * 250) + ox;
    int bob = 0;
    if (P(14) && s_sn.phase == S_RUN) bob = (int)(3 * fabsf(sinf(now / 160.0f)));
    if (P(14)) t.fillEllipse(sqx, 306 + oy, 34, 6, mixc(Theme::BG, Theme::BLACK, 200));
    if (P(14) && s_sn.phase == S_RUN) for (int q = 0; q < 3; q++) {
        const uint32_t ph = (now + q * 140) % 420;
        t.fillCircle(sqx - 30 - (int)(ph / 14), 302 + oy - (int)(ph / 40), 2 + (int)(ph / 140), mixc(Theme::BG, Theme::WHITE, 110 - (int)(ph / 4)));
    }
    squachy(t, -1, pose, sqx, 304 + oy - bob, 1.4f, now);
    if (P(18) && s_sn.phase == S_CAUGHT) {   // the "!" over his head
        const uint32_t ca = now - s_sn.caughtAt;
        const int ey = 170 + oy - (ca < 200 ? (int)(200 - ca) / 10 : 0);
        Theme::drawBangersOutline(t, sqx - 6, ey, "!", Theme::BLACK, Theme::BangersSize::LG, 3);
        Theme::drawBangersText(t, sqx - 6, ey, "!", Theme::RED, Theme::BangersSize::LG);
    }
    const char* big; uint16_t bc;
    char cnt[4];
    switch (s_sn.phase) {
        case S_COUNT: snprintf(cnt, sizeof cnt, "%lu", (unsigned long)(COUNT_MS / 1000 - (now - s_sn.at) / 1000)); big = cnt; bc = Theme::VAPOR_YELLOW; break;
        case S_RUN:   { const bool go = P(17) && now - s_sn.at < 600; big = go ? "GO!" : "SNEAK!"; bc = go ? Theme::GREEN : Theme::CYAN; break; }
        case S_CAUGHT: big = "CAUGHT!"; bc = Theme::PINK; break;
        case S_CLEAN: big = "CLEAN RUN"; bc = Theme::GREEN; break;
        default:      big = live ? (P(20) && (now / 600) % 2 ? "PRESS START" : "READY?") : (Tab5Sense::fresh(now) ? "CALIBRATE FIRST" : "NO RADIO"); bc = live ? Theme::CYAN : Theme::AMBER; break;
    }
    if (s_sn.phase == S_CAUGHT || s_sn.phase == S_CLEAN) {
        const int w = Theme::bangersTextWidth(big, Theme::BangersSize::LG) + 60;
        for (int k = 0; k < 4; k++) t.drawRect(300 - w / 2 + ox - k, 54 + oy - k, w + 2 * k, 60 + 2 * k, bc);
        headline(t, 300 + ox, 64 + oy, big, bc, 1, s_sn.phase == S_CAUGHT ? 4 : 0);
    } else if (s_sn.phase == S_COUNT && P(17)) {
        const uint32_t into = (now - s_sn.at) % 1000;
        for (int q = 0; q < 3; q++) { const int r = 20 + (int)(into / 6) + q * 14; if (r < 140) t.drawCircle(320, 70, r, mixc(Theme::BG, Theme::VAPOR_YELLOW, 220 - (int)(r * 1.5f))); }
        for (int k = 2; k >= 0; k--) headline(t, 320 - k * 2, 52 - k * 2, big, k == 0 ? bc : Theme::PINK, 1, 0);
        textT(t, 320, 112, now - s_sn.at < BASE_FROM_MS ? "GET IN POSITION" : "HOLD STILL... READING THE ROOM", Theme::VAPOR_YELLOW, 2, 1);
    } else {
        headline(t, 320, 38, big, bc, 1, 0);
    }
    if (s_sn.phase == S_CLEAN) {
        static const uint16_t* const CC[5] = { &Theme::PINK, &Theme::CYAN, &Theme::VAPOR_YELLOW, &Theme::GREEN, &Theme::VAPOR_PURPLE };
        for (int q = 0; q < 60; q++) {
            const int x = (int)(hashf(q) * 640), y = 18 + (int)(fmodf(now / 6.0f + hashf(q + 3) * 400, 316));
            t.fillRect(x, y, 4 + q % 3, 3, *CC[q % 5]);
        }
        if (P(19)) for (int f = 0; f < 3; f++) {   // fireworks
            const uint32_t cyc = (now + f * 450) % 1300;
            const int fx = 140 + (int)(hashf(f * 9 + (now + f * 450) / 1300) * 360), fy = 60 + (int)(hashf(f * 5 + (now + f * 450) / 1300) * 60);
            for (int q = 0; q < 16; q++) {
                const float a = q * 0.3927f, r = cyc / 18.0f;
                t.fillRect(fx + (int)(cosf(a) * r), fy + (int)(sinf(a) * r) + (int)(cyc * cyc / 90000), 2, 2, mixc(Theme::BG, *CC[(q + f) % 5], 255 - (int)(cyc / 6)));
            }
        }
    }
    const int mx = 140, my = 148, segs = 25;
    for (int q = 0; q < segs; q++) {
        const int v = (q + 1) * 100 / segs;
        const uint16_t c = v <= T * 2 / 3 ? Theme::GREEN : (v <= T ? Theme::AMBER : Theme::RED);
        t.fillRect(mx + q * 13 + ox, my + oy, 11, 14, v <= s_sn.peak ? c : mixc(Theme::BG, Theme::PURPLE, 45));
    }
    const int cur = live ? (s.score > 100 ? 100 : s.score) : 0;
    t.fillRect(mx + cur * segs * 13 / 100 - 1 + ox, my - 4 + oy, 3, 22, Theme::WHITE);
    t.fillRect(mx + MOTION_LINE * segs * 13 / 100 - 2 + ox, my - 7 + oy, 3, 28, Theme::AMBER);
    t.fillRect(mx + T * segs * 13 / 100 - 2 + ox, my - 7 + oy, 3, 28, Theme::VAPOR_PINK);
    char b[40];
    snprintf(b, sizeof b, "PEAK %u", (unsigned)s_sn.peak); textT(t, mx + ox, my + 18 + oy, b, Theme::WHITE, 2);
    snprintf(b, sizeof b, "STAY UNDER %u", (unsigned)T); textT(t, mx + segs * 13 + ox, my + 18 + oy, b, Theme::VAPOR_PINK, 2, 2);
    snprintf(b, sizeof b, "LEVEL %s", L.name); textT(t, 12, 22, b, Theme::VAPOR_PINK, 2);
    if (s_sn.phase == S_RUN) { snprintf(b, sizeof b, "TIME %.1f s", (now - s_sn.at) / 1000.0f); textT(t, P(15) ? 560 : 628, 22, b, Theme::CYAN, 2, 2); }
    if (P(16)) {   // stealth bar and heartbeat
        const int stealth = live ? 100 - (s.score > 100 ? 100 : s.score) : 0;
        // Fallout's sneak readout: hidden under half the target, caution up to it, danger past it.
        const int sc0 = live ? s.score : 0;
        const bool danger = sc0 >= T, caution = sc0 * 3 >= T * 2;
        textT(t, 12, 44, danger ? "[DANGER]" : (caution ? "[CAUTION]" : "[HIDDEN]"), danger ? Theme::RED : (caution ? Theme::AMBER : Theme::GREEN), 1);
        t.fillRect(70, 44, 80, 7, mixc(Theme::BG, Theme::PURPLE, 50));
        t.fillRect(70, 44, stealth * 80 / 100, 7, stealth > 70 ? Theme::GREEN : (stealth > 40 ? Theme::AMBER : Theme::RED));
        const float bpm = 70 + (live ? s.score : 0) * 1.2f;
        int py = 0;
        for (int x = 0; x < 120; x++) {
            const float ph = fmodf((now / 1000.0f + x / 160.0f) * bpm / 60.0f, 1.0f);
            const int y = 66 - (ph < 0.06f ? (int)(14 * sinf(ph / 0.06f * 3.14159f)) : (ph < 0.1f ? -(int)(5 * sinf((ph - 0.06f) / 0.04f * 3.14159f)) : 0));
            if (x) t.drawLine(12 + x - 1, py, 12 + x, y, Theme::RED);
            py = y;
        }
    }
    const uint16_t panel = mixc(Theme::BG, Theme::PURPLE, 30);
    t.fillRect(486, 186, 146, 110, panel); t.drawRect(486, 186, 146, 110, Theme::VAPOR_PINK);
    text(t, 559, 190, P(20) ? "HIGH SCORES" : "LAST RUNS", Theme::VAPOR_YELLOW, panel, 2, 1);
    if (!s_sn.runN) text(t, 559, 214, "none yet", mixc(panel, Theme::WHITE, 150), panel, 2, 1);
    for (uint8_t k = 0; k < s_sn.runN; k++) {
        const SRun& r = s_sn.runs[k];
        const bool ok = r.peak < r.target;
        if (P(20)) snprintf(b, sizeof b, "%c %c %3u", grade(r.peak, r.target, r.base), LEVELS[r.level].name[0], (unsigned)r.peak);
        else snprintf(b, sizeof b, "%c %3u %s", LEVELS[s_sn.runs[k].level].name[0], (unsigned)s_sn.runs[k].peak, ok ? "OK" : "X");
        text(t, 498, 210 + k * 17, b, ok ? Theme::GREEN : Theme::PINK, panel, 2);
    }
    const uint8_t* ghost = s_sn.best[s_sn.level] != 255 ? s_sn.bestTrace[s_sn.level] : nullptr;
    if (ghost) for (int j = 1; j < 100; j++) t.drawLine(170 + (j - 1) * 3, 322 - ghost[j - 1] * 9 / 10, 170 + j * 3, 322 - ghost[j] * 9 / 10, mixc(Theme::BG, Theme::VAPOR_PURPLE, 110));
    if (s_sn.phase == S_RUN || s_sn.phase == S_CAUGHT || s_sn.phase == S_CLEAN) {
        const int upto = s_sn.phase == S_RUN ? (int)((now - s_sn.at) / 100) : 100;
        for (int j = 1; j < upto && j < 100; j++) t.drawLine(170 + (j - 1) * 3, 322 - s_sn.trace[j - 1] * 9 / 10, 170 + j * 3, 322 - s_sn.trace[j] * 9 / 10, Theme::CYAN);
    }
    if (P(20) && (s_sn.phase == S_CAUGHT || s_sn.phase == S_CLEAN) && now - s_sn.at > 1200) {
        const int cx = 220, cy = 196;
        t.fillRect(cx - 80, cy, 160, 64, panel); t.drawRect(cx - 80, cy, 160, 64, s_sn.phase == S_CLEAN ? Theme::GREEN : Theme::PINK);
        const char g[2] = { grade(s_sn.peak, T, (uint8_t)s_sn.base), 0 };
        Theme::drawBangersOutline(t, cx - 66, cy + 10, g, Theme::BLACK, Theme::BangersSize::LG, 2);
        Theme::drawBangersText(t, cx - 66, cy + 10, g, g[0] == 'S' ? Theme::VAPOR_YELLOW : (g[0] == 'D' ? Theme::PINK : Theme::GREEN), Theme::BangersSize::LG);
        snprintf(b, sizeof b, "PEAK %u", (unsigned)s_sn.peak); text(t, cx - 30, cy + 10, b, Theme::WHITE, panel, 2);
        snprintf(b, sizeof b, "%s %s", L.name, s_sn.newBest ? "BEST!" : ""); text(t, cx - 30, cy + 30, b, s_sn.newBest ? Theme::GREEN : Theme::CYAN, panel, 2);
    } else if (!P(20) && s_sn.phase == S_CLEAN && s_sn.newBest) {
        t.fillRect(200, 200, 240, 44, panel); t.drawRect(200, 200, 240, 44, Theme::GREEN);
        text(t, 320, 206, "NEW BEST", Theme::GREEN, panel, 2, 1);
        snprintf(b, sizeof b, "peak %u on %s", (unsigned)s_sn.peak, L.name); text(t, 320, 224, b, Theme::CYAN, panel, 2, 1);
    }
}

// ============================================================================
// VITALS: SPECTRUM LAB (the owner's pick, pass 5 of the design page, 2026-11-08)
// ============================================================================
// A waterfall of every rhythm in the chest signal with the live spectrum over
// it, the breathing and heart numbers big on the right, the breath loop, the
// heart lock gauge and pulse, an event log, and Dr. Squach calling it.
const int VX = 40, VW = 340;                 // spectrum strip and waterfall
const int VSY = 54, VSH = 50;                // live spectrum
const int VWY = 110, VWH = 172;              // waterfall
const int VRX = 394;                         // right panel
const float V_SHOWN_S = 30.0f;               // waterfall seconds on screen (100 rows)

inline int vfx(float hz) {   // held just past the panel, whatever the estimate says
    const float x = VX + hz / (Vitals::BINS * Vitals::FS / 512.0f) * VW;
    return (int)(x < VX - 20 ? VX - 20 : (x > VX + VW + 20 ? VX + VW + 20 : x));
}
void f2(TFT_eSPI& t, int x, int y, const char* s, uint16_t col, uint8_t align = 0) {
    t.setTextFont(2); t.setTextSize(1); t.setTextWrap(false); t.setTextColor(col);
    const int w = t.textWidth(s);
    t.setCursor(align == 1 ? x - w / 2 : (align == 2 ? x - w : x), y);
    t.print(s);
}
const uint16_t* vLut() {
    static uint16_t lut[256]; static bool built = false;
    if (!built) {
        const uint16_t st[7] = { Theme::BG, mixc(Theme::BG, Theme::PURPLE, 110), Theme::PURPLE, Theme::PINK, Theme::AMBER, Theme::VAPOR_YELLOW, Theme::WHITE };
        for (int i = 0; i < 256; i++) { const float v = powf(i / 255.0f, 1.2f) * 6; const int k = v >= 6 ? 5 : (int)v; lut[i] = mixc(st[k], st[k + 1], (int)((v - k) * 256)); }
        built = true;
    }
    return lut;
}
void heartIcon(TFT_eSPI& t, int x, int y, int s, uint16_t c) {
    t.fillCircle(x - s / 2, y - s / 4, s / 2 + 1, c); t.fillCircle(x + s / 2, y - s / 4, s / 2 + 1, c);
    t.fillTriangle(x - s - 1, y - s / 6, x + s + 1, y - s / 6, x, y + s, c);
}
void drawVitals(TFT_eSPI& t, uint32_t now) {
    Vitals::update(now);
    const Vitals::Metrics& m = Vitals::metrics();
    const bool lock = m.conf > 0.35f, clean = m.snr > 15 && !m.motion && m.bpm > 0;
    const bool enough = Vitals::count() >= 200 && vitalsLink(now) == VLink::LIVE;
    char b[48];
    // ---- header
    Theme::drawBangersText(t, 26, 14, "SPECTRUM LAB", Theme::CYAN, Theme::BangersSize::MD);
    snprintf(b, sizeof b, "%s REC %u:%02u", (now / 500) % 2 ? "\x07" : " ", (unsigned)(m.session / 60), (unsigned)fmodf(m.session, 60));
    if ((now / 500) % 2) t.fillCircle(166, 30, 3, Theme::PINK);
    f2(t, 174, 22, b + 2, Theme::PINK);
    auto chip = [&](int x, const char* s, bool on, uint16_t c) {
        t.setTextFont(2); const int w = t.textWidth(s) + 10;
        if (on) t.fillRect(x, 22, w, 16, c); t.drawRect(x, 22, w, 16, c);
        f2(t, x + 5, 22, s, on ? Theme::BG : c);
    };
    chip(250, "BREATH LOCK", clean, Theme::CYAN);
    chip(352, "HEART LOCK", lock, Theme::VAPOR_PINK);
    // Parked, not joined: no saved WiFi to lend the radio, so it listens to
    // whatever transmits (fine for breathing, too uneven for the heart)
    if (Tab5Sense::fresh(now) && Tab5Sense::last().mode == SENSE_MODE_PARK) f2(t, 632, 22, "NO WIFI: LISTENING", Theme::AMBER, 2);
    else { snprintf(b, sizeof b, "%d READS/S", m.rate); f2(t, 632, 22, b, mixc(Theme::BG, Theme::WHITE, 150), 2); }
    // ---- live spectrum
    const float* sp = Vitals::spectrum();
    const uint16_t frame = Theme::PURPLE, panel = mixc(Theme::BG, Theme::PURPLE, 28);
    t.fillRect(VX, VSY, VW, VSH, panel);
    for (int k = 1; k < 4; k++) t.drawFastHLine(VX, VSY + k * VSH / 4, VW, mixc(Theme::BG, Theme::PURPLE, 60));
    auto band = [&](float a, float z, uint16_t c, const char* s) { t.fillRect(vfx(a), VSY - 4, vfx(z) - vfx(a), 2, c); textT(t, (vfx(a) + vfx(z)) / 2, VSY - 13, s, c, 1, 1); };
    band(0.13f, 0.6f, Theme::CYAN, "BREATHING"); band(0.8f, 2.0f, Theme::VAPOR_PINK, "HEART");
    int py[Vitals::BINS];
    for (int k = 0; k < Vitals::BINS; k++) py[k] = VSY + VSH - 1 - (int)(sp[k] * (VSH - 6));
    for (int x = VX + 1; x < VX + VW - 1; x++) {
        const float fb = (x - VX) * (float)Vitals::BINS / VW - 0.5f; int k = (int)fb; if (k < 0) k = 0; if (k > Vitals::BINS - 2) k = Vitals::BINS - 2;
        const float f = fb - k; const int y = (int)(py[k] + (py[k + 1] - py[k]) * (f < 0 ? 0 : f));
        const int h = VSY + VSH - 1 - y;
        if (h > 0) { t.drawFastVLine(x, y, h / 2 + 1, mixc(panel, Theme::VAPOR_PINK, 120)); t.drawFastVLine(x, y + h / 2, h - h / 2, mixc(panel, Theme::PURPLE, 110)); }
    }
    for (int k = 1; k < Vitals::BINS; k++) {
        const int x0 = VX + (k - 1) * VW / Vitals::BINS + VW / 128, x1 = VX + k * VW / Vitals::BINS + VW / 128;
        t.drawLine(x0, py[k - 1], x1, py[k], Theme::VAPOR_PINK); t.drawLine(x0, py[k - 1] - 1, x1, py[k] - 1, mixc(Theme::BG, Theme::VAPOR_PINK, 140));
    }
    t.drawRect(VX, VSY, VW, VSH, frame);
    if (enough && !m.motion) {
        // the peak's bin, held inside the curve: a bad estimate once indexed
        // outside py[] and drew a line to a garbage height that took seconds
        const int pk = (int)lroundf(m.peakHz / Vitals::binHz(1));
        const int x = vfx(m.peakHz < 0 ? 0 : m.peakHz), yk = py[pk < 0 ? 0 : (pk >= Vitals::BINS ? Vitals::BINS - 1 : pk)];
        t.fillCircle(x, yk, 3, Theme::CYAN); t.drawLine(x, yk, x + 12, VSY + 12, Theme::CYAN); t.drawFastHLine(x + 12, VSY + 12, 56, Theme::CYAN);
        snprintf(b, sizeof b, "%.1f/MIN", m.peakHz * 60); f2(t, x + 14, VSY - 2 + 0, b, Theme::CYAN);
        const int x2 = vfx(m.peakHz * 2);
        if (x2 < VX + VW - 10) { for (int y = VSY + 4; y < VSY + VSH; y += 6) t.drawFastVLine(x2, y, 3, mixc(Theme::BG, Theme::CYAN, 130)); textT(t, x2 + 3, VSY + VSH - 10, "2x", mixc(Theme::BG, Theme::CYAN, 170), 1); }
        if (lock) {
            const int xh = vfx(m.hr / 60);
            if (xh < VX + VW) { t.fillCircle(xh, VSY + 26, 3, Theme::VAPOR_PINK); t.drawLine(xh, VSY + 26, xh + 12, VSY + 34, Theme::VAPOR_PINK); snprintf(b, sizeof b, "%d/MIN", (int)lroundf(m.hr)); f2(t, xh + 14, VSY + 27, b, Theme::VAPOR_PINK); }
        }
    }
    if (m.motion) f2(t, VX + VW / 2, VSY + 18, "BROADBAND: MOTION", Theme::PINK, 1);
    // ---- waterfall
    const uint16_t* lut = vLut();
    const uint8_t rows = Vitals::waterfallRows();
    const int shownRows = 100;
    for (int y = 0; y < VWH; y++) {
        const int r = y * shownRows / VWH;
        if (r >= rows) { t.drawFastHLine(VX, VWY + y, VW, Theme::BG); continue; }
        const uint8_t* row = Vitals::waterfallRow((uint8_t)r);
        for (int k = 0; k < Vitals::BINS; k++) {
            const int x0 = VX + k * VW / Vitals::BINS, xm = VX + (2 * k + 1) * VW / (2 * Vitals::BINS), x1 = VX + (k + 1) * VW / Vitals::BINS;
            const int nb = k + 1 < Vitals::BINS ? row[k + 1] : row[k];
            t.drawFastHLine(x0, VWY + y, xm - x0, lut[row[k]]);
            t.drawFastHLine(xm, VWY + y, x1 - xm, lut[(row[k] + nb) / 2]);
        }
    }
    { const int sy = VWY + (int)((now / 18) % VWH); t.drawFastHLine(VX, sy, VW, mixc(Theme::BG, Theme::CYAN, 150)); }
    t.drawRect(VX, VWY, VW, VWH, frame);
    for (int pm = 0; pm <= 150; pm += 15) {
        const int x = vfx(pm / 60.0f); if (x > VX + VW) break;
        t.drawFastVLine(x, VWY + VWH, pm % 30 ? 3 : 6, mixc(Theme::BG, Theme::WHITE, pm % 30 ? 90 : 160));
        if (pm % 30 == 0) { snprintf(b, sizeof b, "%d", pm); textT(t, x, VWY + VWH + 8, b, mixc(Theme::BG, Theme::WHITE, 160), 1, 1); }
    }
    textT(t, VX + VW, VWY + VWH + 18, "/MIN", mixc(Theme::BG, Theme::WHITE, 130), 1, 2);
    textT(t, VX - 4, VWY + 2, "NOW", mixc(Theme::BG, Theme::WHITE, 150), 1, 2);
    textT(t, VX - 4, VWY + VWH - 9, "-30s", mixc(Theme::BG, Theme::WHITE, 150), 1, 2);
    if (enough && !m.motion) { const int x = vfx(m.peakHz); t.fillTriangle(x, VWY + 9, x - 5, VWY + 1, x + 5, VWY + 1, Theme::CYAN); }
    if (lock) { const int x = vfx(m.hr / 60); if (x < VX + VW) t.fillTriangle(x, VWY + 9, x - 5, VWY + 1, x + 5, VWY + 1, Theme::VAPOR_PINK); }
    // events on the waterfall's time edge
    Vitals::Event ev[Vitals::EVN];
    const uint8_t ne = Vitals::events(ev, Vitals::EVN);
    auto evText = [&](const Vitals::Event& e, char* out, size_t n) -> uint16_t {
        switch (e.kind) {
            case Vitals::Ev::MOVED: snprintf(out, n, "MOVED"); return Theme::PINK;
            case Vitals::Ev::HOLD: snprintf(out, n, "HOLD %u s", (unsigned)e.val); return Theme::AMBER;
            case Vitals::Ev::SIGH: snprintf(out, n, "SIGH"); return Theme::VAPOR_PURPLE;
            default: snprintf(out, n, "HEART LOCK %u", (unsigned)e.val); return Theme::VAPOR_PINK;
        }
    };
    for (uint8_t i = 0; i < ne; i++) {
        const float age = m.session - ev[i].t; if (age < 0 || age > V_SHOWN_S) continue;
        const int y = VWY + (int)(age / V_SHOWN_S * VWH);
        char s[24]; const uint16_t c = evText(ev[i], s, sizeof s);
        t.fillRect(VX + 1, y, 10, 2, c);
        const int w = (int)strlen(s) * 6 + 4; t.fillRect(VX + 13, y - 4, w, 10, Theme::BG); textT(t, VX + 15, y - 3, s, c, 1);
    }
    // ---- not listening yet
    if (!enough) {
        const char* l1 = vitalsLink(now) == VLink::JOINING ? "JOINING YOUR WIFI" : vitalsLink(now) == VLink::NO_SIGNAL ? "NO CHEST SIGNAL" : vitalsLink(now) == VLink::LIVE ? "LISTENING" : "ASKING THE RADIO";
        const char* l2 = vitalsLink(now) == VLink::NO_SIGNAL ? "no word from the radio" : "sit still near the Tab5 for 20 s";
        t.fillRect(VX + 40, VWY + 50, VW - 80, 60, Theme::BG); t.drawRect(VX + 40, VWY + 50, VW - 80, 60, Theme::AMBER);
        headline(t, VX + VW / 2, VWY + 54, l1, Theme::AMBER, 1, 0);
        f2(t, VX + VW / 2, VWY + 90, l2, Theme::AMBER, 1);
    }
    // ---- the big numbers
    auto big = [&](int x, const char* label, const char* val, uint16_t c, const char* sub) {
        f2(t, x, 40, label, c);
        // Bangers has no '.', so a decimal is the whole part, a drawn point, then the tenths
        char whole[12]; strncpy(whole, val, sizeof whole - 1); whole[sizeof whole - 1] = 0;
        char* dot = strchr(whole, '.'); const char* tenths = nullptr;
        if (dot) { *dot = 0; tenths = dot + 1; }
        Theme::drawBangersOutline(t, x, 54, whole, Theme::BLACK, Theme::BangersSize::LG, 2);
        Theme::drawBangersText(t, x, 54, whole, c, Theme::BangersSize::LG);
        if (tenths) {
            const int wx = x + Theme::bangersTextWidth(whole, Theme::BangersSize::LG) + 2;
            t.fillRect(wx, 82, 6, 6, Theme::BLACK); t.fillRect(wx + 1, 83, 4, 4, c);
            Theme::drawBangersOutline(t, wx + 9, 54, tenths, Theme::BLACK, Theme::BangersSize::LG, 2);
            Theme::drawBangersText(t, wx + 9, 54, tenths, c, Theme::BangersSize::LG);
        }
        textT(t, x + 1, 94, "/MIN", mixc(Theme::BG, Theme::WHITE, 140), 1);
        textT(t, x + 116, 94, sub, mixc(Theme::BG, Theme::WHITE, 140), 1, 2);
    };
    char v1[12], v2[12], s1[16], s2[16];
    if (enough && !m.motion && m.bpm > 0) snprintf(v1, sizeof v1, "%.1f", m.bpm); else snprintf(v1, sizeof v1, "--");
    if (lock) snprintf(v2, sizeof v2, "%d", (int)lroundf(m.hr)); else snprintf(v2, sizeof v2, "--");
    snprintf(s1, sizeof s1, "%d dB", enough ? m.snr : 0);
    snprintf(s2, sizeof s2, "%d%% SURE", (int)lroundf(m.conf * 100));
    big(VRX, "BREATHS", v1, Theme::CYAN, s1);
    big(VRX + 122, "HEART  EXP", v2, Theme::VAPOR_PINK, s2);
    t.drawFastHLine(VRX, 106, 238, mixc(Theme::BG, Theme::PURPLE, 90));
    // ---- breath loop: the wave against how fast it changes, a comet
    const int lx = VRX + 58, ly = 160, R = 46;
    arc(t, lx, ly, R, 0, 6.2832f, mixc(Theme::BG, Theme::PURPLE, 70));
    t.drawFastHLine(lx - R, ly, 2 * R, mixc(Theme::BG, Theme::PURPLE, 60)); t.drawFastVLine(lx, ly - R, 2 * R, mixc(Theme::BG, Theme::PURPLE, 60));
    textT(t, lx + R - 2, ly - R + 2, "IN", mixc(Theme::BG, Theme::WHITE, 130), 1, 2); textT(t, lx - R + 2, ly + R - 9, "OUT", mixc(Theme::BG, Theme::WHITE, 130), 1);
    auto lpt = [&](int k, int& x, int& y) {
        const float a = Vitals::breathAt((uint16_t)k), d = (Vitals::breathAt((uint16_t)(k > 0 ? k - 1 : 0)) - Vitals::breathAt((uint16_t)(k + 1))) * Vitals::FS / 2 / 1.8f;
        float ax = a / 1.3f, dy = d / 1.3f; const float r2 = ax * ax + dy * dy;
        if (r2 > 1) { const float k2 = 1 / sqrtf(r2); ax *= k2; dy *= k2; }   // onto the rim, not into a square
        x = lx + (int)(ax * R); y = ly - (int)(dy * R);
    };
    if (Vitals::count() > 160) {
        int px, pyy; lpt(150, px, pyy);
        for (int k = 149; k >= 0; k--) {
            int x, y; lpt(k, x, y);
            const int f = 256 - k * 256 / 150;
            const uint16_t c = Vitals::motionAt((uint16_t)k) ? mixc(Theme::BG, Theme::PINK, f) : mixc(Theme::BG, mixc(Theme::VAPOR_PURPLE, Theme::CYAN, f), f);
            t.drawLine(px, pyy, x, y, c); if (k < 50) t.drawLine(px, pyy + 1, x, y + 1, c);
            px = x; pyy = y;
        }
        t.fillCircle(px, pyy, 3, Theme::WHITE);
    }
    textT(t, lx, ly + R + 4, "BREATH LOOP", Theme::CYAN, 1, 1);
    // ---- heart lock gauge
    const int gx = VRX + 186, gy = 156, gr = 36;
    const float a0 = 3.14159f * 0.75f, a1 = 3.14159f * 2.25f;
    arc(t, gx, gy, gr, a0, a1, mixc(Theme::BG, Theme::PURPLE, 80), 6);
    const float fill = clampf(m.conf / 0.82f, 0, 1);
    if (fill > 0.01f) arc(t, gx, gy, gr, a0, a0 + (a1 - a0) * fill, lock ? Theme::VAPOR_PINK : mixc(Theme::BG, Theme::VAPOR_PINK, 140), 6);
    { const float at = a0 + (a1 - a0) * 0.35f / 0.82f; t.drawLine(gx + (int)(cosf(at) * (gr - 8)), gy + (int)(sinf(at) * (gr - 8)), gx + (int)(cosf(at) * (gr + 3)), gy + (int)(sinf(at) * (gr + 3)), Theme::AMBER); }
    const float beat = lock && m.hr > 30 ? fmodf(now / (60000.0f / m.hr), 1.0f) : 0.5f;
    heartIcon(t, gx, gy - 4, lock ? (beat < 0.14f ? 10 : 7) : 6, lock ? Theme::PINK : mixc(Theme::BG, Theme::PURPLE, 120));
    textT(t, gx, gy + 12, lock ? "LOCKED" : "SEARCHING", lock ? Theme::VAPOR_PINK : mixc(Theme::BG, Theme::WHITE, 130), 1, 1);
    textT(t, gx, gy + gr + 6, "HEART LOCK", Theme::VAPOR_PINK, 1, 1);
    // ---- pulse trace
    const int PY = 221, PH = 21;
    t.fillRect(VRX, PY, 238, PH, panel); t.drawRect(VRX, PY, 238, PH, mixc(Theme::BG, Theme::PURPLE, 90));
    if (lock) {
        int px = -1, pyy = 0;
        for (int k = 119; k >= 0; k--) { const int x = VRX + 237 - k * 237 / 119, y = PY + PH / 2 - (int)(Vitals::heartAt((uint16_t)k) * (PH / 2 - 2)); if (px >= 0) t.drawLine(px, pyy, x, y, Theme::PINK); px = x; pyy = y; }
    } else textT(t, VRX + 119, PY + 8, "heart band: waiting for a still minute", mixc(Theme::BG, Theme::WHITE, 120), 1, 1);
    // ---- stats
    char r1[12], r2[12], r3[12], r4[12];
    if (m.rhythm >= 0) snprintf(r1, sizeof r1, "%d%%", m.rhythm); else snprintf(r1, sizeof r1, "--");
    if (m.ie > 0) snprintf(r2, sizeof r2, "1:%.1f", m.ie); else snprintf(r2, sizeof r2, "--");
    if (m.depth >= 0) snprintf(r3, sizeof r3, "%d%%", m.depth); else snprintf(r3, sizeof r3, "--");
    snprintf(r4, sizeof r4, "%u:%02u", (unsigned)(m.still / 60), (unsigned)fmodf(m.still, 60));
    const char* L[4] = { "RHYTHM", "IN:EX", "DEPTH", "STILL" }; const char* V[4] = { r1, r2, r3, r4 };
    for (int k = 0; k < 4; k++) { const int x = VRX + (k % 2) * 122, y = 248 + (k / 2) * 18; f2(t, x, y, L[k], mixc(Theme::BG, Theme::WHITE, 140)); f2(t, x + 114, y, V[k], Theme::WHITE, 2); }
    // ---- lab log and Dr. Squach
    t.fillRect(0, 302, 640, 30, mixc(Theme::BG, Theme::PURPLE, 40)); t.drawFastHLine(0, 302, 640, Theme::PURPLE);
    f2(t, 8, 308, "LAB LOG", Theme::AMBER);
    int lxp = 74;
    if (!ne) f2(t, lxp, 308, "nothing yet: sit still and breathe", mixc(Theme::BG, Theme::WHITE, 130));
    for (uint8_t i = 0; i < ne && i < 4; i++) {
        char s[24], line[32]; const uint16_t c = evText(ev[i], s, sizeof s);
        snprintf(line, sizeof line, "%u:%02u %s", (unsigned)(ev[i].t / 60), (unsigned)fmodf(ev[i].t, 60), s);
        t.setTextFont(2); const int w = t.textWidth(line);
        if (lxp + w > 410) break;
        f2(t, lxp, 308, line, c); lxp += w + 14;
    }
    if (m.motion) snprintf(b, sizeof b, "Whoa, you moved!");
    else if (enough && m.pause > 8) snprintf(b, sizeof b, "Holding it? %d s...", (int)m.pause);
    else if (lock && (now / 5000) % 2) snprintf(b, sizeof b, "Heartbeat: ~%d", (int)lroundf(m.hr));
    else if (clean) snprintf(b, sizeof b, "Clean lock: %d/min", (int)lroundf(m.bpm));
    else if (!enough) snprintf(b, sizeof b, "Sit still for me...");
    else snprintf(b, sizeof b, "Hmm, noisy...");
    t.setTextFont(2); const int bw = t.textWidth(b) + 14;
    t.fillRect(580 - bw, 305, bw, 24, Theme::BG); t.drawRect(580 - bw, 305, bw, 24, Theme::CYAN);
    f2(t, 587 - bw, 309, b, Theme::CYAN);
    // Squachy himself in the corner (the app drew a goggled "Dr. Squach" head
    // here; the real one reads better, asked for 2026-11-17).
    {
        using VP = Squachy::VisitPose;
        const VP pose = m.motion ? VP::STARTLED : (lock || clean) ? VP::CHEER : !enough ? VP::NONE : VP::CROUCH;
        squachy(t, -1, pose, 612, 331, 0.55f, now);
    }
}

}  // namespace

// ============================================================================
void uiSensingInit(TFT_eSPI& t) {
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
    // Back to whichever screen was showing last, across a reboot too.
    s_mode = (uint8_t)(Settings::sensePage() % M_COUNT);
    s_enterMs = s_lastMs = millis();
    s_lastWord = nullptr;
    if (s_glass) memset(s_glass, 0, (size_t)G_W * G_H * 2);
    if (s_phos) memset(s_phos, 0, (size_t)GL_W * ST_H);
}

void uiSensingTick(TFT_eSPI& t, uint32_t now, bool advance) {
    (void)advance;
    senseCydVitals(s_mode == M_VITALS);
    Vitals::update(now);
    const uint32_t dt = s_lastMs && now > s_lastMs ? (now - s_lastMs > 200 ? 200 : now - s_lastMs) : 16;
    s_lastMs = now;
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
    if (s_mode == M_SCOPE) drawScope(t, now, dt);
    else if (s_mode == M_SNEAK) drawSneak(t, now);
    else if (s_mode == M_VITALS) drawVitals(t, now);
    else drawSensing(t, now, dt);
    if (s_mode == M_SENSING) statusLine(t, now);
    modeDots(t);
    radioBadge(t, now);
    // SCOPE, SNEAK and VITALS are EXPERIMENTAL: more than "something moved"
    // asks a lot of one router's signal. Beside the page dots.
    if (s_mode != M_SENSING) {
        const int x = 320 + (M_COUNT - 1) * 7 + 14;
        t.drawRect(x - 3, 2, 12 * 6 + 5, 12, Theme::AMBER);
        textT(t, x, 4, "EXPERIMENTAL", Theme::AMBER, 1);
    }
    buttons(t);
}

SensingTap uiSensingTap(int x, int y, int screenW, int screenH, uint32_t now) {
    const Theme::ButtonBarGeom g = Theme::computeButtonBar(screenW, screenH);
    if (y >= g.y && y <= g.y + g.h) {
        int k = -1;
        for (int i = 0; i < 3; i++) if (x >= g.x[i] && x <= g.x[i] + g.w[i]) k = i;
        if (k == 0) return SensingTap::BACK;
        if (s_mode == M_SNEAK) {
            if (k == 1) sneakStart(now);
            if (k == 2 && s_sn.phase != S_COUNT && s_sn.phase != S_RUN) s_sn.level = (uint8_t)((s_sn.level + 1) % 3);
            return SensingTap::NONE;
        }
        if (s_mode == M_VITALS && k == 1) { Vitals::newSession(now); return SensingTap::NONE; }
        if (k == 1) return SensingTap::RECAL;
        if (k == 2) return SensingTap::SLEEP;
        return SensingTap::NONE;
    }
    if (s_mode == M_SENSING && y >= 232 && y < 250)
        for (int k = 0; k < 3; k++) if (x >= 8 + k * 122 && x < 8 + k * 122 + 118) s_tab = (uint8_t)k;
    if (y < 18 && x >= 290 && x <= 350) uiSensingSwipe(1);
    return SensingTap::NONE;
}

void uiSensingSwipe(int dir) {
    s_mode = (uint8_t)((s_mode + M_COUNT + (dir < 0 ? -1 : 1)) % M_COUNT);
    Settings::setSensePage(s_mode);
    s_lastWord = nullptr;
    if (s_glass) memset(s_glass, 0, (size_t)G_W * G_H * 2);
    if (s_phos) memset(s_phos, 0, (size_t)GL_W * ST_H);
}

uint8_t uiSensingPage() { return s_mode; }

// No keyboard on the Tab5, and its screen is touch: these are for the other boards.
bool uiSensingKey(char, uint32_t) { return false; }
uint8_t uiSensingStops(int, int, int16_t*, int16_t*, uint8_t) { return 0; }
void uiSensingBannerStop(int screenW, int screenH, int16_t& x, int16_t& y) { x = (int16_t)(screenW / 2); y = (int16_t)(screenH / 2); }

#endif  // SENSE_CYD_APP && TAB5
