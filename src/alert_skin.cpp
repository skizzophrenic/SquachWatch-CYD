// SquachWatch-CYD — the alert card's skins (see alert_skin.h).
//
// Ported from the alert card lab's skins (2026-11-20): every coordinate here is
// the lab's, in the 320x240 design space, and SC maps it onto the panel.
#include "alert_skin.h"
#include "theme.h"
#include <Arduino.h>
#include <math.h>
#include <string.h>
#include <stdio.h>

namespace AlertSkin {


// ---- the frame, for reading back ------------------------------------------------
static uint8_t* s_fb = nullptr;
static int s_fbW = 0, s_fbH = 0;
void setFrame(uint8_t* buf, int w, int h) { s_fb = buf; s_fbW = w; s_fbH = h; }

static inline uint8_t to332(uint16_t c) { return (uint8_t)(((c >> 8) & 0xE0) | ((c >> 6) & 0x1C) | ((c >> 3) & 0x03)); }
static const uint8_t LV[8] = { 0, 36, 73, 109, 146, 182, 219, 255 };
static const uint8_t LB[4] = { 0, 85, 170, 255 };
static inline uint16_t from332(uint8_t i) {
    const uint8_t r = LV[i >> 5], g = LV[(i >> 2) & 7], b = LB[i & 3];
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// ---- where things landed, for the taps --------------------------------------------
struct Rect { int16_t x, y, w, h; bool ok; };
static Rect s_btn[5], s_queue, s_chip[4];
void forget() { for (auto& r : s_btn) r.ok = false; s_queue.ok = false; for (auto& r : s_chip) r.ok = false; }
static bool get(const Rect& r, int& x, int& y, int& w, int& h) { if (!r.ok) return false; x = r.x; y = r.y; w = r.w; h = r.h; return true; }
bool buttonRect(uint8_t i, int& x, int& y, int& w, int& h) { return i < 5 && get(s_btn[i], x, y, w, h); }
bool queueRect(int& x, int& y, int& w, int& h) { return get(s_queue, x, y, w, h); }
bool chipRect(uint8_t i, int& x, int& y, int& w, int& h) { return i < 4 && get(s_chip[i], x, y, w, h); }

// ---- the colour roles in use --------------------------------------------------------
static Roles P;
static uint8_t s_rolesFor = 0xFF;
static void useTheme(uint8_t palette) {
    if (palette == s_rolesFor) return;
    s_rolesFor = palette;
    if (palette == Theme::PALETTE_DUSK || palette >= Theme::PALETTE_COUNT) { P = duskRoles(); return; }
    const Theme::Palette& p = Theme::kPalettes[palette];
    const uint16_t pal[12] = { p.bg, p.taskbar, p.purple, p.cyan, p.pink, p.vaporPink, p.vaporPurple,
                               p.vaporBlue, p.vaporYellow, p.green, p.amber, p.red };
    P = deriveRoles(pal);
}
static uint16_t mix565(uint16_t a, uint16_t b, float t) {
    const int ar = (a >> 11) * 255 / 31, ag = ((a >> 5) & 63) * 255 / 63, ab = (a & 31) * 255 / 31;
    const int br = (b >> 11) * 255 / 31, bg = ((b >> 5) & 63) * 255 / 63, bb = (b & 31) * 255 / 31;
    return snap565(ar + (int)((br - ar) * t), ag + (int)((bg - ag) * t), ab + (int)((bb - ab) * t));
}
static int luma(uint16_t c) { const int r = (c >> 11) * 255 / 31, g = ((c >> 5) & 63) * 255 / 63, b = (c & 31) * 255 / 31; return (r * 54 + g * 183 + b * 19) >> 8; }
static uint16_t inkOn(uint16_t face) { return luma(face) > 150 ? P.night : P.pale; }

// ---- lookup tables over the 256 colours of an 8-bit frame ----------------------------
static uint8_t L_frost[256], L_sheen[256], L_smoke[256], L_scan[256];
static uint8_t s_lutsFor = 0xFF;
static void buildLuts() {
    if (s_lutsFor == s_rolesFor) return;
    s_lutsFor = s_rolesFor;
    for (int i = 0; i < 256; i++) {
        const uint16_t c = from332((uint8_t)i);
        L_frost[i] = to332(mix565(c, P.lav, 0.34f));
        L_sheen[i] = to332(mix565(c, P.pale, 0.5f));   // the theme's lightest, not white
        L_smoke[i] = to332(mix565(c, P.night, 0.9f));
        L_scan[i]  = to332(mix565(c, P.deep, 0.3f));   // the theme's darkest, not black
    }
}

// ---- the design space on the panel ----------------------------------------------------
struct SC {
    TFT_eSPI& t;
    float k; int ox, oy, pw, ph, ts;
    Theme::BangersSize bs;
    SC(TFT_eSPI& tt) : t(tt) {
        pw = t.width(); ph = t.height();
        const float kx = pw / 320.0f, ky = ph / 240.0f;
        k = kx < ky ? kx : ky;
        ox = (int)((pw - 320 * k) / 2); oy = (int)((ph - 240 * k) / 2);
        ts = k >= 2.0f ? (int)k : 1;
        bs = k >= 2.0f ? Theme::BangersSize::LG : Theme::BangersSize::MD;
    }
    int X(float x) const { return ox + (int)lroundf(x * k); }
    int Y(float y) const { return oy + (int)lroundf(y * k); }
    int L(float v) const { const int r = (int)lroundf(v * k); return r < 1 ? 1 : r; }
    // design-space extents of the whole panel, for the desk
    float dx0() const { return -ox / k; }
    float dy0() const { return -oy / k; }
    float dw() const { return pw / k; }
    float dh() const { return ph / k; }

    void fillRect(float x, float y, float w, float h, uint16_t c) { const int x0 = X(x), y0 = Y(y), x1 = X(x + w), y1 = Y(y + h); if (x1 > x0 && y1 > y0) t.fillRect(x0, y0, x1 - x0, y1 - y0, c); }
    void drawRect(float x, float y, float w, float h, uint16_t c) { const int x0 = X(x), y0 = Y(y); int ww = X(x + w) - x0, hh = Y(y + h) - y0; if (ww < 1) ww = 1; if (hh < 1) hh = 1; t.drawRect(x0, y0, ww, hh, c); }
    void hline(float x, float y, float w, uint16_t c) { const int x0 = X(x); int ww = X(x + w) - x0; if (ww > 0) t.drawFastHLine(x0, Y(y), ww, c); }
    void vline(float x, float y, float h, uint16_t c) { const int y0 = Y(y); int hh = Y(y + h) - y0; if (hh > 0) t.drawFastVLine(X(x), y0, hh, c); }
    void line(float x0, float y0, float x1, float y1, uint16_t c) { t.drawLine(X(x0), Y(y0), X(x1), Y(y1), c); }
    void fillCircle(float cx, float cy, float r, uint16_t c) { t.fillCircle(X(cx), Y(cy), (int)lroundf(r * k), c); }
    void drawCircle(float cx, float cy, float r, uint16_t c) { t.drawCircle(X(cx), Y(cy), (int)lroundf(r * k), c); }
    void fillTri(float x0, float y0, float x1, float y1, float x2, float y2, uint16_t c) { t.fillTriangle(X(x0), Y(y0), X(x1), Y(y1), X(x2), Y(y2), c); }
    void fillRR(float x, float y, float w, float h, float r, uint16_t c) {
        const int x0 = X(x), y0 = Y(y), ww = X(x + w) - x0, hh = Y(y + h) - y0; int rr = (int)lroundf(r * k);
        if (rr * 2 > ww) rr = ww / 2; if (rr * 2 > hh) rr = hh / 2; if (ww > 0 && hh > 0) t.fillRoundRect(x0, y0, ww, hh, rr, c);
    }
    void drawRR(float x, float y, float w, float h, float r, uint16_t c) {
        const int x0 = X(x), y0 = Y(y), ww = X(x + w) - x0, hh = Y(y + h) - y0; int rr = (int)lroundf(r * k);
        if (rr * 2 > ww) rr = ww / 2; if (rr * 2 > hh) rr = hh / 2; if (ww > 0 && hh > 0) t.drawRoundRect(x0, y0, ww, hh, rr, c);
    }
    // the 5x7 font: design widths, drawn at ts
    float textW(const char* s) const { return strlen(s) * 6.0f * ts / k; }
    float text(float x, float y, const char* s, uint16_t c) {
        t.setTextFont(1); t.setTextSize(ts); t.setTextWrap(false); t.setTextColor(c);
        t.setCursor(X(x), Y(y) + (int)((8 * k - 8 * ts) / 2)); t.print(s);
        return x + textW(s);
    }
    void textC(float cx, float y, const char* s, uint16_t c) { text(cx - textW(s) / 2, y, s, c); }
    void textR(float rx, float y, const char* s, uint16_t c) { text(rx - textW(s), y, s, c); }
    // Bangers: MD, or LG on the big panels
    float bangW(const char* s) const { return Theme::bangersTextWidth(s, bs) / k; }
    float bang(float x, float y, const char* s, uint16_t c) { Theme::drawBangersText(t, X(x), Y(y), s, c, bs); return x + bangW(s); }
    void bangC(float cx, float y, const char* s, uint16_t c) { bang(cx - bangW(s) / 2, y, s, c); }
    // The type's icon cuts its bite in Theme::BG; here the colour behind it is
    // whatever the title bar is, so BG is lent for the one call.
    void icon(DetectionType ty, float cx, float cy, float s, uint16_t bg) {
        const uint16_t keep = Theme::BG; Theme::BG = bg;
        Theme::drawTypeIcon(t, ty, X(cx), Y(cy), (int)lroundf(s * k));
        Theme::BG = keep;
    }
    // one pixel in panel space: straight into the frame when there is one
    void px(int x, int y, uint16_t c) {
        if (x < 0 || y < 0 || x >= pw || y >= ph) return;
        if (s_fb && s_fbW == pw) s_fb[y * s_fbW + x] = to332(c); else t.drawPixel(x, y, c);
    }
    void mark(Rect& r, float x, float y, float w, float h) { r.x = (int16_t)X(x); r.y = (int16_t)Y(y); r.w = (int16_t)(X(x + w) - X(x)); r.h = (int16_t)(Y(y + h) - Y(y)); r.ok = true; }
};

// The type's icon right after its word, as tall as the word; returns where it ends.
static float iconAfter(SC& s, DetectionType ty, float x, float yTop, float h, uint16_t bg) {
    const float sz = h / 2.5f, cx = x + 4 + sz * 1.35f, cy = yTop + h - sz * 1.17f;
    s.icon(ty, cx, cy, sz, bg);
    return cx + sz * 1.5f;
}

// 2x2 ordered dither between two colours over a design rectangle, in panel pixels
static void dither(SC& s, float x, float y, float w, float h, uint16_t a, uint16_t b, int level) {
    static const uint8_t M[2][2] = { { 0, 2 }, { 3, 1 } };
    const int x0 = s.X(x), y0 = s.Y(y), x1 = s.X(x + w), y1 = s.Y(y + h);
    for (int yy = y0; yy < y1; yy++) for (int xx = x0; xx < x1; xx++) s.px(xx, yy, M[yy & 1][xx & 1] < level ? b : a);
}
static void halfShadow(SC& s, float x, float y, float w, float h, uint16_t c) {
    const int x0 = s.X(x), y0 = s.Y(y), x1 = s.X(x + w), y1 = s.Y(y + h);
    for (int yy = y0; yy < y1; yy++) for (int xx = x0 + ((yy - y0) & 1); xx < x1; xx += 2) s.px(xx, yy, c);
}
// remap a panel region's pixels through a table; mask in panel pixels
template <typename F>
static void remap(SC& s, int x0, int y0, int w, int h, const uint8_t* lut, F inside) {
    if (x0 < 0) { w += x0; x0 = 0; } if (y0 < 0) { h += y0; y0 = 0; }
    if (x0 + w > s.pw) w = s.pw - x0; if (y0 + h > s.ph) h = s.ph - y0;
    if (w <= 0 || h <= 0) return;
    if (s_fb && s_fbW == s.pw) {
        for (int y = y0; y < y0 + h; y++) { uint8_t* row = s_fb + y * s_fbW; for (int x = x0; x < x0 + w; x++) if (inside(x, y)) row[x] = lut[row[x]]; }
    } else {
        for (int y = y0; y < y0 + h; y++) for (int x = x0; x < x0 + w; x++) if (inside(x, y)) s.t.drawPixel(x, y, from332(lut[to332(s.t.readPixel(x, y))]));
    }
}
struct RR { int x, y, w, h, r; bool in(int px, int py) const {
    if (px < x || py < y || px >= x + w || py >= y + h) return false;
    const int cx = px < x + r ? x + r : px > x + w - 1 - r ? x + w - 1 - r : px;
    const int cy = py < y + r ? y + r : py > y + h - 1 - r ? y + h - 1 - r : py;
    return (px - cx) * (px - cx) + (py - cy) * (py - cy) <= r * r; } };
static RR rrOf(SC& s, float x, float y, float w, float h, float r) { return { s.X(x), s.Y(y), s.X(x + w) - s.X(x), s.Y(y + h) - s.Y(y), (int)lroundf(r * s.k) }; }

// ---- time --------------------------------------------------------------------------------
struct Clk { uint32_t age, amb, qa; uint8_t queue; };
static float ease(float k) { if (k <= 0) return 0; if (k >= 1) return 1; const float m = 1 - k; return 1 - m * m * m; }
static float easeT(uint32_t age, uint32_t dur) { return ease((float)age / dur); }

static void bevel(SC& s, float x, float y, float w, float h, uint16_t hi, uint16_t hi2, uint16_t lo, uint16_t lo2, bool two = true) {
    s.hline(x, y, w, hi); s.vline(x, y, h, hi); s.hline(x, y + h - 1, w, lo); s.vline(x + w - 1, y, h, lo);
    if (two) { s.hline(x + 1, y + 1, w - 2, hi2); s.vline(x + 1, y + 1, h - 2, hi2); s.hline(x + 1, y + h - 2, w - 2, lo2); s.vline(x + w - 2, y + 1, h - 2, lo2); }
}
static void sunIcon(SC& s, float cx, float cy, float r, uint16_t bg) { s.fillCircle(cx, cy, r, P.sun2); for (int i = 0; i < 3; i++) s.fillRect(cx - r - 1, cy + i * 2, 2 * r + 3, 1, bg); }
static void zoomRects(SC& s, const float from[4], const float to[4], float k, const uint16_t* cols, int n) {
    for (int j = 0; j < n; j++) {
        const float kk = ease(k - j * 0.09f); if (kk <= 0) continue;
        float r[4]; for (int i = 0; i < 4; i++) r[i] = from[i] + (to[i] - from[i]) * kk;
        s.drawRect(r[0], r[1], r[2] < 2 ? 2 : r[2], r[3] < 2 ? 2 : r[3], cols[j]);
    }
}

// ---- the opener's sunset, moving -----------------------------------------------------------
struct Wall { float horizon, sunX, sunR; bool hill; };
static uint16_t skyAt(float f) {   // the band colour a fraction of the way down the sky
    return f < 0.30f ? P.deep : f < 0.52f ? P.night : f < 0.80f ? P.plum : f < 0.95f ? P.mauve : P.rose;
}
// It holds still. Moving, it changed most of the screen every frame, and an
// SPI panel shows that as tearing: a horizontal seam where the old frame meets
// the new one. Only small things move on the skins now.
static void wallpaper(SC& s, float x, float y, float w, float h, const Wall& o, uint32_t) {
    const uint32_t amb = 0;
    const float hz = y + h * o.horizon, sh = hz - y;
    const float stops[6] = { 0, 0.30f, 0.52f, 0.80f, 0.95f, 1.0f };
    const uint16_t cols[5] = { P.deep, P.night, P.plum, P.mauve, P.rose };
    for (int i = 0; i < 5; i++) {
        const float y0 = y + stops[i] * sh, y1 = y + stops[i + 1] * sh;
        s.fillRect(x, y0, w, y1 - y0, cols[i]);
        if (i < 4) for (int k = 1; k <= 3; k++) dither(s, x, y1 - 4 + k, w, 1, cols[i], cols[i + 1], k);
    }
    // stars, twinkling
    uint32_t seed = 7;
    const int n = (int)(w * sh / 260);
    for (int i = 0; i < n; i++) {
        seed = seed * 1103515245u + 12345u; const float rx = (seed >> 8 & 0xFFFF) / 65535.0f;
        seed = seed * 1103515245u + 12345u; const float ry = (seed >> 8 & 0xFFFF) / 65535.0f;
        seed = seed * 1103515245u + 12345u; const float ph = (seed >> 8 & 0xFFFF) / 65535.0f;
        // A slow glint now and then, not a field of blinking: on the T-Deck's
        // quick frames the old two-second twinkle read as flashing.
        const float tw = fmodf(amb / 7000.0f + ph, 1.0f);
        s.fillRect(x + rx * w, y + ry * sh * 0.5f, 1, 1, tw < 0.04f ? P.pale : ph < 0.5f ? P.slate2 : P.slate);
    }
    if (o.sunR > 0) {   // the striped sun, its stripes sinking
        const float sx = x + w * o.sunX, r = o.sunR, cy = hz - r * 0.55f;
        const uint16_t SUN[4] = { P.sun1, P.sun2, P.sun3, P.mauve };
        const float sink = fmodf(amb / 700.0f, 7.0f);   // a pixel every 0.7 s: drifting, not strobing
        for (int yy = (int)-r; yy <= (int)(r * 0.55f); yy++) {
            const float py = cy + yy; if (py >= hz) break;
            int band = (int)(((yy + r) / (r * 1.55f)) * 4); if (band > 3) band = 3;
            if (yy > -r * 0.15f) {
                const float kk = yy + r * 0.15f - sink; const int gap = 1 + (int)((yy + r * 0.15f) / (r * 0.7f) * 4);
                const float m = fmodf(fmodf(kk, 7.0f) + 7.0f, 7.0f); if (m < gap) continue;
            }
            const float half = floorf(sqrtf(fmaxf(0, r * r - yy * yy)));
            s.fillRect(sx - half, py, half * 2 + 1, 1, SUN[band]);
        }
    }
    if (o.hill) {   // Bliss, at dusk
        for (int xx = 0; xx < (int)w; xx++) { const float top = hz - 26 + roundf(cosf((xx / w - 0.35f) * 3.1f) * -18 + 18); s.fillRect(x + xx, top, 1, y + h - top, xx % 3 ? P.mauve : P.rose); }
        return;
    }
    s.hline(x, hz - 1, w, P.rose2); s.hline(x, hz, w, P.peach);
    s.fillRect(x, hz + 1, w, y + h - hz - 1, P.deep); dither(s, x, hz + 1, w, 3, P.mauve, P.deep, 2);
    const float roll = fmodf(amb / 3000.0f, 1.0f), vx = x + w * o.sunX;   // one line's step in 3 s
    for (int k = 0; k < 14; k++) { const float gy = hz + 2 + roundf((k + roll) * (k + roll) * 0.55f); if (gy >= y + h) break; s.hline(x, gy, w, k + roll < 4 ? P.plum2 : P.teal); }
    for (int k = -12; k <= 12; k++) s.line(vx + k * 3, hz + 2, vx + k * 34, y + h - 1, abs(k) > 6 ? P.slate2 : P.teal);
}
static void desk(SC& s, bool sunset, const Wall& o, uint32_t amb, uint16_t plain) {
    if (sunset) wallpaper(s, s.dx0(), s.dy0(), s.dw(), s.dh(), o, amb);
    else s.t.fillRect(0, 0, s.pw, s.ph, plain);
}

// ---- the shared content ------------------------------------------------------------------
struct K {   // the content's colours, per skin
    uint16_t bg, text, muted, accent, type, line, lineHi, stop, here, pin, warn, good,
             boxBg, boxEdge, check, ladBg, ladEdge, ladOld, ladNew, ladHot, ladText, chipA, chipB, key, rule;
};
static K KD() { return { P.indigo, P.pale, P.dim, P.peach, P.lav, P.mauve, P.rose2, P.pale, P.peach, P.peach, P.gold, P.mint,
                         P.pale, P.night, P.night, P.night, P.slate2, P.slate, P.aqua, P.peach, P.pale, P.gold, P.lav, P.dim, P.slate2 }; }
static K KL() { return { P.pale, P.night, P.plum2, P.mauve, P.plum2, P.rose2, P.mauve, P.plum2, P.rose, P.rose, P.mauve, P.mint,
                         P.pale, P.plum2, P.mauve, P.indigo, P.plum2, P.slate, P.aqua, P.peach, P.pale, P.mauve, P.plum2, P.plum2, P.lav }; }
struct Box { float x, y, w, h; };
struct CO { bool verdict = true, type = true, chips = true, chipRound = false, fill = true; float ladW = 60; };

static uint16_t verdictCol(const Card& c, const K& k) {
    switch (c.verdict) { case Verdict::FOLLOWING: return k.accent; case Verdict::SPAM: return k.warn; case Verdict::MET: return k.type; default: return k.text; }
}
// fit a string into a design width, cutting it short
static const char* fitTo(SC& s, const char* src, float maxW, char* buf, size_t n) {
    const int cap = (int)(maxW / (6.0f * s.ts / s.k));
    size_t len = strlen(src); if ((int)len <= cap || cap <= 0) return src;
    if ((size_t)cap >= n) return src;
    memcpy(buf, src, cap); buf[cap] = 0; return buf;
}
static void trail(SC& s, const Card& c, float L, float R, float ay, const K& k, const Clk& ck, bool dwell) {
    const float ly = ay + 30, xs0 = L + 34, xs2 = R - 20, span = xs2 - xs0;
    s.fillRect(xs0 - 14, ly - 2, xs2 + 10 - (xs0 - 14), 4, k.line); s.hline(xs0 - 14, ly - 2, xs2 + 10 - (xs0 - 14), k.lineHi);
    if (!c.nStops) { s.text(xs0 - 14, ly + 12, "JUST HEARD. NO TRAIL YET.", k.muted); return; }
    float xs[Detection::STOPS_N];
    for (uint8_t i = 0; i < c.nStops; i++) xs[i] = xs0 + span * c.stops[i].pos / 1000.0f;
    const uint8_t last = c.nStops - 1;
    for (uint8_t i = 1; i < c.nStops; i++) if (c.stops[i].leg[0]) s.textC((xs[i - 1] + xs[i]) / 2, ly - 12, c.stops[i].leg, k.muted);
    const float hx = xs[last];
    if (c.hereLast) {
        const float pulse = (ck.amb % 1600) / 1600.0f;
        if (pulse < 0.85f) s.drawCircle(hx, ly, 9 + roundf(pulse * 9), mix565(k.pin, k.bg, ease(pulse / 0.85f)));
    }
    for (uint8_t i = 0; i < c.nStops; i++) {
        const float nx = xs[i]; const bool isLast = i == last, here = isLast && c.hereLast;
        const float r = isLast ? 8 : 7;
        s.fillCircle(nx, ly, r, k.bg); s.drawCircle(nx, ly, r, k.stop); s.drawCircle(nx, ly, r - 1, k.stop);
        s.fillCircle(nx, ly, isLast ? 4 : 3, here ? k.here : k.stop);
        if (!here) { s.t.setTextSize(s.ts * 2); const float tw = strlen(c.stops[i].time) * 12.0f * s.ts / s.k;
                     s.t.setTextFont(1); s.t.setTextColor(k.text); s.t.setCursor(s.X(nx - tw / 2), s.Y(ay)); s.t.print(c.stops[i].time); s.t.setTextSize(s.ts); }
        s.textC(nx, ly + 12, c.stops[i].place, here ? k.here : k.text);
        float yy = ly + 22;
        if (here) { s.textC(nx, yy, c.stops[i].time, k.here); yy += 10; }
        if (dwell) s.textC(nx, yy, c.stops[i].dwell, k.muted);
    }
    if (c.hereLast) {
        const float bob = roundf(sinf(ck.amb / 260.0f) * 1.2f);
        s.fillCircle(hx, ly - 17 + bob, 7, k.pin); s.fillTri(hx - 6, ly - 14 + bob, hx + 6, ly - 14 + bob, hx, ly - 4, k.pin); s.fillCircle(hx, ly - 17 + bob, 3, k.bg);
    }
}
static float boxCheck(SC& s, float x, float y, const Tick& tk, const K& k) {
    s.fillRect(x, y, 9, 9, k.boxBg); s.drawRect(x, y, 9, 9, k.boxEdge);
    if (tk.on) { s.line(x + 2, y + 4, x + 3, y + 6, k.check); s.line(x + 3, y + 6, x + 6, y + 2, k.check); s.line(x + 2, y + 5, x + 3, y + 7, k.check); s.line(x + 3, y + 7, x + 6, y + 3, k.check); }
    s.text(x + 12, y + 1, tk.label, tk.on ? k.text : k.muted);
    return x + 12 + s.textW(tk.label) + 8;
}
static void ticks(SC& s, const Card& c, float x, float y, const K& k, bool narrow) {
    for (int i = 0; i < 4; i++) {
        Tick tk = c.ticks[i];
        if (narrow && i == 1) snprintf(tk.label, sizeof tk.label, "ALL STOPS");
        x = boxCheck(s, x, y, tk, k);
    }
}
static float ladder(SC& s, const Card& c, float x, float y, float w, const K& k) {
    for (uint8_t i = 0; i < c.sparkN; i++) {
        float len = (c.spark[i] + 100) / 60.0f * (w - 2); if (len < 2) len = 2; if (len > w) len = w;
        const bool newest = i == c.sparkN - 1;
        s.fillRect(x, y + i * 3, len, 2, newest ? k.ladHot : mix565(k.ladOld, k.ladNew, c.sparkN > 1 ? (float)i / (c.sparkN - 1) : 1));
    }
    return y + c.sparkN * 3;
}
// ---- the info card in a skin's window (drawInfo below) ----
static InfoBody s_infoBody = nullptr;
static uint8_t  s_infoN = 0;   // the buttons the body asked for, set as it draws
static void infoBody(SC& s, Box B, uint16_t bg) {
    s.fillRect(B.x, B.y, B.w, B.h, bg);
    const int x0 = s.X(B.x), y0 = s.Y(B.y);
    s_infoN = s_infoBody(s.t, x0, y0, s.X(B.x + B.w) - x0, s.Y(B.y + B.h) - y0);
}
static const char* infoLabel(int n, int i) {
    static const char* const L3[3] = { "BACK", "MORE", "GOT IT" };
    static const char* const L2[2] = { "BACK", "GOT IT" };
    return n == 3 ? L3[i] : L2[i];
}

// The fact rows, in order WHO, MAC, SEEN, OWNER (a tracker), CONF. When only
// `rows` fit, CONF goes first, then SEEN, then MAC: a tracker's OWNER row
// always shows (NOT SET UP has no chip, so the row is the only place it says
// it). Returns how many rows it filled in.
static int factRows(const Card& c, int rows, const char* keys[5], const char* vals[5]) {
    static const uint8_t byRank[5] = { 0, 3, 1, 2, 4 };   // WHO, OWNER, MAC, SEEN, CONF
    static const char* const allK[5] = { "WHO", "MAC", "SEEN", "OWNER", "CONF" };
    const char* allV[5] = { c.who, c.mac, c.seen, c.ownerRow, c.conf };
    bool on[5] = { false, false, false, false, false };
    for (int r = 0, n = 0; r < 5 && n < rows; r++) { const uint8_t i = byRank[r]; if (i == 3 && !c.ownerRow[0]) continue; on[i] = true; n++; }
    int n = 0;
    for (int i = 0; i < 5; i++) if (on[i]) { keys[n] = allK[i]; vals[n] = allV[i]; n++; }
    return n;
}

static void content(SC& s, const Card& c, Box B, const K& k, const Clk& ck, CO o = CO()) {
    if (s_infoBody) { infoBody(s, B, k.bg); return; }   // the info card's middle instead
    if (o.fill) s.fillRect(B.x, B.y, B.w, B.h, k.bg);
    const float px = B.x + B.w - o.ladW - 4, R = px - 6;
    float y = B.y + 4;
    bool chipsDone = !o.chips;
    char buf[48];
    if (o.verdict) {
        float x = s.bang(B.x + 6, y - 3, c.verdictWord, verdictCol(c, k));
        if (o.type) {
            const char* tl = c.typeLabel;
            if (x + 6 + s.bangW(tl) > B.x + B.w - 4) tl = c.typeShort;
            if (x + 6 + s.bangW(tl) <= B.x + B.w - 4) { x = s.bang(x + 6, y - 3, tl, k.type); x = iconAfter(s, c.type, x, y - 1, 22, k.bg); }
        }
        if (!chipsDone) {
            const float wa = c.owner[0] ? s.textW(c.owner) + 10 : 0, wm = s.textW(c.met) + 10, need = wa + wm + 18;
            if (x + need < B.x + B.w - 4) {
                x += 8;
                const char* labs[2] = { c.owner, c.met }; const uint16_t cols[2] = { c.ownerAway ? k.chipA : k.muted, k.chipB };
                for (int i = 0; i < 2; i++) {
                    if (!labs[i][0]) continue;
                    const float w = s.textW(labs[i]) + 10;
                    if (o.chipRound) s.drawRR(x, y + 5, w, 13, 4, cols[i]); else s.drawRect(x, y + 5, w, 13, cols[i]);
                    s.text(x + 5, y + 8, labs[i], cols[i]); x += w + 5;
                }
                chipsDone = true;
            }
        }
        y += 27;
    }
    const float avail = B.y + B.h - y;
    const bool ownerRow = c.ownerRow[0] != 0;
    int rows = ownerRow ? 5 : 4; bool dwell = true; float need = 72 + 15 + rows * 10 + 2;
    while (need > avail && rows > 2) { rows--; need -= 10; }
    if (need > avail) { dwell = false; need -= 10; }
    trail(s, c, B.x, R, y + 2, k, ck, dwell); y += dwell ? 74 : 64;
    ticks(s, c, B.x + 6, y, k, R - B.x < 236); y += 14;
    s.hline(B.x + 6, y - 2, R - B.x - 4, k.rule);
    const char* keys[5]; const char* vals[5];
    const int nRows = factRows(c, rows, keys, vals);
    const uint16_t confCol = c.confLevel >= 2 ? k.good : c.confLevel == 1 ? k.warn : P.rose;
    const float vx = B.x + (ownerRow ? 42 : 36), maxW = R - vx - (chipsDone ? 0 : 60);
    for (int i = 0; i < nRows; i++) {
        const bool isConf = keys[i][0] == 'C', isOwner = keys[i][0] == 'O';
        s.text(B.x + 6, y + 1 + i * 10, keys[i], k.key);
        s.text(vx, y + 1 + i * 10, fitTo(s, vals[i], i < 2 ? maxW : R - vx, buf, sizeof buf), isConf ? confCol : isOwner ? (c.ownerAway ? k.chipA : k.text) : k.text);
    }
    // No room for the chips: MET goes at the right of the rows (the OWNER row
    // already says what the owner chip would).
    if (!chipsDone) s.textR(R, ownerRow ? y + 1 : y + 11, fitTo(s, c.met, 58, buf, sizeof buf), k.chipB);
    if (!chipsDone && !ownerRow && c.owner[0]) s.textR(R, y + 1, c.owner, c.ownerAway ? k.chipA : k.muted);
    // the ladder column
    const float ly0 = o.verdict ? B.y + 30 : B.y + 4; float lh = B.y + B.h - 4 - ly0; if (lh > 126) lh = 126;
    s.fillRect(px, ly0, o.ladW, lh, k.ladBg); s.drawRect(px, ly0, o.ladW, lh, k.ladEdge);
    const float ey = ladder(s, c, px + 5, ly0 + 5, o.ladW - 10, k);
    char db[8]; snprintf(db, sizeof db, "%d", (int)c.rssi);
    s.bangC(px + o.ladW / 2, ey + 2, db, k.ladText);
    if (ey + 44 < ly0 + lh) s.textC(px + o.ladW / 2, ey + 31, "dBm", k.muted);
    s.textC(px + o.ladW / 2, fminf(ey + 42, ly0 + lh - 10), c.trend, c.trendUp ? k.good : k.muted);
}

// ---- buttons: one routine, many faces ------------------------------------------------------
enum class BK : uint8_t { BEVEL, ROUND, PILL, TAB, TASKBAR, BEOS, AMIGA, NEXT, XPB, AQUAB };
struct BO { float x0 = 4, x1 = 316, gap = 4, y = 205, h = 31; bool keys = false, defRing = false, gloss = false; uint16_t face = 0; };
static void keyLine(SC& s, float cx, float y, const char* label, uint16_t ink) { s.hline(cx - s.textW(label) / 2, y + 9, 5, ink); }
static void face(SC& s, BK kind, float x, float y, float w, float h, const char* b, bool def, bool sunk, const BO& o, const Clk& ck) {
    const float cx = x + w / 2, ty = y + (h - 8) / 2;
    if (sunk) {   // held down: WATCHING, MUTED, RINGING
        s.fillRect(x, y, w, h, P.pale); s.drawRect(x, y, w, h, P.night); s.textC(cx, ty, b, P.night); return;
    }
    switch (kind) {
        case BK::BEVEL: { const uint16_t f = def ? P.rose : P.plum2; s.fillRect(x, y, w, h, f); bevel(s, x, y, w, h, P.lav, P.slate2, P.night, P.indigo); s.textC(cx, ty, b, inkOn(f)); if (o.keys) keyLine(s, cx, ty, b, inkOn(f)); break; }
        case BK::ROUND: {
            if (def && o.defRing) { s.drawRR(x - 3, y - 3, w + 6, h + 6, 8, P.pale); s.drawRR(x - 2, y - 2, w + 4, h + 4, 7, P.pale); }
            const uint16_t f = def ? P.rose : P.plum; s.fillRR(x, y, w, h, 6, f); s.drawRR(x, y, w, h, 6, P.pale); s.textC(cx, ty, b, inkOn(f)); break; }
        case BK::PILL: { const uint16_t f = def ? P.rose : P.plum2, g = def ? P.rose2 : P.slate2; s.fillRR(x, y, w, h, 12, f); if (o.gloss) s.fillRR(x + 3, y + 2, w - 6, 12, 6, g); s.drawRR(x, y, w, h, 12, P.lav); s.textC(cx, ty, b, inkOn(o.gloss ? g : f)); break; }
        case BK::TAB: { const uint16_t f = def ? P.rose : P.night, ink = inkOn(f); s.fillRect(x, y, w, h, f); s.drawRect(x, y, w, h, def ? P.peach : P.plum2);
            if (o.keys) { char kk[4] = { '[', b[0], ']', 0 }; s.textC(cx, y + 4, kk, def ? ink : P.rose2); s.textC(cx, y + 16, b, ink); } else s.textC(cx, ty, b, ink); break; }
        case BK::TASKBAR: { const uint16_t f = def ? P.rose : P.plum; s.fillRR(x, y, w, h, 4, f); s.drawRR(x, y, w, h, 4, def ? P.peach : P.slate2); s.textC(cx, ty, b, inkOn(f)); keyLine(s, cx, ty, b, inkOn(f)); break; }
        case BK::BEOS: s.fillRR(x, y, w, h, 3, P.lav); s.hline(x + 3, y + 1, w - 6, P.pale); s.hline(x + 3, y + h - 2, w - 6, P.dim); s.drawRR(x, y, w, h, 3, P.night); if (def) s.drawRR(x - 2, y - 2, w + 4, h + 4, 4, P.night); s.textC(cx, ty, b, P.night); break;
        case BK::AMIGA: s.fillRect(x, y, w, h, o.face); bevel(s, x, y, w, h, P.pale, 0, P.night, 0, false); bevel(s, x + 1, y + 1, w - 2, h - 2, P.pale, 0, P.night, 0, false); s.textC(cx, ty, b, def ? P.gold : P.pale); break;
        case BK::NEXT:
            s.fillRect(x, y, w, h, P.dim); bevel(s, x, y, w, h, P.pale, P.lav, P.night, P.slate2);
            if (def) { s.text(cx - s.textW(b) / 2 - 6, ty, b, P.night); const float rx = cx + s.textW(b) / 2 + 2; s.vline(rx + 6, ty, 6, P.night); s.hline(rx, ty + 5, 7, P.night); s.fillTri(rx, ty + 5, rx + 3, ty + 2, rx + 3, ty + 8, P.night); }
            else s.textC(cx, ty, b, P.night);
            break;
        case BK::XPB:
            if (def) { s.fillRR(x, y, w, h, 9, P.mint); s.hline(x + 6, y + 2, w - 12, P.pale); s.drawRR(x, y, w, h, 9, P.teal); s.bangC(cx, y + 1, b, P.night); }
            else { s.fillRR(x, y, w, h, 3, P.pale); s.hline(x + 2, y + h - 3, w - 4, P.lav); s.hline(x + 2, y + h - 2, w - 4, P.lav); s.drawRR(x, y, w, h, 3, P.slate2); s.textC(cx, ty, b, P.night); }
            break;
        case BK::AQUAB: {
            const float pulse = def ? (sinf(ck.amb / 320.0f) + 1) / 2 : 0;
            const uint16_t f = def ? mix565(P.slate2, P.aqua, pulse) : P.pale;
            s.fillRR(x, y, w, h, h / 2, f); s.fillRR(x + 2, y + h / 2, w - 4, h / 2 - 1, 8, def ? P.aqua : P.lav);
            s.fillRR(x + 5, y + 2, w - 10, 7, 3, mix565(f, P.pale, 0.6f)); s.drawRR(x, y, w, h, h / 2, P.plum2);
            s.textC(cx, ty, b, P.night); break; }
    }
}
static void buttons(SC& s, const Card& c, BK kind, const BO& o, const Clk& ck) {
    // The info card has two or three (BACK, MORE, GOT IT) in the same faces.
    const int n = s_infoBody ? (s_infoN == 3 ? 3 : 2) : 5;
    const float w = ((o.x1 - o.x0) - o.gap * (n - 1)) / n;
    float x = o.x0;
    for (int i = 0; i < n; i++) {
        const char* b = s_infoBody ? infoLabel(n, i) : c.btn[i];
        face(s, kind, x, o.y, w, o.h, b, i == 0, s_infoBody ? false : c.sunk[i], o, ck);
        s.mark(s_btn[i], x, o.y, w, o.h);
        Theme::hotButton(s_btn[i].x, s_btn[i].y, s_btn[i].w, s_btn[i].h, s_infoBody ? b : c.hot[i]);
        x += w + o.gap;
    }
}
// SNOOZE was tapped: the four chips, in the skin's own button faces, over the facts
static void snoozeChips(SC& s, const Card& c, BK kind, const BO& o, const Clk& ck, uint16_t bg, uint16_t edge, uint16_t ink) {
    static const char* const SNZ[4] = { "10 MIN", "30 MIN", "1 HOUR", "4 HOURS" };
    if (!c.picking) return;
    s.fillRect(o.x0, 152, o.x1 - o.x0, 50, bg); s.drawRect(o.x0, 152, o.x1 - o.x0, 50, edge);
    s.text(o.x0 + 6, 156, "QUIET THIS ONE FOR:", ink);
    const float w = ((o.x1 - o.x0 - 12) - 3 * 6) / 4;
    for (int i = 0; i < 4; i++) { const float x = o.x0 + 6 + i * (w + 6); face(s, kind, x, 167, w, 30, SNZ[i], false, false, o, ck); s.mark(s_chip[i], x, 167, w, 30); }
}
static void markQueue(SC& s, float x, float y, float w, float h) { s.mark(s_queue, x, y, w, h); }
static bool blinkOn(const Clk& ck, uint32_t dur, uint32_t period) { return ck.qa < dur && ((ck.qa / period) % 2 == 0); }

// ===================== the eleven skins =====================
// 1 -- SUNSET 95: Windows 95 bones in the opener's colours
static void sSunset(SC& s, const Card& c, const Clk& ck, bool sunset) {
    const float inset = 4, X0 = inset, Y0 = inset, WW = 320 - 2 * inset, HH = 202 - inset;
    desk(s, sunset, { 0.62f, 0.5f, 60, false }, ck.amb, P.plum);
    BO bo; bo.keys = true;
    if (ck.age < 360) {
        const float k = easeT(ck.age, 360), from[4] = { 2, 222, 90, 14 }, to[4] = { X0 + 3, Y0 + 3, WW - 6, 29 };
        float r[4]; for (int i = 0; i < 4; i++) r[i] = from[i] + (to[i] - from[i]) * k;
        s.fillRect(r[0], r[1], r[2], r[3], P.mauve); s.text(r[0] + 4, r[1] + 3, "ALERT.EXE", P.pale);
        buttons(s, c, BK::BEVEL, bo, ck); return;
    }
    s.fillRect(X0, Y0, WW, HH, P.plum); bevel(s, X0, Y0, WW, HH, P.lav, P.slate2, P.night, P.indigo);
    const uint16_t tb[5] = { P.night, P.plum2, P.mauve, P.rose, P.sun3 };
    for (int i = 0; i < 5; i++) {
        const float x0 = X0 + 3 + roundf(i * (WW - 6) / 5), x1 = X0 + 3 + roundf((i + 1) * (WW - 6) / 5);
        s.fillRect(x0, Y0 + 3, x1 - x0, 29, tb[i]); if (i) dither(s, x0 - 2, Y0 + 3, 4, 29, tb[i - 1], tb[i], 2);
    }
    float x = s.bang(X0 + 8, Y0 + 4, c.verdictWord, P.peach);
    const char* tl = x + 8 + s.bangW(c.typeLabel) < X0 + WW - 40 ? c.typeLabel : c.typeShort;
    if (x + 8 + s.bangW(tl) < X0 + WW - 40) { x = s.bang(x + 8, Y0 + 4, tl, P.pale); if (x + 24 < X0 + WW - 40) iconAfter(s, c.type, x, Y0 + 6, 22, tb[(int)((x + 12 - X0) * 5 / WW) > 4 ? 4 : (int)((x + 12 - X0) * 5 / WW)]); }
    const float bxs[2] = { X0 + WW - 34, X0 + WW - 18 }; const char* gl[2] = { "_", "X" };
    for (int i = 0; i < 2; i++) { s.fillRect(bxs[i], Y0 + 10, 14, 13, P.plum); bevel(s, bxs[i], Y0 + 10, 14, 13, P.lav, P.slate2, P.night, P.indigo); s.textC(bxs[i] + 7, Y0 + 13, gl[i], P.pale); }
    // the menu bar holds the chips and the queue
    s.fillRect(X0 + 3, Y0 + 32, WW - 6, 13, P.indigo);
    float mx = X0 + 8;
    if (c.owner[0]) { s.text(mx, Y0 + 35, c.owner, c.ownerAway ? P.gold : P.dim); s.hline(mx, Y0 + 43, 5, P.gold); mx += s.textW(c.owner) + 16; }
    if (c.met[0]) { s.text(mx, Y0 + 35, c.met, P.lav); s.hline(mx, Y0 + 43, 5, P.lav); }
    if (c.queue) {
        char more[16]; snprintf(more, sizeof more, "+%u MORE >", (unsigned)c.queue);
        const float mw = s.textW(more), bx = X0 + WW - 8 - mw;
        const bool bl = blinkOn(ck, 540, 90);
        if (bl) s.fillRect(bx - 3, Y0 + 33, mw + 6, 11, P.aqua);
        s.text(bx, Y0 + 35, more, bl ? P.night : P.aqua); markQueue(s, bx - 3, Y0 + 32, mw + 6, 13);
    }
    s.fillRect(X0 + 4, Y0 + 47, WW - 8, HH - 51, P.night); bevel(s, X0 + 4, Y0 + 47, WW - 8, HH - 51, P.night, P.indigo, P.lav, P.slate2);
    K k = KD(); k.bg = P.night;
    CO o; o.verdict = false; o.chips = false;
    content(s, c, { X0 + 6, Y0 + 49, WW - 12, HH - 55 }, k, ck, o);
    snoozeChips(s, c, BK::BEVEL, bo, ck, P.plum, P.lav, P.pale);
    buttons(s, c, BK::BEVEL, bo, ck);
}

#if ALERT_SKINS_ALL
// 2 -- NEON DESKTOP: the card split into windows, the buttons a taskbar
static void win(SC& s, float x, float y, float w, float h, const char* title, bool focus, bool close = true) {
    s.fillRect(x + 2, y + 2, w, h, P.deep);
    s.fillRect(x, y, w, h, P.night); s.drawRect(x, y, w, h, focus ? P.rose2 : P.slate2);
    const uint16_t t0 = focus ? P.mauve : P.indigo, t1 = focus ? P.rose : P.slate;
    s.fillRect(x + 1, y + 1, w - 2, 11, t0); dither(s, x + 1 + (w - 2) / 2, y + 1, (w - 2) / 2, 11, t0, t1, 2);
    if (title && title[0]) s.text(x + 5, y + 3, title, focus ? P.peach : P.lav);
    if (close) { s.fillRect(x + w - 12, y + 2, 9, 9, focus ? P.rose2 : P.slate2); s.text(x + w - 10, y + 3, "x", P.night); }
}
static void sDesktop(SC& s, const Card& c, const Clk& ck, bool sunset) {
    desk(s, sunset, { 0.58f, 0.78f, 48, false }, ck.amb, P.deep);
    s.fillRect(0, 202, 320, 38, P.indigo); s.hline(0, 202, 320, P.rose2); s.hline(0, 203, 320, P.plum2);
    BO bo; bo.y = 207; bo.h = 29;
    if (s_infoBody) {
        win(s, 4, 4, 312, 196, "INFO", true);
        infoBody(s, { 5, 16, 310, 183 }, P.night);
        buttons(s, c, BK::TASKBAR, bo, ck);
        return;
    }
    auto shown = [&](int k) { return ck.age >= (uint32_t)k * 130; };
    if (c.queue && shown(3)) { win(s, 14, 2, 236, 40, "", false, false); if (c.queue > 1) win(s, 9, 7, 236, 40, "", false, false); }
    if (shown(0)) {
        win(s, 244, 20, 72, 112, "SIGNAL", false);
        K k = KD();
        const float ey = ladder(s, c, 250, 36, 60, k);
        char db[8]; snprintf(db, sizeof db, "%d", (int)c.rssi); s.bangC(280, ey + 4, db, P.pale); s.textC(280, ey + 33, c.trend, c.trendUp ? P.mint : P.dim);
    }
    if (shown(2)) {
        char title[24]; snprintf(title, sizeof title, "TRAIL   1 OF %u", (unsigned)(c.queue + 1));
        win(s, 4, 12, 236, 124, title, true);
        if (c.queue) markQueue(s, 4, 12, 236, 12);
        float x = s.bang(9, 25, c.verdictWord, verdictCol(c, KD()));
        const char* tl = x + 7 + s.bangW(c.typeLabel) < 236 ? c.typeLabel : c.typeShort;
        if (x + 7 + s.bangW(tl) < 236) { x = s.bang(x + 7, 25, tl, P.lav); if (x + 24 < 236) iconAfter(s, c.type, x, 27, 22, P.night); }
        K k = KD(); k.bg = P.night; k.stop = P.aqua; k.lineHi = P.rose2; k.line = P.plum2;
        trail(s, c, 6, 238, 56, k, ck, true);
    }
    if (shown(1)) {
        win(s, 10, 128, 300, 72, "DETAILS", false);
        K k = KD(); k.boxBg = P.night; k.boxEdge = P.aqua; k.check = P.mint;
        ticks(s, c, 16, 144, k, false);
        char buf[48];
        const bool ownerRow = c.ownerRow[0] != 0;
        const char* keys[5] = { "WHO", "MAC", "SEEN", ownerRow ? "OWNER" : "CONF", "CONF" }; const char* vals[5] = { c.who, c.mac, c.seen, ownerRow ? c.ownerRow : c.conf, c.conf };
        for (int i = 0; i < (ownerRow ? 5 : 4); i++) { const bool isConf = keys[i][0] == 'C', isOwner = keys[i][0] == 'O'; s.text(16, 155 + i * 9, keys[i], P.dim); s.text(46, 155 + i * 9, fitTo(s, vals[i], i < 2 ? 190 : 258, buf, sizeof buf), isConf ? (c.confLevel >= 2 ? P.mint : c.confLevel ? P.gold : P.rose) : isOwner ? (c.ownerAway ? P.gold : P.pale) : P.pale); }
        s.textR(304, 155, fitTo(s, c.met, 60, buf, sizeof buf), P.lav);
    }
    snoozeChips(s, c, BK::TASKBAR, bo, ck, P.night, P.rose2, P.pale);
    buttons(s, c, BK::TASKBAR, bo, ck);
}

#endif  // ALERT_SKINS_ALL

// 3 -- SYSTEM 7 AT DUSK
static void sMac(SC& s, const Card& c, const Clk& ck, bool sunset) {
    const float top = 18;
    if (sunset) wallpaper(s, s.dx0(), 14, s.dw(), s.dh() - 14 + s.dy0(), { 0.9f, 0.12f, 40, false }, ck.amb);
    else { s.t.fillRect(0, 0, s.pw, s.ph, P.plum); dither(s, s.dx0(), 14, s.dw(), s.dh(), P.plum, P.indigo, 1); }
    s.fillRect(s.dx0(), 0, s.dw(), 14, P.pale); s.hline(s.dx0(), 14, s.dw(), P.night); sunIcon(s, 10, 7, 5, P.pale);
    float x = 22; const char* menus[3] = { "ALERT", "TRAIL", "VIEW" };
    for (int i = 0; i < 3; i++) { s.text(x, 4, menus[i], P.night); x += s.textW(menus[i]) + 12; }
    s.textR(314, 4, (ck.amb / 500) % 2 ? "14 43" : "14:43", P.night);
    // the clock is the panel's real one in ui_alert's hands; here the card's last stop
    if (c.queue) {
        char more[16]; snprintf(more, sizeof more, "+%u MORE", (unsigned)c.queue);
        const float mw = s.textW(more) + 8, mx = 314 - s.textW("14:43") - 14 - mw; const bool bl = blinkOn(ck, 540, 90);
        if (bl) s.fillRect(mx, 1, mw, 12, P.night); s.text(mx + 4, 4, more, bl ? P.pale : P.night); markQueue(s, mx, 0, mw, 14);
    }
    BO bo; bo.x0 = 7; bo.x1 = 313; bo.gap = 7; bo.y = 206; bo.h = 29; bo.defRing = true;
    const float wx = 4, wy = top, ww = 310, wh = 198 - top;
    if (ck.age < 380) { const float from[4] = { 4, 2, 13, 11 }, to[4] = { wx, wy, ww, wh }; const uint16_t cols[4] = { P.pale, P.lav, P.dim, P.slate2 }; zoomRects(s, from, to, ck.age / 380.0f, cols, 4); return; }
    s.fillRect(wx + 2, wy + 2, ww, wh, P.deep); s.fillRect(wx, wy, ww, wh, P.indigo); s.drawRect(wx, wy, ww, wh, P.pale);
    s.fillRect(wx + 1, wy + 1, ww - 2, 13, P.plum);
    for (int i = 0; i < 6; i++) s.hline(wx + 3, wy + 3 + i * 2, ww - 6, i < 2 ? P.peach : i < 4 ? P.rose2 : P.mauve);
    s.fillRect(wx + 8, wy + 3, 11, 9, P.plum); s.drawRect(wx + 9, wy + 3, 9, 9, P.pale);
    s.hline(wx + 1, wy + 14, ww - 2, P.pale);
    const char* title = c.typeLabel; const float tw = s.textW(title) + 14;
    s.fillRect(160 - tw / 2 - 6, wy + 2, tw + 12, 11, P.plum); const float te = s.text(160 - tw / 2, wy + 4, title, P.pale); iconAfter(s, c.type, te - 2, wy + 2, 11, P.plum);
    CO o; o.type = false; o.chipRound = true;
    content(s, c, { wx + 2, wy + 16, ww - 4, wh - 18 }, KD(), ck, o);
    snoozeChips(s, c, BK::ROUND, bo, ck, P.indigo, P.pale, P.pale);
    buttons(s, c, BK::ROUND, bo, ck);
}

// 4 -- GLASS: frosted glass over the sunset
static void sGlass(SC& s, const Card& c, const Clk& ck, bool sunset) {
    const float fx = 3, fy = 3, fw = 314, fh = 196, cx = 9, cy = 33, cw = 302, ch = 160;
    BO bo; bo.gloss = true; bo.y = 205; bo.h = 31; bo.gap = 4; bo.x0 = 5; bo.x1 = 315;
    desk(s, sunset, { 0.66f, 0.5f, 70, false }, ck.amb, P.deep);
    if (ck.age < 420) {
        const float k = easeT(ck.age, 420), w = fmaxf(20, fw * k), h = fmaxf(12, fh * k), x = 160 - w / 2, y = 101 - h / 2;
        s.drawRR(x - 1, y - 1, w + 2, h + 2, 10, P.aqua); s.drawRR(x, y, w, h, 9, P.pale);
        return;
    }
    buildLuts();
    const RR frame = rrOf(s, fx, fy, fw, fh, 9), client = rrOf(s, cx, cy, cw, ch, 5);
    remap(s, frame.x, frame.y, frame.w, frame.h, L_frost, [&](int a, int b) { return frame.in(a, b) && !client.in(a, b); });
    remap(s, client.x, client.y, client.w, client.h, L_smoke, [&](int a, int b) { return client.in(a, b); });
    const uint32_t sw = ck.age - 420;
    if (sw < 760) {
        const float sp = (-60 + (sw / 760.0f) * 460) * s.k;
        remap(s, frame.x, frame.y, frame.w, frame.h, L_sheen, [&](int a, int b) { const float d = (a - s.ox) + (b - s.oy) * 0.6f - sp; return d > 0 && d < 16 * s.k && frame.in(a, b) && !client.in(a, b); });
    }
    s.drawRR(fx, fy, fw, fh, 9, P.pale); s.drawRR(cx, cy, cw, ch, 5, P.slate2);
    s.bang(13, 5, c.verdictWord, P.mauve);
    float x = s.bang(12, 4, c.verdictWord, P.pale);
    const char* tl = x + 8 + s.bangW(c.typeLabel) < 274 ? c.typeLabel : c.typeShort;
    if (x + 8 + s.bangW(tl) < 274) { s.bang(x + 9, 5, tl, P.plum2); x = s.bang(x + 8, 4, tl, P.peach); if (x + 24 < 278) iconAfter(s, c.type, x, 6, 22, P.slate2); }
    s.fillRR(280, 8, 30, 15, 6, P.rose); s.fillRect(283, 9, 24, 5, P.rose2); s.textC(295, 12, "X", P.pale);
    K k = KD(); k.bg = P.night; k.stop = P.aqua; k.lineHi = P.aqua; k.line = P.plum2; k.check = P.mint; k.boxBg = P.night; k.boxEdge = P.aqua;
    CO o; o.verdict = false; o.fill = false;
    content(s, c, { cx + 2, cy + 2, cw - 4, ch - 4 }, k, ck, o);
    if (c.queue) {
        char more[16]; snprintf(more, sizeof more, "+%u MORE >", (unsigned)c.queue);
        const bool glow = blinkOn(ck, 900, 150);
        s.textR(236, 182, more, glow ? P.peach : P.aqua); markQueue(s, 236 - s.textW(more) - 4, 178, s.textW(more) + 8, 14);
    }
    snoozeChips(s, c, BK::PILL, bo, ck, P.night, P.pale, P.pale);
    buttons(s, c, BK::PILL, bo, ck);
}

#if ALERT_SKINS_ALL
// 5 -- TILING: a tiling window manager, calm and all text
static void tile(SC& s, float x, float y, float w, float h, const char* name, bool focus, bool urgent = false) {
    s.fillRect(x, y, w, h, P.night); s.drawRect(x, y, w, h, urgent ? P.rose : focus ? P.peach : P.plum2);
    if (name && name[0]) { s.fillRect(x + 6, y, s.textW(name) + 6, 1, P.night); s.text(x + 9, y - 3, name, focus ? P.peach : P.dim); }
}
static void sTiling(SC& s, const Card& c, const Clk& ck, bool) {
    const float top = 19;
    s.t.fillRect(0, 0, s.pw, s.ph, P.deep);
    s.fillRect(s.dx0(), 0, s.dw(), 14, P.night);
    const int n = c.queue + 1 > 5 ? 5 : c.queue + 1;
    const bool urgent = c.queue && ck.qa < 2400;
    for (int i = 0; i < n; i++) {
        const float x = 2 + i * 14; const bool on = i == 0, urg = urgent && i == n - 1 && ((ck.qa / 200) % 2 == 0);
        s.fillRect(x, 1, 12, 12, urg ? P.rose : on ? P.peach : P.plum); char d[2] = { (char)('1' + i), 0 }; s.textC(x + 6, 3, d, on || urg ? P.night : P.lav);
    }
    if (c.queue) markQueue(s, 16, 0, (n - 1) * 14, 14);
    s.text(4 + n * 14, 3, c.typeShort, P.peach);
    s.textR(316, 3, "| 14:43", P.lav);
    if (c.owner[0]) s.textR(316 - s.textW("| 14:43") - 6, 3, c.owner, c.ownerAway ? P.gold : P.dim);
    BO bo; bo.keys = true; bo.y = 207; bo.h = 30; bo.x0 = 4; bo.x1 = 316; bo.gap = 3;
    if (s_infoBody) {
        tile(s, 2, top, 316, 198 - top, "info", true);
        infoBody(s, { 4, top + 4, 312, 198 - top - 6 }, P.night);
        buttons(s, c, BK::TAB, bo, ck);
        return;
    }
    const float k = ck.age >= 520 ? 1 : ck.age / 520.0f, H1 = 112;
    if (k < 0.3f) { tile(s, 2, top, 316, 198 - top, "trail", true); buttons(s, c, BK::TAB, bo, ck); return; }
    K kk = KD(); kk.bg = P.night; kk.stop = P.rose2; kk.line = P.plum; kk.lineHi = P.mauve; kk.boxBg = P.night; kk.boxEdge = P.rose2; kk.check = P.peach;
    tile(s, 2, top, 236, H1, "trail", true);
    float x = s.bang(8, top + 4, c.verdictWord, verdictCol(c, kk));
    const char* tl = x + 7 + s.bangW(c.typeLabel) < 236 ? c.typeLabel : c.typeShort;
    if (x + 7 + s.bangW(tl) < 236) { x = s.bang(x + 7, top + 4, tl, P.rose2); if (x + 24 < 236) iconAfter(s, c.type, x, top + 6, 22, P.night); }
    trail(s, c, 4, 238, top + 37, kk, ck, true);
    tile(s, 242, top, 76, H1, "signal", false);
    K kl = KD(); kl.ladOld = P.plum; kl.ladNew = P.rose2;
    const float ey = ladder(s, c, 248, top + 7, 64, kl);
    char db[8]; snprintf(db, sizeof db, "%d", (int)c.rssi); s.bangC(280, ey + 3, db, P.pale); s.textC(280, ey + 32, c.trend, c.trendUp ? P.mint : P.dim);
    const float yw = top + H1 + 6;
    if (k >= 0.6f) { tile(s, 2, yw, 316, 19, "why", false); ticks(s, c, 8, yw + 6, kk, false); }
    if (k >= 0.9f) {
        const float yf = yw + 24, hf = 205 - yf - 2; tile(s, 2, yf, 316, hf, "facts", false);
        const int want = c.ownerRow[0] ? 5 : 4, fit = (int)((hf - 4) / 9);
        const int rows = fit > want ? want : fit;
        char buf[48];
        const char* keys[5]; const char* vals[5];
        const int nRows = factRows(c, rows, keys, vals);
        for (int i = 0; i < nRows; i++) { const bool isConf = keys[i][0] == 'C', isOwner = keys[i][0] == 'O'; s.text(8, yf + 4 + i * 9, keys[i], P.dim); s.text(c.ownerRow[0] ? 44 : 38, yf + 4 + i * 9, fitTo(s, vals[i], i < 1 ? 210 : 264, buf, sizeof buf), isConf ? (c.confLevel >= 2 ? P.mint : c.confLevel ? P.gold : P.rose) : isOwner ? (c.ownerAway ? P.gold : P.pale) : P.pale); }
        s.textR(312, yf + 5, fitTo(s, c.met, 60, buf, sizeof buf), P.lav);
    }
    snoozeChips(s, c, BK::TAB, bo, ck, P.night, P.peach, P.pale);
    buttons(s, c, BK::TAB, bo, ck);
}

// 6 -- BEOS: the yellow tab
static void sBeos(SC& s, const Card& c, const Clk& ck, bool) {
    s.t.fillRect(0, 0, s.pw, s.ph, P.slate2); dither(s, s.dx0(), s.dy0(), s.dw(), 202 - s.dy0(), P.slate2, P.slate, 1);
    const char* tabTxt = c.typeLabel; const float tabW = s.textW(tabTxt) + 58, wx = 4, ww = 312, wy = 20;
    // the Deskbar, top right: this catch, how many wait, the clock
    const float dx = 170; s.fillRect(dx, 2, 148, 15, P.lav); bevel(s, dx, 2, 148, 15, P.pale, 0, P.night, 0, false); sunIcon(s, dx + 9, 9, 5, P.lav);
    s.fillRect(dx + 16, 4, s.textW(c.typeShort) + 4, 11, P.gold); s.text(dx + 18, 6, c.typeShort, P.night);
    if (c.queue) { char more[12]; snprintf(more, sizeof more, "+%u", (unsigned)c.queue); const float qx = dx + 24 + s.textW(c.typeShort); s.text(qx, 6, more, P.night); markQueue(s, qx - 2, 2, s.textW(more) + 4, 15); }
    s.textR(316, 6, "14:43", P.night);
    const float slide = easeT(ck.age, 260), unroll = ck.age < 260 ? 0 : easeT(ck.age - 260, 260);
    const float tx = -tabW + (wx + tabW) * slide;
    s.fillRect(tx, 4, tabW, 17, P.sun1); bevel(s, tx, 4, tabW, 17, P.pale, 0, P.sun3, 0, false);
    s.fillRect(tx + 5, 8, 10, 10, P.sun1); bevel(s, tx + 5, 8, 10, 10, P.sun3, 0, P.pale, 0, false);
    { const float te = s.text(tx + 21, 9, tabTxt, P.night); iconAfter(s, c.type, te - 2, 7, 12, P.sun1); }
    const float zx = tx + tabW - 15; s.drawRect(zx, 8, 10, 10, P.sun3); s.drawRect(zx, 8, 6, 6, P.sun3);
    if (c.queue && ck.qa < 100000) { const float bx = tx + tabW + 2 + fmaxf(0, 1 - ck.qa / 300.0f) * 30; if (bx + 60 < dx) { s.fillRect(bx, 6, 58, 15, P.gold); bevel(s, bx, 6, 58, 15, P.pale, 0, P.sun3, 0, false); s.text(bx + 8, 10, "NEXT", P.night); } }
    BO bo; bo.y = 207; bo.h = 28; bo.x0 = 7; bo.x1 = 313; bo.gap = 7;
    const float wh = (198 - wy) * unroll; if (wh < 6) return;
    s.fillRect(wx, wy, ww, wh, P.lav); bevel(s, wx, wy, ww, wh, P.pale, 0, P.night, 0, false);
    const Box B = { wx + 3, wy + 3, ww - 6, wh - 6 };
    if (unroll >= 1) { CO o; o.type = false; content(s, c, B, KD(), ck, o); }
    else s.fillRect(B.x, B.y, B.w, B.h, P.indigo);
    if (unroll >= 1) { snoozeChips(s, c, BK::BEOS, bo, ck, P.lav, P.night, P.night); buttons(s, c, BK::BEOS, bo, ck); }
}

#endif  // ALERT_SKINS_ALL

// 7 -- AMIGA WORKBENCH: the alert is its own screen and rises over the Workbench
static void gadgetClose(SC& s, float x, float y, uint16_t bg) { s.fillRect(x, y, 13, 11, bg); bevel(s, x, y, 13, 11, P.pale, 0, P.night, 0, false); s.fillRect(x + 5, y + 4, 3, 3, P.night); }
static void gadgetDepth(SC& s, float x, float y, uint16_t bg) { s.fillRect(x, y, 15, 11, bg); bevel(s, x, y, 15, 11, P.pale, 0, P.night, 0, false); s.drawRect(x + 3, y + 2, 7, 5, P.night); s.fillRect(x + 6, y + 4, 7, 5, P.pale); s.drawRect(x + 6, y + 4, 7, 5, P.night); }
static void sAmiga(SC& s, const Card& c, const Clk& ck, bool) {
    const uint16_t blue = P.plum2;
    s.t.fillRect(0, 0, s.pw, s.ph, blue);
    // the Workbench screen behind; its title bar stays showing at the top
    s.fillRect(s.dx0(), 0, s.dw(), 11, P.pale); s.text(3, 2, "Workbench release.", blue);
    if (c.queue) { char more[12]; snprintf(more, sizeof more, "+%u MORE", (unsigned)c.queue); const bool bl = blinkOn(ck, 600, 100); s.text(214, 2, more, bl ? P.gold : P.night); markQueue(s, 212, 0, s.textW(more) + 4, 11); }
    gadgetDepth(s, 303, 0, P.pale);
    s.fillRect(16, 26, 22, 22, P.pale); s.fillRect(20, 26, 14, 8, P.night); s.fillRect(28, 28, 4, 5, P.pale); s.fillRect(19, 38, 16, 9, P.gold); s.textC(27, 52, "SQUACH", P.pale);
    float sy = 12 + (1 - easeT(ck.age, 420)) * 228;
    s.fillRect(s.dx0(), sy, s.dw(), 240 - sy + 20, blue); s.hline(s.dx0(), sy - 1, s.dw(), P.night);
    s.fillRect(s.dx0(), sy, s.dw(), 11, P.pale); s.text(3, sy + 2, "SquachWatch Alert", blue); gadgetDepth(s, 303, sy, P.pale);
    sy += 12;
    BO bo; bo.face = blue; bo.y = 206; bo.h = 30;
    const float wx = 4, wy = sy + 3, ww = 312, wh = 200 - wy;
    if (wy > 190) return;
    s.drawRect(wx, wy, ww, wh, P.pale); s.drawRect(wx + 1, wy + 1, ww - 2, wh - 2, P.night);
    s.fillRect(wx + 2, wy + 2, ww - 4, 12, P.pale);
    for (int i = 0; i < 4; i++) s.hline(wx + 18, wy + 4 + i * 2, ww - 54, P.lav);
    gadgetClose(s, wx + 2, wy + 2, P.pale); gadgetDepth(s, wx + ww - 18, wy + 2, P.pale);
    const float tw = s.textW(c.typeShort) + s.textW(c.owner) + 30;
    s.fillRect(wx + 20, wy + 3, tw, 10, P.pale);
    float te = s.text(wx + 24, wy + 4, c.typeShort, P.night); te = iconAfter(s, c.type, te - 2, wy + 3, 10, P.pale); s.text(te + 4, wy + 4, c.owner, P.night);
    K k = KD(); k.bg = P.light ? blue : P.night; k.text = P.pale; k.muted = P.lav; k.accent = P.gold; k.line = P.light ? P.night : blue; k.lineHi = P.gold;
    k.stop = P.pale; k.here = P.gold; k.pin = P.gold; k.warn = P.gold; k.ladBg = P.night; k.ladEdge = P.pale; k.key = P.lav; k.chipA = P.gold; k.chipB = P.pale; k.rule = P.pale;
    CO o; o.type = false; o.chips = false;
    content(s, c, { wx + 3, wy + 15, ww - 6, wh - 17 }, k, ck, o);
    const float rx = wx + ww - 12, ry = wy + wh - 11; s.fillRect(rx, ry, 10, 9, P.pale); s.line(rx + 2, ry + 7, rx + 8, ry + 1, P.night); s.line(rx + 5, ry + 7, rx + 8, ry + 4, P.night);
    snoozeChips(s, c, BK::AMIGA, bo, ck, blue, P.pale, P.pale);
    buttons(s, c, BK::AMIGA, bo, ck);
}

#if ALERT_SKINS_ALL
// 8 -- NEXTSTEP: black title bars, grey bevels, the Dock
static void nextBtn(SC& s, float x, float y, bool close) { s.fillRect(x, y, 12, 12, P.dim); bevel(s, x, y, 12, 12, P.pale, 0, P.night, 0, false); if (close) { s.line(x + 3, y + 3, x + 8, y + 8, P.night); s.line(x + 8, y + 3, x + 3, y + 8, P.night); } else s.drawRect(x + 3, y + 3, 6, 6, P.night); }
static void sNext(SC& s, const Card& c, const Clk& ck, bool) {
    const float ww = 290, wx = 4, wy = 4, wh = 196;
    s.t.fillRect(0, 0, s.pw, s.ph, P.indigo); dither(s, s.dx0(), s.dy0(), s.dw(), 202 - s.dy0(), P.indigo, P.slate, 1);
    // the Dock down the right edge; a tile for this catch and one for each waiting
    const int tiles = 2 + (c.queue > 3 ? 3 : c.queue);
    for (int i = 0; i < tiles; i++) {
        const float y = 2 + i * 24; const bool fresh = i == tiles - 1 && c.queue && blinkOn(ck, 900, 150);
        s.fillRect(298, y, 21, 22, fresh ? P.lav : P.dim); bevel(s, 298, y, 21, 22, P.pale, 0, P.night, 0, false);
        if (i == 0) sunIcon(s, 308, y + 11, 6, P.dim); else if (i == 1) s.icon(c.type, 308, y + 12, 5, fresh ? P.lav : P.dim); else { s.fillCircle(308, y + 11, 4, P.slate2); }
    }
    if (c.queue) markQueue(s, 298, 50, 21, (tiles - 2) * 24);
    BO bo; bo.x1 = 294; bo.y = 205; bo.h = 31;
    if (ck.age < 380) { const float from[4] = { 298, 26, 21, 22 }, to[4] = { wx, wy, ww, wh }; const uint16_t cols[3] = { P.pale, P.lav, P.dim }; zoomRects(s, from, to, ck.age / 380.0f, cols, 3); buttons(s, c, BK::NEXT, bo, ck); return; }
    s.fillRect(wx, wy, ww, wh, P.dim); s.drawRect(wx - 1, wy - 1, ww + 2, wh + 2, P.night);
    s.fillRect(wx, wy, ww, 17, P.night);
    { const float tw = s.textW(c.typeLabel) + 14; const float te = s.text(wx + ww / 2 - tw / 2, wy + 5, c.typeLabel, P.pale); iconAfter(s, c.type, te - 2, wy + 3, 12, P.night); }
    nextBtn(s, wx + 3, wy + 3, false); nextBtn(s, wx + ww - 15, wy + 3, true);
    const Box B = { wx + 4, wy + 20, ww - 8, wh - 31 };
    bevel(s, B.x - 1, B.y - 1, B.w + 2, B.h + 2, P.night, 0, P.pale, 0, false);
    CO o; o.type = false; o.ladW = 56;
    content(s, c, B, KD(), ck, o);
    const float ry = wy + wh - 6; s.fillRect(wx, ry, ww, 6, P.dim); bevel(s, wx, ry, ww, 6, P.pale, 0, P.night, 0, false); s.vline(wx + 24, ry, 6, P.night); s.vline(wx + ww - 25, ry, 6, P.night);
    snoozeChips(s, c, BK::NEXT, bo, ck, P.dim, P.night, P.night);
    buttons(s, c, BK::NEXT, bo, ck);
}

#endif  // ALERT_SKINS_ALL

// 9 -- WINDOWS XP LUNA: dissolves in over Bliss at dusk
static void sXP(SC& s, const Card& c, const Clk& ck, bool sunset) {
    const Wall wl = { 0.6f, 0.7f, 44, true };
    desk(s, sunset, wl, ck.amb, P.deep);
    const float X0 = 8, Y0 = 8, WW = 304, HH = 192, th = 30;
    s.fillRR(X0, Y0, WW, 14, 7, P.plum2); s.fillRect(X0, Y0 + 7, WW, HH - 7, P.plum2);
    const uint16_t bands[4] = { P.slate2, P.plum2, P.plum2, P.indigo };
    for (int i = 0; i < 4; i++) s.fillRect(X0 + 3, Y0 + 2 + i * 7, WW - 6, 8, bands[i]);
    s.hline(X0 + 7, Y0 + 1, WW - 14, P.aqua);
    s.bang(X0 + 9, Y0 + 3, c.verdictWord, P.night);
    float x = s.bang(X0 + 8, Y0 + 2, c.verdictWord, P.pale);
    const char* tl = x + 6 + s.bangW(c.typeShort) < X0 + WW - 70 ? c.typeShort : "";
    if (tl[0]) { x = s.bang(x + 6, Y0 + 2, tl, P.peach); if (x + 24 < X0 + WW - 66) iconAfter(s, c.type, x, Y0 + 4, 22, P.plum2); }
    const float bx[3] = { WW - 64, WW - 44, WW - 24 }; const char* g[3] = { "_", "o", "X" }; const uint16_t bc[3] = { P.plum2, P.plum2, P.rose };
    for (int i = 0; i < 3; i++) { s.fillRR(X0 + bx[i], Y0 + 6, 18, 18, 3, bc[i]); s.drawRR(X0 + bx[i], Y0 + 6, 18, 18, 3, P.pale); s.textC(X0 + bx[i] + 9, Y0 + 11, g[i], P.pale); }
    CO o; o.verdict = false; o.type = false;
    content(s, c, { X0 + 3, Y0 + th + 1, WW - 6, HH - th - 4 }, P.light ? KL() : KD(), ck, o);
    if (c.queue && ck.qa < 1800) {   // the tray balloon for a new catch
        const float bx2 = 170, by = 150;
        s.fillRR(bx2, by, 140, 34, 5, P.sun1); s.drawRR(bx2, by, 140, 34, 5, P.night); s.fillTri(bx2 + 110, by + 33, bx2 + 124, by + 33, bx2 + 128, by + 46, P.sun1);
        s.fillCircle(bx2 + 12, by + 12, 6, P.plum2); s.textC(bx2 + 12, by + 9, "i", P.pale); s.text(bx2 + 24, by + 6, "NEW CATCH", P.night);
        char more[20]; snprintf(more, sizeof more, "%u WAITING", (unsigned)c.queue); s.text(bx2 + 24, by + 18, more, P.night);
        markQueue(s, bx2, by, 140, 34);
    } else if (c.queue) {
        char more[16]; snprintf(more, sizeof more, "+%u MORE", (unsigned)c.queue);
        s.textR(X0 + WW - 70, Y0 + 11, more, P.pale); markQueue(s, X0 + WW - 74 - s.textW(more), Y0 + 6, s.textW(more) + 6, 18);
    }
    BO bo; bo.y = 205; bo.h = 31; bo.x0 = 5; bo.x1 = 315; bo.gap = 5;
    snoozeChips(s, c, BK::XPB, bo, ck, P.light ? P.pale : P.night, P.slate2, P.light ? P.night : P.pale);
    buttons(s, c, BK::XPB, bo, ck);
    if (ck.age < 360) {   // dissolve in: a 4x4 ordered dither, the window's pixels giving way to the sky
        static const uint8_t M[4][4] = { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };
        const int lvl = (int)(easeT(ck.age, 360) * 16);
        const int x0 = s.X(X0), y0 = s.Y(Y0), x1 = s.X(X0 + WW), y1 = s.Y(Y0 + HH);
        for (int yy = y0; yy < y1; yy++) { const uint16_t sky = sunset ? skyAt((yy - s.oy) / (s.k * 240 * 0.6f)) : P.deep; for (int xx = x0; xx < x1; xx++) if (M[yy & 3][xx & 3] >= lvl) s.px(xx, yy, sky); }
    }
}

// 10 -- AQUA: pinstripes, gel and traffic lights; drops in like a sheet
static void sAqua(SC& s, const Card& c, const Clk& ck, bool sunset) {
    desk(s, sunset, { 0.7f, 0.3f, 50, false }, ck.amb, P.slate2);
    s.fillRect(s.dx0(), 0, s.dw(), 13, P.pale); s.hline(s.dx0(), 13, s.dw(), P.lav); sunIcon(s, 10, 6, 5, P.pale);
    float x = 22; const char* menus[3] = { "SquachWatch", "Alert", "Trail" };
    for (int i = 0; i < 3; i++) { s.text(x, 3, menus[i], P.night); if (!i) s.text(x + 1, 3, menus[i], P.night); x += s.textW(menus[i]) + 12; }
    s.textR(314, 3, "14:43", P.night);
    if (c.queue) { char more[8]; snprintf(more, sizeof more, "+%u", (unsigned)c.queue); const float qx = 314 - s.textW("14:43") - 10 - s.textW(more); s.text(qx, 3, more, blinkOn(ck, 600, 100) ? P.rose : P.night); markQueue(s, qx - 3, 0, s.textW(more) + 6, 13); }
    const float drop = (1 - easeT(ck.age, 360)) * -190;
    const float X0 = 6, Y0 = 18 + drop, WW = 308, HH = 182, th = 20;
    halfShadow(s, X0 + 4, Y0 + 6, WW, HH, P.deep);
    s.fillRR(X0, Y0, WW, HH, 6, P.pale);
    for (float yy = Y0 + th; yy < Y0 + HH - 4; yy += 2) s.hline(X0 + 1, yy, WW - 2, P.lav);
    for (int i = 0; i < th; i += 2) s.hline(X0 + 4, Y0 + 1 + i, WW - 8, i < 6 ? P.pale : P.lav);
    const uint16_t tl[3] = { P.rose, P.gold, P.mint };
    for (int i = 0; i < 3; i++) { s.fillCircle(X0 + 12 + i * 14, Y0 + 10, 5, tl[i]); s.drawCircle(X0 + 12 + i * 14, Y0 + 10, 5, P.plum); s.fillRect(X0 + 10 + i * 14, Y0 + 7, 3, 2, P.pale); }
    { const float tw = s.textW(c.typeLabel) + 14; const float te = s.text(X0 + WW / 2 - tw / 2, Y0 + 6, c.typeLabel, P.night); iconAfter(s, c.type, te - 2, Y0 + 4, 12, P.lav); }
    s.hline(X0 + 1, Y0 + th, WW - 2, P.dim);
    K k = P.light ? KL() : KD(); if (P.light) k.bg = P.pale;
    CO o; o.type = false; o.chipRound = true; o.fill = !P.light;
    content(s, c, { X0 + 3, Y0 + th + 2, WW - 6, HH - th - 5 }, k, ck, o);
    s.drawRR(X0, Y0, WW, HH, 6, P.dim);
    BO bo; bo.y = 205; bo.h = 31; bo.x0 = 6; bo.x1 = 314; bo.gap = 5;
    snoozeChips(s, c, BK::AQUAB, bo, ck, P.pale, P.plum2, P.night);
    buttons(s, c, BK::AQUAB, bo, ck);
}

// 11 -- TERMINAL: nothing but characters, on a 53 x 30 grid of 6 x 8 cells. Words in
// the 5x7 font; lines and blocks drawn across whole cells, the way a terminal
// emulator draws them (the font's own box glyphs leave a gap after every cell).
static float cx6(float c) { return c * 6 + 1; }
static float ry8(float r) { return r * 8; }
static void tdbl(SC& s, float c0, float c1, float r, uint16_t col) { const float y = ry8(r) + 3; s.hline(cx6(c0) + 3, y, (c1 - c0) * 6, col); s.hline(cx6(c0) + 3, y + 2, (c1 - c0) * 6, col); }
static void tvdbl(SC& s, float c, float r0, float r1, uint16_t col) { const float x = cx6(c) + 2; s.vline(x, ry8(r0) + 4, (r1 - r0) * 8, col); s.vline(x + 2, ry8(r0) + 4, (r1 - r0) * 8, col); }
static void sTerm(SC& s, const Card& c, const Clk& ck, bool) {
    const uint16_t fg = P.pale, hi = P.peach, dim = P.dim, acc = P.aqua, bg = P.deep;
    s.t.fillRect(0, 0, s.pw, s.ph, bg);
    tdbl(s, 0, 52, 0, acc); tdbl(s, 0, 52, 25, acc); tvdbl(s, 0, 0, 25, acc); tvdbl(s, 52, 0, 25, acc);
    auto label = [&](float col, float row, const char* str, uint16_t cc) { s.fillRect(cx6(col) - 1, ry8(row), strlen(str) * 6 + 2, 8, bg); s.text(cx6(col), ry8(row), str, cc); };
    label(3, 0, " SQUACHWATCH ", hi);
    char of[16]; snprintf(of, sizeof of, " 1 OF %u ", (unsigned)(c.queue + 1)); label(38, 0, of, dim);
    if (c.queue) markQueue(s, cx6(38), 0, 60, 8);
    if (s_infoBody) {
        label(38, 0, " INFO ", dim);
        infoBody(s, { cx6(1) + 2, ry8(1) + 2, cx6(51) - cx6(1) - 2, ry8(25) - ry8(1) - 2 }, bg);
        const int n = s_infoN == 3 ? 3 : 2;
        const float gap = 6, x0 = cx6(1), w = (cx6(52) - x0 - gap * (n - 1)) / n;
        for (int i = 0; i < n; i++) {
            const uint16_t cc = i == 0 ? hi : acc;
            const char* b = infoLabel(n, i);
            char lb[16]; snprintf(lb, sizeof lb, "[%c]%s", b[0], b + 1);
            const float x = x0 + i * (w + gap);
            s.drawRect(x, 208, w, 30, cc); if (i == 0) s.drawRect(x + 1, 209, w - 2, 28, cc);
            s.textC(x + w / 2, 219, lb, cc);
            s.mark(s_btn[i], x, 208, w, 30); Theme::hotButton(s_btn[i].x, s_btn[i].y, s_btn[i].w, s_btn[i].h, b);
        }
        buildLuts();
        remap(s, 0, 0, s.pw, s.ph, L_scan, [](int, int y) { return (y & 1) == 1; });
        return;
    }
    const int shown = (int)(ck.age / 24);
    auto row = [&](int r) { return r <= shown; };
    auto put = [&](float col, int r, const char* str, uint16_t cc) { if (row(r)) s.text(cx6(col), ry8(r), str, cc); };
    if (row(2)) {   // the verdict and the type, double size
        s.t.setTextFont(1); s.t.setTextSize(s.ts * 2); s.t.setTextColor(c.verdict == Verdict::FOLLOWING ? hi : fg);
        s.t.setCursor(s.X(cx6(2)), s.Y(ry8(2))); s.t.print(c.verdictWord);
        const float vw = strlen(c.verdictWord) + 1; s.t.setTextColor(fg); s.t.setCursor(s.X(cx6(2 + vw * 2)), s.Y(ry8(2))); if (2 + (vw + strlen(c.typeShort)) * 2 < 36) {
            s.t.print(c.typeShort);
            s.t.setTextSize(s.ts);
            iconAfter(s, c.type, cx6(2 + (vw + strlen(c.typeShort)) * 2), ry8(2), 15, bg);
        }
        s.t.setTextSize(s.ts);
    }
    if (c.owner[0]) put(2, 4, c.owner, c.ownerAway ? P.gold : dim);
    put(c.owner[0] ? 4 + strlen(c.owner) : 2, 4, c.met, dim);
    // the trail on the grid
    if (!c.nStops) put(2, 8, "JUST HEARD. NO TRAIL YET.", dim);
    else {
        float cols[Detection::STOPS_N];
        for (uint8_t i = 0; i < c.nStops; i++) cols[i] = 3 + roundf(31 * c.stops[i].pos / 1000.0f);
        const uint8_t last = c.nStops - 1;
        for (uint8_t i = 0; i < c.nStops; i++) {
            const bool here = i == last && c.hereLast; const float cc = cols[i];
            put(cc - 2, 6, c.stops[i].time, here ? hi : fg);
            put(cc - 2, 9, c.stops[i].place, here ? hi : fg);
            put(cc - 2, 10, c.stops[i].dwell, dim);
            if (i && c.stops[i].leg[0]) put((cols[i - 1] + cc) / 2 - 3, 7, c.stops[i].leg, dim);
        }
        if (row(8)) {
            tdbl(s, cols[0], cols[last], 8, P.rose2);
            for (uint8_t i = 0; i < c.nStops; i++) {
                const float x = cx6(cols[i]) + 2, y = ry8(8) + 4; s.fillRect(x - 3, y - 3, 7, 7, bg);
                if (i < last || !c.hereLast) s.fillRect(x - 2, y - 2, 5, 5, fg);
                else { s.drawRect(x - 3, y - 3, 7, 7, hi); if ((ck.amb / 400) % 2) s.fillRect(x - 1, y - 1, 3, 3, hi); }
            }
        }
    }
    // signal: block bars, whole cells wide
    if (row(2)) { s.drawRect(cx6(39) + 2, ry8(2) + 3, 12 * 6 + 1, 9 * 8 + 1, dim); label(41, 2, " SIGNAL ", dim); }
    const int nb = c.sparkN < 6 ? c.sparkN : 6;
    for (int i = 0; i < nb; i++) {
        const int r = 3 + i; if (!row(r)) continue;
        const int8_t v = c.spark[c.sparkN - nb + i]; int n = (int)roundf((v + 100) / 60.0f * 10); if (n < 1) n = 1; if (n > 10) n = 10;
        for (int kk = 0; kk < 10; kk++) s.fillRect(cx6(40 + kk), ry8(r) + 1, 5, 6, kk < n ? (i == nb - 1 ? hi : acc) : P.slate);
    }
    char db[16]; snprintf(db, sizeof db, "%d dBm", (int)c.rssi); put(40, 9, db, fg); put(40, 10, c.trend, c.trendUp ? P.mint : dim);
    char tk[64]; snprintf(tk, sizeof tk, "[%c] %s  [%c] %s  [%c] %s  [%c] %s", c.ticks[0].on ? 'x' : ' ', c.ticks[0].label, c.ticks[1].on ? 'x' : ' ', c.ticks[1].label,
                          c.ticks[2].on ? 'x' : ' ', c.ticks[2].label, c.ticks[3].on ? 'x' : ' ', c.ticks[3].label);
    put(2, 12, tk, P.mint);
    char line[64], buf[48];
    snprintf(line, sizeof line, "WHO   %s", fitTo(s, c.who, 260, buf, sizeof buf)); put(2, 14, line, fg);
    snprintf(line, sizeof line, "MAC   %s", c.mac); put(2, 15, line, fg);
    snprintf(line, sizeof line, "SEEN  %s", c.seen); put(2, 16, line, fg);
    if (c.ownerRow[0]) { snprintf(line, sizeof line, "OWNER %s", c.ownerRow); put(2, 17, line, c.ownerAway ? P.gold : fg); }
    snprintf(line, sizeof line, "CONF  %s", c.conf); put(2, c.ownerRow[0] ? 18 : 17, line, c.confLevel >= 2 ? P.mint : P.gold);
    put(2, 20, c.verdict == Verdict::FOLLOWING ? "> heard at every stop on the trail" : "> watching", dim);
    const bool caught = c.queue && ck.qa < 100000;
    if (caught) { char nc[40]; snprintf(nc, sizeof nc, "> %u more waiting", (unsigned)c.queue); put(2, 21, nc, hi); }
    const int cr = caught ? 22 : 21;
    if (row(cr)) { s.text(cx6(2), ry8(cr), ">", fg); if ((ck.amb / 500) % 2) s.fillRect(cx6(4), ry8(cr), 5, 7, fg); }
    // buttons: boxed labels, the hotkey in brackets
    float col = 1;
    for (int i = 0; i < 5; i++) {
        const uint16_t cc = c.sunk[i] ? P.night : (i == 0 ? hi : acc);
        char lb[16]; snprintf(lb, sizeof lb, "[%c]%s", c.btn[i][0], c.btn[i] + 1);
        const float x = cx6(col), w = 10 * 6 - 2;
        if (c.sunk[i]) s.fillRect(x, 208, w, 30, P.pale);
        s.drawRect(x, 208, w, 30, cc); if (i == 0) s.drawRect(x + 1, 209, w - 2, 28, cc);
        s.textC(x + w / 2, 219, lb, cc);
        s.mark(s_btn[i], x, 208, w, 30); Theme::hotButton(s_btn[i].x, s_btn[i].y, s_btn[i].w, s_btn[i].h, c.hot[i]);
        col += 10.5f;
    }
    if (c.picking) {
        static const char* const SNZ[4] = { "10 MIN", "30 MIN", "1 HOUR", "4 HOURS" };
        s.fillRect(cx6(2), ry8(18) - 2, 290, 30, bg); s.drawRect(cx6(2), ry8(18) - 2, 290, 30, hi);
        s.text(cx6(3), ry8(18), "QUIET THIS ONE FOR:", hi);
        for (int i = 0; i < 4; i++) { const float x = cx6(3 + i * 12); s.drawRect(x, ry8(19) + 2, 66, 14, acc); s.textC(x + 33, ry8(19) + 5, SNZ[i], fg); s.mark(s_chip[i], x, ry8(19) + 2, 66, 14); }
    }
    // scanlines
    buildLuts();
    remap(s, 0, 0, s.pw, s.ph, L_scan, [](int, int y) { return (y & 1) == 1; });
}

// ---- the entry point -------------------------------------------------------------------
void draw(TFT_eSPI& t, uint8_t id, const Card& c, uint32_t now, uint32_t openedAt, uint32_t queueAt, uint8_t motion, bool sunset, uint8_t palette) {
    useTheme(palette);
    SC s(t);
    Clk ck;
    const uint32_t age = now - openedAt;
    ck.age = motion == 2 ? 100000u : age;
    ck.amb = motion == 0 ? now : openedAt + 3000u;
    ck.qa  = (motion == 2 || !queueAt || now < queueAt) ? 100000u : now - queueAt;
    ck.queue = c.queue;
    forget();
    switch (id) {
        case SUNSET95: sSunset(s, c, ck, sunset); break;
#if ALERT_SKINS_ALL
        case DESKTOP:  sDesktop(s, c, ck, sunset); break;
#endif
        case SYSTEM7:  sMac(s, c, ck, sunset); break;
        case GLASS:    sGlass(s, c, ck, sunset); break;
        case AMIGA:    sAmiga(s, c, ck, sunset); break;
#if ALERT_SKINS_ALL
        case TILING:   sTiling(s, c, ck, sunset); break;
        case BEOS:     sBeos(s, c, ck, sunset); break;
        case NEXT:     sNext(s, c, ck, sunset); break;
#endif
        case XP:       sXP(s, c, ck, sunset); break;
        case AQUA:     sAqua(s, c, ck, sunset); break;
        case TERMINAL: sTerm(s, c, ck, sunset); break;
        default: break;
    }
}

void drawInfo(TFT_eSPI& t, uint8_t id, const char* name, DetectionType type, InfoBody body,
              uint32_t now, bool sunset, uint8_t palette) {
    Card c{};
    c.verdict = Verdict::NEW;
    c.verdictWord = "ABOUT";
    c.typeLabel = name ? name : "";
    c.typeShort = c.typeLabel;
    c.type = type;
    c.conf = "";
    c.trend = "";
    static const char* const NONE[5] = { "", "", "", "", "" };
    for (int i = 0; i < 5; i++) { c.btn[i] = NONE[i]; c.hot[i] = NONE[i]; }
    s_infoBody = body; s_infoN = 0;
    // As if it had been open a long while: no opening animation.
    draw(t, id, c, now, now - 100000u, 0, 2, sunset, palette);
    s_infoBody = nullptr;
}

}  // namespace AlertSkin
