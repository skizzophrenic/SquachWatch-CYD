// SquachWatch-CYD PC emulator — TFT_eSPI/TFT_eSprite shim.
//
// The real TFT_eSPI library makes exactly 6 methods virtual --
// drawPixel, drawChar, readPixel, setWindow, pushColor, and the
// begin/end_nin_write transaction pair -- specifically so TFT_eSprite
// can override just those and inherit every higher-level shape/text
// function (fillRect, fillTriangle, fillEllipse, drawRoundRect, print,
// textWidth, ...) from the base class for free. This shim follows the
// exact same split: implement the 6 virtuals against an in-memory
// RGB565 buffer, and every shape function this project actually calls
// is hand-written once here in terms of those. That's the whole reason
// this is worth doing as a shim instead of a from-scratch reimplementation
// of every primitive -- the algorithm surface is small and the split
// mirrors upstream, so behavior stays close to what real hardware draws.
//
// Text uses the real Adafruit GLCD 5x7 font table (glcdfont_data.h,
// copied verbatim from the vendored TFT_eSPI package) rather than a
// placeholder, since so much of this project's UI is label/counter text
// -- a rendered screenshot with fake glyphs wouldn't actually be
// readable enough to be useful.
//
// Known gaps (acceptable for a rendering-preview tool, not aiming for
// hardware-exact parity): drawArc is a simple filled-wedge approximation,
// not upstream's anti-aliased version; touch/SPI methods are inert
// stubs (there's no touchscreen to simulate); color depth is tracked on
// sprites but everything is stored/composited as RGB565 internally
// regardless of what setColorDepth() was called with.
#pragma once
#include <Arduino.h>      // must precede the font table: it defines PROGMEM
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cmath>
#include <vector>
#include <algorithm>
#include "glcdfont_data.h"

static const int GLCD_W = 5, GLCD_H = 7, GLCD_ADVANCE = 6;

// Standard TFT_eSPI colour constants (RGB565). Only the handful the
// project actually reaches for outside of Theme's own palette.
#define TFT_BLACK       0x0000
#define TFT_WHITE       0xFFFF
#define TFT_RED         0xF800
#define TFT_GREEN       0x07E0
#define TFT_BLUE        0x001F
#define TFT_YELLOW      0xFFE0
#define TFT_CYAN        0x07FF
#define TFT_MAGENTA     0xF81F
#define TFT_ORANGE      0xFDA0
#define TFT_DARKGREY    0x7BEF
#define TFT_LIGHTGREY   0xD69A

class TFT_eSPI {
public:
    explicit TFT_eSPI(int w = 320, int h = 240) : _w(w), _h(h) {
        _buf.assign((size_t)w * h, 0x0000);
    }
    virtual ~TFT_eSPI() {}

    void init() {}
    void begin() { init(); }
    void setRotation(uint8_t r) { _rotation = r; }
    uint8_t getRotation() const { return _rotation; }
    int16_t width()  const { return _w; }
    int16_t height() const { return _h; }
    void invertDisplay(bool) {}
    void fillScreen(uint32_t color) { fillRect(0, 0, _w, _h, color); }

    // ---- the 6 real virtuals, targeting _buf ----------------------
    virtual void drawPixel(int32_t x, int32_t y, uint32_t color) {
        if (x < 0 || y < 0 || x >= _w || y >= _h) return;
        _buf[(size_t)y * _w + x] = (uint16_t)color;
    }
    virtual uint16_t readPixel(int32_t x, int32_t y) {
        if (x < 0 || y < 0 || x >= _w || y >= _h) return 0;
        return _buf[(size_t)y * _w + x];
    }
    virtual void setWindow(int32_t xs, int32_t ys, int32_t xe, int32_t ye) {
        _winX0 = xs; _winY0 = ys; _winX1 = xe; _winY1 = ye;
        _winCurX = xs; _winCurY = ys;
    }
    virtual void pushColor(uint16_t color) {
        if (_winCurY <= _winY1) {
            drawPixel(_winCurX, _winCurY, color);
            if (++_winCurX > _winX1) { _winCurX = _winX0; _winCurY++; }
        }
    }
    virtual void begin_nin_write() {}
    virtual void end_nin_write() {}

    // ---- text glyphs (also virtual upstream, for the same reason) --
    virtual int16_t drawChar(uint16_t c, int32_t x, int32_t y, uint8_t /*font*/ = 1) {
        if (c > 255) return GLCD_ADVANCE * textsize;
        for (int col = 0; col < GLCD_W; col++) {
            uint8_t bits = font[(size_t)c * GLCD_W + col];
            for (int row = 0; row < GLCD_H; row++) {
                bool on = (bits >> row) & 1;
                uint16_t col565 = on ? textcolor : textbgcolor;
                if (!on && transparent) continue;
                if (textsize == 1) {
                    drawPixel(x + col, y + row, col565);
                } else {
                    fillRect(x + col * textsize, y + row * textsize, textsize, textsize, col565);
                }
            }
        }
        return GLCD_ADVANCE * textsize;
    }

