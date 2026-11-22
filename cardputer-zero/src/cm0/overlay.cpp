// SquachWatch-CM0 -- the ESC-hold exit modal (see overlay.h).
#include "overlay.h"

#include <cstring>

#include "glcdfont_data.h"   // the sim's classic 5x7 font: 5 column bytes per char, LSB on top

namespace {

// RGB565, the firmware's synthwave palette: black panel, pink frame, cyan
// accents, white text.
const uint16_t C_PANEL = 0x0000;
const uint16_t C_FRAME = 0xF81F;   // pink
const uint16_t C_TEXT  = 0xFFFF;
const uint16_t C_DIM   = 0x7BEF;   // grey
const uint16_t C_BAR   = 0x07FF;   // cyan
const uint16_t C_TRACK = 0x2945;   // dark slate

struct Canvas {
    std::vector<uint16_t>& px;
    int w, h;
    void rect(int x, int y, int rw, int rh, uint16_t c) {
        for (int yy = y; yy < y + rh; yy++) {
            if (yy < 0 || yy >= h) continue;
            for (int xx = x; xx < x + rw; xx++)
                if (xx >= 0 && xx < w) px[(size_t)yy * w + xx] = c;
        }
    }
    void frame(int x, int y, int rw, int rh, int t, uint16_t c) {
        rect(x, y, rw, t, c); rect(x, y + rh - t, rw, t, c);
        rect(x, y, t, rh, c); rect(x + rw - t, y, t, rh, c);
    }
    // One 5x7 glyph at `scale`, advancing 6*scale.
    void glyph(int x, int y, char ch, int scale, uint16_t c) {
        const unsigned char* g = font + (unsigned char)ch * 5;
        for (int col = 0; col < 5; col++)
            for (int row = 0; row < 8; row++)
                if (g[col] & (1u << row)) rect(x + col * scale, y + row * scale, scale, scale, c);
    }
    int textWidth(const char* s, int scale) { return (int)strlen(s) * 6 * scale - scale; }
    void textCentered(int cx, int y, const char* s, int scale, uint16_t c) {
        int x = cx - textWidth(s, scale) / 2;
        for (; *s; s++, x += 6 * scale) glyph(x, y, *s, scale, c);
    }
};

}   // namespace

namespace cm0 {

void composeExitHint(const std::vector<uint16_t>& in, int w, int h, float progress,
                     std::vector<uint16_t>& out) {
    out = in;
    if (w <= 0 || h <= 0 || out.size() < (size_t)w * h) return;
    if (progress < 0) progress = 0;
    if (progress > 1) progress = 1;
    Canvas cv{out, w, h};

    const int bw = 224, bh = 70;
    const int bx = (w - bw) / 2, by = (h - bh) / 2;
    cv.rect(bx, by, bw, bh, C_PANEL);
    cv.frame(bx, by, bw, bh, 2, C_FRAME);
    cv.frame(bx + 4, by + 4, bw - 8, bh - 8, 1, C_BAR);

    cv.textCentered(w / 2, by + 12, "HOLD ESC TO EXIT", 2, C_TEXT);

    const int tx = bx + 16, ty = by + 36, tw = bw - 32, th = 10;
    cv.rect(tx, ty, tw, th, C_TRACK);
    cv.rect(tx, ty, (int)(tw * progress + 0.5f), th, C_BAR);

    cv.textCentered(w / 2, by + 52, "let go to stay", 1, C_DIM);
}

}   // namespace cm0
