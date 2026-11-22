// SquachWatch-CYD — the alert skins' colour roles (see alert_skin.h).
//
// No drawing here, so the host tests can hold every theme's roles to the two
// promises: each one is a colour an RGB332 frame holds exactly, and a theme's
// background stays that theme's background.
#include "alert_skin.h"

namespace AlertSkin {

static const char* const NAMES[COUNT] = {
    "TRAIL", "SUNSET 95", "NEON DESKTOP", "SYSTEM 7", "GLASS", "TILING",
    "BEOS", "AMIGA", "NEXTSTEP", "XP LUNA", "AQUA", "TERMINAL",
};
const char* name(uint8_t id) { return id < COUNT ? NAMES[id] : NAMES[0]; }

bool fits(int w, int h) { return w >= 300 && h >= 220 && w >= h; }

static const uint8_t LV[8] = { 0, 36, 73, 109, 146, 182, 219, 255 };   // 3-bit red and green
static const uint8_t LB[4] = { 0, 85, 170, 255 };                      // 2-bit blue

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

uint16_t snap565(int r, int g, int b) {
    const uint8_t R = LV[clampi((r * 7 + 127) / 255, 0, 7)];
    const uint8_t G = LV[clampi((g * 7 + 127) / 255, 0, 7)];
    const uint8_t B = LB[clampi((b * 3 + 127) / 255, 0, 3)];
    return (uint16_t)(((R & 0xF8) << 8) | ((G & 0xFC) << 3) | (B >> 3));
}

// An 8-bit sprite keeps the top three bits of red and green and two of blue;
// a colour is exact when putting those back gives the same 565 value.
bool is332(uint16_t c) {
    const int r3 = c >> 13, g3 = (c >> 8) & 7, b2 = (c >> 3) & 3;
    const uint16_t back = (uint16_t)(((LV[r3] & 0xF8) << 8) | ((LV[g3] & 0xFC) << 3) | (LB[b2] >> 3));
    return back == c;
}

struct RGB { int r, g, b; };
static RGB rgbOf(uint16_t c) { return { (c >> 11) * 255 / 31, ((c >> 5) & 63) * 255 / 63, (c & 31) * 255 / 31 }; }
// The lab's mix: a step of t from a to b, truncated.
static RGB mix(RGB a, RGB b, float t) {
    return { a.r + (int)((b.r - a.r) * t), a.g + (int)((b.g - a.g) * t), a.b + (int)((b.b - a.b) * t) };
}
static uint16_t S(RGB c) { return snap565(c.r, c.g, c.b); }

Roles duskRoles() {
    Roles R;
    R.deep = S({ 0, 0, 85 });       R.night = S({ 36, 0, 85 });     R.indigo = S({ 36, 36, 85 });
    R.plum = S({ 73, 36, 85 });     R.plum2 = S({ 73, 36, 170 });   R.mauve = S({ 146, 36, 85 });
    R.rose = S({ 182, 73, 85 });    R.rose2 = S({ 219, 109, 170 }); R.peach = S({ 255, 182, 170 });
    R.sun1 = S({ 255, 219, 85 });   R.sun2 = S({ 255, 146, 85 });   R.sun3 = S({ 219, 73, 85 });
    R.lav = S({ 182, 182, 255 });   R.pale = S({ 219, 219, 255 });  R.dim = S({ 146, 146, 170 });
    R.slate = S({ 36, 73, 85 });    R.slate2 = S({ 73, 109, 170 }); R.aqua = S({ 109, 219, 255 });
    R.teal = S({ 0, 182, 170 });    R.mint = S({ 109, 219, 170 });  R.gold = S({ 255, 182, 85 });
    R.light = true;
    return R;
}

Roles deriveRoles(const uint16_t pal[12]) {
    const RGB bg = rgbOf(pal[0]), purple = rgbOf(pal[2]), cyan = rgbOf(pal[3]), pink = rgbOf(pal[4]),
              vpink = rgbOf(pal[5]), vpurple = rgbOf(pal[6]), vblue = rgbOf(pal[7]), vyellow = rgbOf(pal[8]),
              green = rgbOf(pal[9]), amber = rgbOf(pal[10]), WH = { 255, 255, 255 };
    Roles R;
    // The theme's own background is the background: black on every built-in theme.
    R.deep = S(bg); R.night = S(bg); R.indigo = S(bg);
    R.plum   = S(mix(bg, purple, 0.24f));
    R.plum2  = S(mix(bg, vpurple, 0.55f));
    R.mauve  = S(mix(bg, pink, 0.55f));
    R.rose   = S(mix(bg, pink, 0.74f));
    R.rose2  = S(mix(pink, vpink, 0.6f));
    R.peach  = S(mix(vpink, vyellow, 0.55f));
    R.sun1   = S(vyellow);
    R.sun2   = S(mix(vyellow, pink, 0.45f));
    R.sun3   = S(pink);
    // Text and highlights keep the theme's hue rather than drifting to white.
    R.lav    = S(mix(vpurple, WH, 0.25f));
    R.pale   = S(mix(mix(vpurple, cyan, 0.3f), WH, 0.6f));
    R.dim    = S(mix(mix(bg, vpurple, 0.65f), WH, 0.3f));
    R.slate  = S(mix(bg, vblue, 0.32f));
    R.slate2 = S(mix(bg, vblue, 0.5f));
    R.aqua   = S(vblue);
    R.teal   = S(mix(bg, cyan, 0.7f));
    R.mint   = S(mix(green, WH, 0.4f));
    R.gold   = S(amber);
    R.light  = false;
    return R;
}

}  // namespace AlertSkin
