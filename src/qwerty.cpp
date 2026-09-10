// SquachWatch-CYD — QWERTY geometry. See include/qwerty.h.
#include "qwerty.h"

#if SQUACH_MESH

namespace Qwerty {

namespace {
const int MARGIN  = 4;    // off each screen edge
const int GAP     = 2;    // between keys in a row
const int ROW_GAP = 4;    // between rows
const int ROW_MIN = 26, ROW_MAX = 40;

inline int iabs(int v) { return v < 0 ? -v : v; }

void put(Key* out, uint8_t& n, int x, int y, int w, int h, char ch) {
    out[n].x = (int16_t)x; out[n].y = (int16_t)y;
    out[n].w = (int16_t)w; out[n].h = (int16_t)h;
    out[n].ch = ch;
    n++;
}
} // namespace

uint8_t layout(int w, int bandTop, int bandBottom, Key out[KEY_N]) {
    const int avail = w - 2 * MARGIN;

    // Row height from the band, clamped. Portrait has far more height than a
    // keyboard wants, and a 54px key is not a better key, just a taller one.
    int rh = (bandBottom - bandTop - 3 * ROW_GAP) / 4;
    if (rh > ROW_MAX) rh = ROW_MAX;
    if (rh < ROW_MIN) rh = ROW_MIN;
    // Anchored to the bottom of the band, where BACK is and where the hand
    // already is, rather than floating mid-screen with a gap under it.
    const int y0 = bandBottom - (4 * rh + 3 * ROW_GAP);

    uint8_t n = 0;

    // Row 1: ten keys across the full width.
    static const char R1[] = "QWERTYUIOP";
    const int w1 = (avail - 9 * GAP) / 10;
    const int x1 = MARGIN + (avail - (10 * w1 + 9 * GAP)) / 2;
    for (int i = 0; i < 10; i++)
        put(out, n, x1 + i * (w1 + GAP), y0, w1, rh, R1[i]);

    // Row 2: nine keys over the same width, so each is wider. This is the
    // whole return on un-staggering.
    static const char R2[] = "ASDFGHJKL";
    const int w2    = (avail - 8 * GAP) / 9;
    const int span2 = 9 * w2 + 8 * GAP;
    const int x2    = MARGIN + (avail - span2) / 2;
    const int y2    = y0 + rh + ROW_GAP;
    for (int i = 0; i < 9; i++)
        put(out, n, x2 + i * (w2 + GAP), y2, w2, rh, R2[i]);

    // Row 3: seven letters on row two's grid, then backspace in what is left.
    //
    // Backspace is placed AFTER the letters -- from where M actually ends,
    // plus its own gap -- rather than at a position chosen for it. The first
    // mockup did it the other way round, put backspace at a fixed x, and it
    // landed on top of M. Built in this order there is no x at which the two
    // can meet, whatever the screen width.
    static const char R3[] = "ZXCVBNM";
    const int y3 = y2 + rh + ROW_GAP;
    for (int i = 0; i < 7; i++)
        put(out, n, x2 + i * (w2 + GAP), y3, w2, rh, R3[i]);
    const int mEnd = x2 + 7 * w2 + 6 * GAP;     // one past M's last column
    const int bx   = mEnd + BKSP_GAP;
    put(out, n, bx, y3, (x2 + span2) - bx, rh, BKSP);

    // Row 4: clear, space, OK, on row two's span. Space takes what is left;
    // the other two are sized for what it costs to miss them.
    const int y4 = y3 + rh + ROW_GAP;
    const int cw = w2 * 3 / 2, ow = w2 * 2;
    const int sw = span2 - cw - ow - 2 * GAP;
    put(out, n, x2,                       y4, cw, rh, CLR);
    put(out, n, x2 + cw + GAP,            y4, sw, rh, ' ');
    put(out, n, x2 + cw + GAP + sw + GAP, y4, ow, rh, OK);

    return n;
}

int keyAt(const Key* keys, uint8_t n, int x, int y, int reach) {
    int best = -1;
    long bestD = (long)reach * reach + 1;
    for (uint8_t i = 0; i < n; i++) {
        const Key& k = keys[i];
        const int r = k.x + k.w - 1, b = k.y + k.h - 1;
        const int dx = (x < k.x) ? (k.x - x) : (x > r ? x - r : 0);
        const int dy = (y < k.y) ? (k.y - y) : (y > b ? y - b : 0);
        const long d = (long)dx * dx + (long)dy * dy;
        if (d < bestD) { bestD = d; best = i; }
    }
    return best;
}

void TouchFilter::down(int px, int py) {
    x = (int16_t)px; y = (int16_t)py;
    cn = 0;
}

void TouchFilter::move(int px, int py) {
    if (iabs(px - x) + iabs(py - y) <= JUMP_PX) {
        x = (int16_t)px; y = (int16_t)py;       // an ordinary slide
        cn = 0;
        return;
    }
    // A jump. One on its own is the panel losing contact; a run of them that
    // agree is the finger genuinely being somewhere else -- most likely
    // because the press sample itself was the bad one. Compared sample to
    // sample, so a finger that really is moving still accumulates.
    if (cn && iabs(px - cx) + iabs(py - cy) <= AGREE_PX) {
        cx = (int16_t)px; cy = (int16_t)py;
        if (++cn >= AGREE_N) { x = cx; y = cy; cn = 0; }
    } else {
        cx = (int16_t)px; cy = (int16_t)py;
        cn = 1;
    }
}

} // namespace Qwerty
#endif // SQUACH_MESH
