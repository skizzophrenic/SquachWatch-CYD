#if defined(CROWPANEL7)
#include "crowpanel7_display.h"
#include "crowpanel7_rgb.h"
#include "crowpanel7_backlight.h"
#include "crowpanel7_blit.h"
#include <Arduino.h>
#include <Wire.h>

// Everything here maps a LOGICAL coordinate (what the app thinks the screen
// is) onto a PANEL coordinate: a multiply by CROWPANEL_SCALE, which is 2 on
// this board and 1 in the -native build. One place for the mapping, so the
// scale is not a special case anywhere else in the driver.
static inline bool logicalToPanel(int32_t lx, int32_t ly, int32_t& px, int32_t& py) {
    if (lx < 0 || ly < 0 || lx >= SQW_LOGICAL_W || ly >= SQW_LOGICAL_H) return false;
    px = SQW_BLIT_X0 + lx * SQW_BLIT_SCALE;
    py = SQW_BLIT_Y0 + ly * SQW_BLIT_SCALE;
    return true;
}

void CrowPanelTFT::init(uint8_t) {
    // Deliberately does NOT call TFT_eSPI::init(): that emits an ST7789
    // initialisation sequence down an SPI bus this board does not have.
    //
    // This is where the real display comes up instead. It also starts Wire,
    // because the backlight lives on it and setup() dims the screen before
    // the touch controller is ever initialised.
    crowPanelBegin();
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_HZ);
    delay(20);
    CrowBL::begin();
    CrowBlit::begin();

    _init_width  = _width  = SQW_LOGICAL_W;
    _init_height = _height = SQW_LOGICAL_H;
    rotation = 0;
    // Keep the base's viewport bookkeeping consistent — TFT_eSprite and the
    // font code both read _vpW/_vpH through the inherited accessors.
    resetViewport();
}

void CrowPanelTFT::fillScreen(uint32_t color) {
    fillRect(0, 0, SQW_LOGICAL_W, SQW_LOGICAL_H, color);
}

void CrowPanelTFT::drawPixel(int32_t x, int32_t y, uint32_t color) {
    int32_t px, py;
    if (!logicalToPanel(x, y, px, py)) return;
    crowPanelFillRect(px, py, SQW_BLIT_SCALE, SQW_BLIT_SCALE, (uint16_t)color);
}

void CrowPanelTFT::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) {
    if (w <= 0 || h <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SQW_LOGICAL_W) w = SQW_LOGICAL_W - x;
    if (y + h > SQW_LOGICAL_H) h = SQW_LOGICAL_H - y;
    if (w <= 0 || h <= 0) return;
    crowPanelFillRect(SQW_BLIT_X0 + x * SQW_BLIT_SCALE, SQW_BLIT_Y0 + y * SQW_BLIT_SCALE,
                      w * SQW_BLIT_SCALE, h * SQW_BLIT_SCALE, (uint16_t)color);
}

void CrowPanelTFT::drawFastHLine(int32_t x, int32_t y, int32_t w, uint32_t color) { fillRect(x, y, w, 1, color); }
void CrowPanelTFT::drawFastVLine(int32_t x, int32_t y, int32_t h, uint32_t color) { fillRect(x, y, 1, h, color); }

void CrowPanelTFT::drawLine(int32_t xs, int32_t ys, int32_t xe, int32_t ye, uint32_t color) {
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

uint16_t CrowPanelTFT::readPixel(int32_t, int32_t) {
    // The IDF driver exposes no read-back, and this device path is only the
    // net under a failed sprite allocation -- the sprite (the normal canvas)
    // overrides readPixel with its own buffer, so nothing the app shows
    // depends on this answer.
    return 0;
}

void CrowPanelTFT::setWindow(int32_t xs, int32_t ys, int32_t xe, int32_t ye) {
    _wx0 = _wcx = xs; _wy0 = _wcy = ys; _wx1 = xe; _wy1 = ye;
}

void CrowPanelTFT::pushColor(uint16_t color) {
    drawPixel(_wcx, _wcy, color);
    if (++_wcx > _wx1) { _wcx = _wx0; if (++_wcy > _wy1) _wcy = _wy0; }
}

#endif  // CROWPANEL7
