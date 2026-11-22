// StopWatch TFT_eSPI adapter. Text fallback follows st77922_display.cpp.
#include "stopwatch_display.h"
#if defined(M5_STOPWATCH)
#include "stopwatch_board.h"
#include "stopwatch_geometry.h"

void StopWatchTFT::init(uint8_t) {
    StopWatch::begin();
    _init_width = _width = StopWatchGeometry::WIDTH;
    _init_height = _height = StopWatchGeometry::HEIGHT;
    rotation = 0;
    resetViewport();
}
void StopWatchTFT::setRotation(uint8_t) {
    rotation = 0; // one viewport and touch mapping, fixed orientation
    resetViewport();
}
void StopWatchTFT::invertDisplay(bool on) { StopWatch::invert(on); }
void StopWatchTFT::fillScreen(uint32_t color) { fillRect(0, 0, _width, _height, color); }
void StopWatchTFT::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) {
    StopWatch::fillRect(x, y, w, h, static_cast<uint16_t>(color));
}

void StopWatchTFT::drawPixel(int32_t x, int32_t y, uint32_t color) { fillRect(x, y, 1, 1, color); }
void StopWatchTFT::drawFastHLine(int32_t x, int32_t y, int32_t w, uint32_t color) { fillRect(x, y, w, 1, color); }
void StopWatchTFT::drawFastVLine(int32_t x, int32_t y, int32_t h, uint32_t color) { fillRect(x, y, 1, h, color); }

void StopWatchTFT::drawLine(int32_t xs, int32_t ys, int32_t xe, int32_t ye, uint32_t color) {
    // Bresenham through our own drawPixel; the base's version writes through
    // the bus rather than the primitives, so it cannot be inherited.
    int32_t dx = abs(xe - xs), sx = xs < xe ? 1 : -1;
    int32_t dy = -abs(ye - ys), sy = ys < ye ? 1 : -1;
    int32_t err = dx + dy;
    for (;;) {
        drawPixel(xs, ys, color);
        if (xs == xe && ys == ye) break;
        int32_t e2 = 2 * err;
        if (e2 >= dy) { err += dy; xs += sx; }
        if (e2 <= dx) { err += dx; ys += sy; }
    }
}

uint16_t StopWatchTFT::readPixel(int32_t, int32_t) {
    // No read-back on this path, and it is only the net under a failed
    // sprite allocation; the sprite answers readPixel from its own buffer.
    return 0;
}

// A rendered sprite onto the panel, one run of one colour at a time. With
// `transparent`, pixels of the key colour are left alone, which is how the
// library draws text whose background matches its foreground.
static void blitRuns(StopWatchTFT& t, TFT_eSprite& s, int32_t x0, int32_t y0, int32_t w, int32_t h,
                     bool transparent, uint16_t key) {
    for (int32_t y = 0; y < h; y++) {
        int32_t x = 0;
        while (x < w) {
            const uint16_t c = s.readPixel(x, y);
            int32_t run = 1;
            while (x + run < w && s.readPixel(x + run, y) == c) run++;
            if (!(transparent && c == key)) t.fillRect(x0 + x, y0 + y, run, 1, c);
            x += run;
        }
    }
}

void StopWatchTFT::drawChar(int32_t x, int32_t y, uint16_t c, uint32_t color, uint32_t bg, uint8_t size) {
    if (size < 1) size = 1;
    const int32_t w = 6 * size, h = 8 * size;
    const bool transparent = (color == bg);
    const uint16_t key = (uint16_t)~color;
    TFT_eSprite s(this);
    s.setColorDepth(16);
    if (!s.createSprite(w, h)) return;
    s.fillSprite(transparent ? key : (uint16_t)bg);
    s.drawChar(0, 0, c, color, transparent ? key : bg, size);
    blitRuns(*this, s, x, y, w, h, transparent, key);
    s.deleteSprite();
}

int16_t StopWatchTFT::drawChar(uint16_t uniCode, int32_t x, int32_t y, uint8_t font) {
    if (font <= 1) {
        drawChar(x, y, uniCode, textcolor, textbgcolor, textsize);
        return (int16_t)(6 * textsize);
    }
    char str[2] = { (char)uniCode, 0 };
    const int32_t w = textWidth(str, font), h = fontHeight(font);
    if (w <= 0 || h <= 0) return 0;
    const bool transparent = (textcolor == textbgcolor);
    const uint16_t key = (uint16_t)~textcolor;
    TFT_eSprite s(this);
    s.setColorDepth(16);
    if (!s.createSprite(w, h)) return (int16_t)w;
    s.fillSprite(transparent ? key : (uint16_t)textbgcolor);
    s.setTextSize(textsize);
    s.setTextColor(textcolor, transparent ? key : textbgcolor);
    s.drawChar(uniCode, 0, 0, font);
    blitRuns(*this, s, x, y, w, h, transparent, key);
    s.deleteSprite();
    return (int16_t)w;
}

int16_t StopWatchTFT::drawChar(uint16_t uniCode, int32_t x, int32_t y) {
    return drawChar(uniCode, x, y, textfont);
}

void StopWatchTFT::setWindow(int32_t xs, int32_t ys, int32_t xe, int32_t ye) {
    _wx0 = _wcx = xs; _wy0 = _wcy = ys; _wx1 = xe; _wy1 = ye;
}

void StopWatchTFT::pushColor(uint16_t color) {
    drawPixel(_wcx, _wcy, color);
    if (++_wcx > _wx1) { _wcx = _wx0; if (++_wcy > _wy1) _wcy = _wy0; }
}

#endif  // M5_STOPWATCH
