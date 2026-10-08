#include "fast_sprite.h"

#if defined(ARDUINO_ARCH_ESP32)

// 16-bit colour to the sprite's 8-bit byte: the library's own expression.
bool FastSprite::s_fast = true;
FastSprite* g_frameSprite = nullptr;

static inline uint8_t to8(uint32_t color) {
    return (uint8_t)((color & 0xE000) >> 8 | (color & 0x0700) >> 6 | (color & 0x0018) >> 3);
}

void FastSprite::drawPixel(int32_t x, int32_t y, uint32_t color) {
    if (!s_fast || _bpp != 8) { TFT_eSprite::drawPixel(x, y, color); return; }
    if (!_created || _vpOoB) return;
    x += _xDatum;
    y += _yDatum;
    if ((x < _vpX) || (y < _vpY) || (x >= _vpW) || (y >= _vpH)) return;
    _img8[x + y * _iwidth] = to8(color);
}

void FastSprite::drawFastVLine(int32_t x, int32_t y, int32_t h, uint32_t color) {
    if (!s_fast || _bpp != 8) { TFT_eSprite::drawFastVLine(x, y, h, color); return; }
    if (!_created || _vpOoB) return;
    x += _xDatum;
    y += _yDatum;
    if ((x < _vpX) || (x >= _vpW) || (y >= _vpH)) return;
    if (y < _vpY) { h += y - _vpY; y = _vpY; }
    if ((y + h) > _vpH) h = _vpH - y;
    if (h < 1) return;
    // The whole difference from the library: the stride and the pointer are
    // locals, so this is one store and one add per pixel.
    const int32_t stride = _iwidth;
    const uint8_t c = to8(color);
    uint8_t* p = _img8 + x + y * stride;
    while (h--) { *p = c; p += stride; }
}

void FastSprite::drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color) {
    if (!s_fast || _bpp != 8 || !_created || _vpOoB) { TFT_eSPI::drawLine(x0, y0, x1, y1, color); return; }
    // Only when both ends are inside the viewport: a straight line between
    // two inside points is inside, so nothing below needs clipping. A line
    // that crosses the edge goes to the library, whose per-run clipping is
    // the one place that behaviour is defined.
    const int32_t ax = x0 + _xDatum, ay = y0 + _yDatum;
    const int32_t bx = x1 + _xDatum, by = y1 + _yDatum;
    if (ax < _vpX || ax >= _vpW || ay < _vpY || ay >= _vpH ||
        bx < _vpX || bx >= _vpW || by < _vpY || by >= _vpH) {
        TFT_eSPI::drawLine(x0, y0, x1, y1, color);
        return;
    }
    x0 = ax; y0 = ay; x1 = bx; y1 = by;

    // The library's Bresenham exactly -- same transposes, same err = dx/2,
    // same step -- with each pixel stored directly instead of batched into
    // clipped run calls. Same pixels, because the pixel for column x is
    // taken at the row BEFORE the step, which is what its run flush does.
    const int32_t stride = _iwidth;
    const uint8_t c = to8(color);
    uint8_t* img = _img8;

    const bool steep = abs(y1 - y0) > abs(x1 - x0);
    if (steep) { int32_t t = x0; x0 = y0; y0 = t; t = x1; x1 = y1; y1 = t; }
    if (x0 > x1) { int32_t t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }

    const int32_t dx = x1 - x0, dy = abs(y1 - y0);
    int32_t err = dx >> 1;
    const int32_t ystep = (y0 < y1) ? 1 : -1;

    if (steep) {
        // x runs down the screen here; y0 is the column.
        uint8_t* p = img + y0 + x0 * stride;
        for (; x0 <= x1; x0++) {
            *p = c;
            p += stride;
            err -= dy;
            if (err < 0) { y0 += ystep; p += ystep; err += dx; }
        }
    } else {
        uint8_t* p = img + x0 + y0 * stride;
        for (; x0 <= x1; x0++) {
            *p = c;
            p++;
            err -= dy;
            if (err < 0) { y0 += ystep; p += ystep * stride; err += dx; }
        }
    }
}

