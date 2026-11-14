#if defined(TAB5)
#include "tab5_display.h"
#include "tab5_panel.h"
#include "tab5_blit.h"
#include <Arduino.h>
#include <M5GFX.h>

// Everything here maps a LOGICAL coordinate (what the app thinks the screen
// is) onto a PANEL coordinate: a multiply by TAB5_SCALE. M5GFX, at
// TAB5_ROTATION, does the quarter turn into the framebuffer itself on this
// path, so these fallbacks and the blit agree on where a pixel lands.

void Tab5TFT::init(uint8_t) {
    // Deliberately does NOT call TFT_eSPI::init(): that emits an ST7789
    // initialisation sequence down an SPI bus this board does not have.
    if (!Tab5Panel::begin()) Serial.println("[tab5] panel did not come up; drawing goes nowhere");
    Tab5Blit::begin();

    _init_width  = _width  = TAB5_LOGICAL_W;
    _init_height = _height = TAB5_LOGICAL_H;
    rotation = 0;
    resetViewport();
}

void Tab5TFT::fillScreen(uint32_t color) {
    fillRect(0, 0, TAB5_LOGICAL_W, TAB5_LOGICAL_H, color);
}

void Tab5TFT::drawPixel(int32_t x, int32_t y, uint32_t color) {
    fillRect(x, y, 1, 1, color);
}

void Tab5TFT::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) {
    if (!Tab5Panel::up() || w <= 0 || h <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > TAB5_LOGICAL_W) w = TAB5_LOGICAL_W - x;
    if (y + h > TAB5_LOGICAL_H) h = TAB5_LOGICAL_H - y;
    if (w <= 0 || h <= 0) return;
    M5GFX& g = Tab5Panel::gfx();
    g.startWrite();
    g.fillRect(x * TAB5_SCALE, y * TAB5_SCALE, w * TAB5_SCALE, h * TAB5_SCALE, (uint16_t)color);
    g.endWrite();
}

void Tab5TFT::drawFastHLine(int32_t x, int32_t y, int32_t w, uint32_t color) { fillRect(x, y, w, 1, color); }
void Tab5TFT::drawFastVLine(int32_t x, int32_t y, int32_t h, uint32_t color) { fillRect(x, y, 1, h, color); }

void Tab5TFT::drawLine(int32_t xs, int32_t ys, int32_t xe, int32_t ye, uint32_t color) {
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

uint16_t Tab5TFT::readPixel(int32_t x, int32_t y) {
    if (!Tab5Panel::up() || x < 0 || y < 0 || x >= TAB5_LOGICAL_W || y >= TAB5_LOGICAL_H) return 0;
    return (uint16_t)Tab5Panel::gfx().readPixel(x * TAB5_SCALE, y * TAB5_SCALE);
}

void Tab5TFT::setWindow(int32_t xs, int32_t ys, int32_t xe, int32_t ye) {
    _wx0 = _wcx = xs; _wy0 = _wcy = ys; _wx1 = xe; _wy1 = ye;
}

void Tab5TFT::pushColor(uint16_t color) {
    drawPixel(_wcx, _wcy, color);
    if (++_wcx > _wx1) { _wcx = _wx0; if (++_wcy > _wy1) _wcy = _wy0; }
}

#endif  // TAB5