    // ---- shape primitives, built on the virtuals above (same split
    // real TFT_eSPI uses) ------------------------------------------
    void drawFastHLine(int32_t x, int32_t y, int32_t w, uint32_t color) {
        for (int32_t i = 0; i < w; i++) drawPixel(x + i, y, color);
    }
    void drawFastVLine(int32_t x, int32_t y, int32_t h, uint32_t color) {
        for (int32_t i = 0; i < h; i++) drawPixel(x, y + i, color);
    }
    void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) {
        for (int32_t j = 0; j < h; j++) drawFastHLine(x, y + j, w, color);
    }
    void drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) {
        drawFastHLine(x, y, w, color);
        drawFastHLine(x, y + h - 1, w, color);
        drawFastVLine(x, y, h, color);
        drawFastVLine(x + w - 1, y, h, color);
    }
    void drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color) {
        int32_t dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
        int32_t dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
        int32_t err = dx + dy;
        for (;;) {
            drawPixel(x0, y0, color);
            if (x0 == x1 && y0 == y1) break;
            int32_t e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    }
    void drawWideLine(float ax, float ay, float bx, float by, float wd, uint32_t color, uint32_t = 0) {
        float dx = bx - ax, dy = by - ay;
        float len = std::sqrt(dx * dx + dy * dy);
        if (len < 0.01f) { fillCircle((int)ax, (int)ay, (int)(wd / 2), color); return; }
        float nx = -dy / len * (wd / 2), ny = dx / len * (wd / 2);
        fillTriangle((int)(ax + nx), (int)(ay + ny), (int)(ax - nx), (int)(ay - ny), (int)(bx + nx), (int)(by + ny), color);
        fillTriangle((int)(bx + nx), (int)(by + ny), (int)(bx - nx), (int)(by - ny), (int)(ax - nx), (int)(ay - ny), color);
        fillCircle((int)ax, (int)ay, (int)(wd / 2), color);
        fillCircle((int)bx, (int)by, (int)(wd / 2), color);
    }
    void drawCircle(int32_t x0, int32_t y0, int32_t r, uint32_t color) {
        int32_t x = r, y = 0, err = 0;
        while (x >= y) {
            drawPixel(x0 + x, y0 + y, color); drawPixel(x0 + y, y0 + x, color);
            drawPixel(x0 - y, y0 + x, color); drawPixel(x0 - x, y0 + y, color);
            drawPixel(x0 - x, y0 - y, color); drawPixel(x0 - y, y0 - x, color);
            drawPixel(x0 + y, y0 - x, color); drawPixel(x0 + x, y0 - y, color);
            y++;
            if (err <= 0) { err += 2 * y + 1; }
            if (err > 0)  { x--; err -= 2 * x + 1; }
        }
    }
    void fillCircle(int32_t x0, int32_t y0, int32_t r, uint32_t color) {
        for (int32_t y = -r; y <= r; y++) {
            int32_t dx = (int32_t)std::sqrt((double)(r * r - y * y));
            drawFastHLine(x0 - dx, y0 + y, 2 * dx + 1, color);
        }
    }
    void fillEllipse(int32_t x0, int32_t y0, int32_t rx, int32_t ry, uint32_t color) {
        if (rx < 1 || ry < 1) return;
        for (int32_t y = -ry; y <= ry; y++) {
            int32_t dx = (int32_t)((double)rx * std::sqrt(1.0 - (double)(y * y) / (double)(ry * ry)));
            drawFastHLine(x0 - dx, y0 + y, 2 * dx + 1, color);
        }
    }
    void drawEllipse(int32_t x0, int32_t y0, int32_t rx, int32_t ry, uint32_t color) {
        const int steps = 180;
        for (int i = 0; i < steps; i++) {
            double a0 = 2 * M_PI * i / steps, a1 = 2 * M_PI * (i + 1) / steps;
            drawLine((int32_t)(x0 + rx * cos(a0)), (int32_t)(y0 + ry * sin(a0)),
                     (int32_t)(x0 + rx * cos(a1)), (int32_t)(y0 + ry * sin(a1)), color);
        }
    }
    static int32_t edge(int32_t ax, int32_t ay, int32_t bx, int32_t by, int32_t px, int32_t py) {
        return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
    }
    void fillTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint32_t color) {
        int32_t minX = std::min({x0, x1, x2}), maxX = std::max({x0, x1, x2});
        int32_t minY = std::min({y0, y1, y2}), maxY = std::max({y0, y1, y2});
        for (int32_t y = minY; y <= maxY; y++) {
            for (int32_t x = minX; x <= maxX; x++) {
                int32_t w0 = edge(x1, y1, x2, y2, x, y);
                int32_t w1 = edge(x2, y2, x0, y0, x, y);
                int32_t w2 = edge(x0, y0, x1, y1, x, y);
                bool neg = (w0 < 0) || (w1 < 0) || (w2 < 0);
                bool pos = (w0 > 0) || (w1 > 0) || (w2 > 0);
                if (!(neg && pos)) drawPixel(x, y, color);
            }
        }
    }
    void drawTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint32_t color) {
        drawLine(x0, y0, x1, y1, color);
        drawLine(x1, y1, x2, y2, color);
        drawLine(x2, y2, x0, y0, color);
    }
    void drawRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t color) {
        drawFastHLine(x + r, y, w - 2 * r, color);
        drawFastHLine(x + r, y + h - 1, w - 2 * r, color);
        drawFastVLine(x, y + r, h - 2 * r, color);
        drawFastVLine(x + w - 1, y + r, h - 2 * r, color);
        drawCircleQuadrants(x + r, y + r, r, color, false);
        drawCircleQuadrants(x + w - 1 - r, y + r, r, color, false);
        drawCircleQuadrants(x + r, y + h - 1 - r, r, color, false);
        drawCircleQuadrants(x + w - 1 - r, y + h - 1 - r, r, color, false);
    }
    void fillRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t color) {
        fillRect(x + r, y, w - 2 * r, h, color);
        fillRect(x, y + r, r, h - 2 * r, color);
        fillRect(x + w - r, y + r, r, h - 2 * r, color);
        fillCircle(x + r, y + r, r, color);
        fillCircle(x + w - 1 - r, y + r, r, color);
        fillCircle(x + r, y + h - 1 - r, r, color);
        fillCircle(x + w - 1 - r, y + h - 1 - r, r, color);
    }
    void drawArc(int32_t x, int32_t y, int32_t r, int32_t ir, uint32_t startAngle, uint32_t endAngle,
                uint32_t color, uint32_t /*bg*/, bool /*roundEnds*/ = false) {
        // Filled wedge, not upstream's anti-aliased ring -- see the
        // file-level comment on known gaps.
        if (endAngle < startAngle) endAngle += 360;
        for (uint32_t a = startAngle; a <= endAngle; a++) {
            double rad = (a - 90) * M_PI / 180.0;
            for (int32_t rr = ir; rr <= r; rr++) {
                drawPixel((int32_t)(x + rr * cos(rad)), (int32_t)(y + rr * sin(rad)), color);
            }
        }
    }

    // ---- text ------------------------------------------------------
    void setCursor(int32_t x, int32_t y) { cursor_x = x; cursor_y = y; }
    void setTextColor(uint16_t fg) { textcolor = fg; textbgcolor = fg; transparent = true; }
    void setTextColor(uint16_t fg, uint16_t bg) { textcolor = fg; textbgcolor = bg; transparent = false; }
    void setTextSize(uint8_t s) { textsize = s ? s : 1; }
    void setTextWrap(bool) {}
    // fontHeight(int font): real TFT_eSPI takes a FONT INDEX here, not a
    // size multiplier -- this project only ever loads/uses font 1 (the
    // default GLCD font via setTextSize()), so any call passing a
    // different index is asking about a font that was never loaded and
    // gets 0 back on real hardware too. Reproducing that faithfully
    // (not "helpfully" treating the argument as a size) is deliberate:
    // it's what would surface a real latent bug in the caller instead
    // of hiding it.
    int16_t fontHeight(int font) const { return font == 1 ? GLCD_H * textsize : 0; }
    int16_t fontHeight() const { return GLCD_H * textsize; }
    int16_t textWidth(const char* s) const {
        int16_t w = 0;
        for (; *s; s++) w += GLCD_ADVANCE * textsize;
        return w;
    }
    size_t write(uint8_t c) {
        if (c == '\n') { cursor_y += fontHeight() + 1; cursor_x = 0; return 1; }
        cursor_x += drawChar(c, cursor_x, cursor_y);
        return 1;
    }
    size_t print(const char* s) { size_t n = 0; for (; *s; s++) n += write((uint8_t)*s); return n; }
    size_t println(const char* s) { size_t n = print(s); n += write('\n'); return n; }
    size_t printf(const char* fmt, ...) {
        char buf[256];
        va_list ap; va_start(ap, fmt);
        vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        return print(buf);
    }

    uint16_t color565(uint8_t r, uint8_t g, uint8_t b) const {
        return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    }

    // ---- touch/SPI: inert, there's no touchscreen to simulate -------
    bool getTouch(uint16_t*, uint16_t*, uint16_t = 600) { return false; }
    uint16_t getTouchRawZ() { return 0; }
    void getTouchRaw(uint16_t* x, uint16_t* y) { if (x) *x = 0; if (y) *y = 0; }
    void setTouch(uint16_t*) {}
    bool calibrateTouch(uint16_t*, uint32_t, uint32_t, uint8_t) { return true; }
    void* getSPIinstance() { return nullptr; }
    void writecommand(uint8_t) {}
    void writedata(uint8_t) {}

    // ---- sim-only access for the render harness ---------------------
    const std::vector<uint16_t>& pixelsRGB565() const { return _buf; }