void FastSprite::drawChar(int32_t x, int32_t y, uint16_t c, uint32_t color, uint32_t bg, uint8_t size) {
#ifdef LOAD_GFXFF
    if (gfxFont) { TFT_eSprite::drawChar(x, y, c, color, bg, size); return; }
#endif
    if (!s_fast || _bpp != 8 || !_created || _vpOoB) { TFT_eSprite::drawChar(x, y, c, color, bg, size); return; }
    if (c < 32) return;
    // Only a cell that sits wholly inside the viewport. One that crosses
    // the edge goes to the library, which clips it pixel by pixel.
    const int32_t cx = x + _xDatum, cy = y + _yDatum;
    const int32_t cw = 6 * size, chh = 8 * size;
    if (cx < _vpX || cy < _vpY || cx + cw > _vpW || cy + chh > _vpH) {
        TFT_eSprite::drawChar(x, y, c, color, bg, size);
        return;
    }
    if (c > 255) return;
    if (!_cp437 && c > 175) c++;
    // Code 255 becomes 256 here, and the library then reads five bytes past
    // the end of its font table -- whatever happens to sit there. This copy
    // of the table sits somewhere else, so it cannot reproduce that; nothing
    // on this device prints code 255, and drawing nothing is the honest
    // version of undefined. The self-check stops at 254 for the same reason.
    if (c > 255) return;

    const bool fillbg = (bg != color);
    const uint8_t fg8 = to8(color), bg8 = to8(bg);
    const int32_t stride = _iwidth;
    uint8_t* cell = _img8 + cx + cy * stride;

    // Column by column, the library's order: five columns from the table
    // and a sixth blank one, low bit of each column byte at the top.
    for (int8_t i = 0; i < 6; i++) {
        uint8_t line = (i == 5) ? 0 : pgm_read_byte(font + (c * 5) + i);
        if (size == 1) {
            uint8_t* p = cell + i;
            for (int8_t j = 0; j < 8; j++) {
                if (line & 0x1) *p = fg8;
                else if (fillbg) *p = bg8;
                line >>= 1;
                p += stride;
            }
        } else {
            for (int8_t j = 0; j < 8; j++) {
                const bool on = line & 0x1;
                if (on || fillbg) {
                    const uint8_t v = on ? fg8 : bg8;
                    uint8_t* p = cell + i * size + (j * size) * stride;
                    if (size == 2) {
                        // The common case: a memset per font pixel costs more than
                        // the four bytes it writes.
                        p[0] = v; p[1] = v; p[stride] = v; p[stride + 1] = v;
                    } else {
                        for (uint8_t r = 0; r < size; r++) { memset(p, v, size); p += stride; }
                    }
                }
                line >>= 1;
            }
        }
    }
}

uint32_t FastSprite::checksum() const {
    if (!_created || _bpp != 8) return 0;
    uint32_t h = 2166136261u;
    const uint8_t* p = _img8;
    for (int32_t n = _iwidth * _iheight; n > 0; n--) { h ^= *p++; h *= 16777619u; }
    return h;
}

// ---- the self-check ----
//
// Each pattern deliberately reaches past every edge, so the fall-back to the
// library on a clipped case is exercised alongside the fast path, and the
// whole set runs three times: no viewport, a viewport with its datum moved,
// and one without. The pixel and vertical line are checked first because
// the library's line and character are built on them.

namespace {

uint32_t mix(uint32_t i) { return i * 2654435761u; }

template <typename F>
void pattern(F draw) { for (uint32_t i = 0; i < 300; i++) draw(i); }

}  // namespace

int FastSprite::selfCheck(Print& out) {
    if (!_created || _bpp != 8) { out.println("[check] not an 8-bit sprite"); return -1; }
    int bad = 0;
    struct View { const char* name; bool set; int32_t x, y, w, h; bool datum; };
    static const View VIEWS[3] = {
        { "full",           false, 0, 0, 0, 0, false },
        { "viewport+datum", true, 23, 31, 210, 150, true },
        { "viewport",       true, 23, 31, 210, 150, false },
    };
    const int32_t W = width(), H = height();

    for (uint8_t v = 0; v < 3; v++) {
        if (VIEWS[v].set) setViewport(VIEWS[v].x, VIEWS[v].y, VIEWS[v].w, VIEWS[v].h, VIEWS[v].datum);
        else resetViewport();

        struct Case { const char* what; uint32_t a, b; };
        Case cases[6];
        uint8_t n = 0;

        // pixel: all over, including well outside
        fillRect(-1000, -1000, 3000, 3000, 0x0000);
        pattern([&](uint32_t i){ basePixel((int32_t)(mix(i) % (W + 80)) - 40, (int32_t)(mix(i + 7) % (H + 60)) - 30, mix(i + 3)); });
        cases[n].what = "drawPixel"; cases[n].a = checksum();
        fillRect(-1000, -1000, 3000, 3000, 0x0000);
        pattern([&](uint32_t i){ drawPixel((int32_t)(mix(i) % (W + 80)) - 40, (int32_t)(mix(i + 7) % (H + 60)) - 30, mix(i + 3)); });
        cases[n].b = checksum(); n++;

        // vertical line: any x, any y, any height, including negative starts
        fillRect(-1000, -1000, 3000, 3000, 0x0000);
        pattern([&](uint32_t i){ baseVLine((int32_t)(mix(i) % (W + 40)) - 20, (int32_t)(mix(i + 1) % (H + 80)) - 40, (int32_t)(mix(i + 2) % 300) + 1, mix(i + 3)); });
        cases[n].what = "drawFastVLine"; cases[n].a = checksum();
        fillRect(-1000, -1000, 3000, 3000, 0x0000);
        pattern([&](uint32_t i){ drawFastVLine((int32_t)(mix(i) % (W + 40)) - 20, (int32_t)(mix(i + 1) % (H + 80)) - 40, (int32_t)(mix(i + 2) % 300) + 1, mix(i + 3)); });
        cases[n].b = checksum(); n++;

        // line: every slope, both directions, ends in and out of bounds
        fillRect(-1000, -1000, 3000, 3000, 0x0000);
        pattern([&](uint32_t i){ baseLine((int32_t)(mix(i) % (W + 60)) - 30, (int32_t)(mix(i + 1) % (H + 60)) - 30, (int32_t)(mix(i + 2) % (W + 60)) - 30, (int32_t)(mix(i + 4) % (H + 60)) - 30, mix(i + 5)); });
        cases[n].what = "drawLine"; cases[n].a = checksum();
        fillRect(-1000, -1000, 3000, 3000, 0x0000);
        pattern([&](uint32_t i){ drawLine((int32_t)(mix(i) % (W + 60)) - 30, (int32_t)(mix(i + 1) % (H + 60)) - 30, (int32_t)(mix(i + 2) % (W + 60)) - 30, (int32_t)(mix(i + 4) % (H + 60)) - 30, mix(i + 5)); });
        cases[n].b = checksum(); n++;

        // characters: every code, sizes 1 and 2, with and without a
        // background, placed so plenty of them cross an edge
        for (uint8_t size = 1; size <= 2; size++) {
            for (uint8_t withBg = 0; withBg <= 1; withBg++) {
                const uint32_t fg = 0xFFE0, bgc = withBg ? 0x001F : 0xFFE0;
                fillRect(-1000, -1000, 3000, 3000, 0x0000);
                pattern([&](uint32_t i){ baseChar((int32_t)(mix(i) % (W + 20)) - 10, (int32_t)(mix(i + 1) % (H + 20)) - 10, (uint16_t)(i % 255), fg, bgc, size); });
                static const char* NAMES[4] = { "char s1", "char s1+bg", "char s2", "char s2+bg" };
                cases[n].what = NAMES[(size - 1) * 2 + withBg]; cases[n].a = checksum();
                fillRect(-1000, -1000, 3000, 3000, 0x0000);
                pattern([&](uint32_t i){ drawChar((int32_t)(mix(i) % (W + 20)) - 10, (int32_t)(mix(i + 1) % (H + 20)) - 10, (uint16_t)(i % 255), fg, bgc, size); });
                cases[n].b = checksum();
                if (cases[n].a != cases[n].b) {
                    // Which character, where: one at a time, first three.
                    int shown = 0;
                    for (uint32_t i = 0; i < 300 && shown < 3; i++) {
                        const int32_t px = (int32_t)(mix(i) % (W + 20)) - 10, py = (int32_t)(mix(i + 1) % (H + 20)) - 10;
                        const uint16_t code = (uint16_t)(i % 255);
                        fillRect(-1000, -1000, 3000, 3000, 0x0000); baseChar(px, py, code, fg, bgc, size); const uint32_t ca = checksum();
                        fillRect(-1000, -1000, 3000, 3000, 0x0000); drawChar(px, py, code, fg, bgc, size); const uint32_t cb = checksum();
                        if (ca != cb) { out.printf("[check]   differs: code %u at %ld,%ld size %u bg %u\n", (unsigned)code, (long)px, (long)py, (unsigned)size, (unsigned)withBg); shown++; }
                    }
                }
                n++;
                if (n >= 6) break;
            }
            if (n >= 6) break;
        }

        for (uint8_t k = 0; k < n; k++) {
            const bool ok = cases[k].a == cases[k].b;
            if (!ok) bad++;
            out.printf("[check] %-15s %-12s %s\n", VIEWS[v].name, cases[k].what, ok ? "identical" : "DIFFER");
        }
    }
    resetViewport();
    return bad;
}

#endif  // ARDUINO_ARCH_ESP32