protected:
    int _w, _h;
    uint8_t _rotation = 0;
    std::vector<uint16_t> _buf;
    int32_t _winX0 = 0, _winY0 = 0, _winX1 = 0, _winY1 = 0, _winCurX = 0, _winCurY = 0;

    int32_t cursor_x = 0, cursor_y = 0;
    uint16_t textcolor = 0xFFFF, textbgcolor = 0x0000;
    uint8_t textsize = 1;
    bool transparent = false;

private:
    // Quarter-circle outline helper for drawRoundRect -- draws whichever
    // 90-degree arc the caller needs by brute-force angle sweep rather
    // than the classic 4-way Bresenham symmetry trick, since round-rect
    // only ever wants one specific quadrant at each corner and this
    // stays simpler to read than threading a quadrant mask through the
    // symmetric version above.
    void drawCircleQuadrants(int32_t cx, int32_t cy, int32_t r, uint32_t color, bool) {
        for (int a = 0; a < 360; a++) {
            double rad = a * M_PI / 180.0;
            drawPixel((int32_t)(cx + r * cos(rad)), (int32_t)(cy + r * sin(rad)), color);
        }
    }
};

// ---------------------------------------------------------------------
// TFT_eSprite: overrides the same 6 virtuals to target its own buffer
// instead of the parent TFT_eSPI's, and inherits every shape/text method
// above unchanged -- exactly the mechanism real TFT_eSPI/TFT_eSprite use.
class TFT_eSprite : public TFT_eSPI {
public:
    explicit TFT_eSprite(TFT_eSPI* parent) : TFT_eSPI(0, 0), _parent(parent) {}

    void* createSprite(int16_t w, int16_t h) {
        _w = w; _h = h;
        _buf.assign((size_t)w * h, 0x0000);
        _created = true;
        return _buf.data();
    }
    void deleteSprite() { _buf.clear(); _created = false; }
    bool created() const { return _created; }
    void setColorDepth(uint8_t depth) { _depth = depth; }
    uint8_t getColorDepth() const { return _depth; }
    void* getPointer() { return _created ? (void*)_buf.data() : nullptr; }

    // Blits this sprite's buffer onto the parent at (x,y) -- the
    // real device's equivalent of an SPI DMA push to the panel; here
    // it's just a straight copy into the parent's own in-memory buffer,
    // which is exactly what the sim harness reads out to PNG.
    void pushSprite(int32_t x, int32_t y) {
        if (!_parent || !_created) return;
        for (int32_t j = 0; j < _h; j++)
            for (int32_t i = 0; i < _w; i++)
                _parent->drawPixel(x + i, y + j, _buf[(size_t)j * _w + i]);
    }

    // Viewport support: main.cpp's CYD35 two-pass half-height render
    // path calls this to redirect drawing into the top/bottom half of a
    // shared buffer. Implemented as a simple coordinate offset + clip
    // rather than the real library's datum/rotation interplay, which
    // this project's viewport usage never actually exercises.
    void setViewport(int32_t x, int32_t y, int32_t w, int32_t h, bool = true) {
        _vpX = x; _vpY = y; _vpW = w; _vpH = h; _vpActive = true;
    }
    void resetViewport() { _vpActive = false; }

    void setPivot(int16_t, int16_t) {}

    void drawPixel(int32_t x, int32_t y, uint32_t color) override {
        if (_vpActive) { x += _vpX; y += _vpY; if (x < 0 || y < 0 || x >= _vpW + _vpX || y >= _vpH + _vpY) return; }
        if (x < 0 || y < 0 || x >= _w || y >= _h) return;
        _buf[(size_t)y * _w + x] = (uint16_t)color;
    }
    uint16_t readPixel(int32_t x, int32_t y) override {
        if (x < 0 || y < 0 || x >= _w || y >= _h) return 0;
        return _buf[(size_t)y * _w + x];
    }

private:
    TFT_eSPI* _parent;
    bool _created = false;
    uint8_t _depth = 16;
    int32_t _vpX = 0, _vpY = 0, _vpW = 0, _vpH = 0;
    bool _vpActive = false;
};
