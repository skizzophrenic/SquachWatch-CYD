// SquachWatch-CYD — Co5300TFT. See co5300_display.h.
#include "co5300_display.h"
#if defined(WAVESHARE_206)
#include "co5300_qspi.h"
#include "frame_push.h"

void Co5300TFT::init(uint8_t) {
    // Not TFT_eSPI::init(): there is no ST7789 on this board to initialise.
    Co5300::begin();
    _init_width  = _width  = Co5300::LOG;
    _init_height = _height = Co5300::LOG;
    rotation = 0;
    resetViewport();
}

void Co5300TFT::setRotation(uint8_t r) {
    rotation = (uint8_t)(r & 2);
    _width = _height = Co5300::LOG;
    resetViewport();
    FramePush::invalidate();
}

void Co5300TFT::writecommand(uint8_t c) {
    if (c != 0x10 && c != 0x11 && c != 0x28 && c != 0x29) return;
    FramePush::asyncWait();
    FramePush::busLock();
    Co5300::writeReg(c, nullptr, 0);
    FramePush::busUnlock();
}

void Co5300TFT::invertDisplay(bool on) {
    FramePush::busLock();
    Co5300::invert(on);
    FramePush::busUnlock();
}

void Co5300TFT::setBrightness(uint8_t level) {
    FramePush::busLock();
    Co5300::brightness(level);
    FramePush::busUnlock();
}

void Co5300TFT::fillScreen(uint32_t color) { fillRect(0, 0, _width, _height, color); }

void Co5300TFT::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) {
    if (w <= 0 || h <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > _width)  w = _width - x;
    if (y + h > _height) h = _height - y;
    if (w <= 0 || h <= 0) return;
    if (rotation == 2) { x = _width - x - w; y = _height - y - h; }
    const int32_t px0 = Co5300::OFF_X + Co5300::edge(x), px1 = Co5300::OFF_X + Co5300::edge(x + w);
    const int32_t py0 = Co5300::OFF_Y + Co5300::edge(y), py1 = Co5300::OFF_Y + Co5300::edge(y + h);
    FramePush::busLock();
    Co5300::fillRect((int16_t)px0, (int16_t)py0, (int16_t)(px1 - px0), (int16_t)(py1 - py0), (uint16_t)color);
    FramePush::busUnlock();
    FramePush::invalidate();   // the panel now holds something the push did not record
}

void Co5300TFT::drawPixel(int32_t x, int32_t y, uint32_t color) { fillRect(x, y, 1, 1, color); }
void Co5300TFT::drawFastHLine(int32_t x, int32_t y, int32_t w, uint32_t color) { fillRect(x, y, w, 1, color); }
void Co5300TFT::drawFastVLine(int32_t x, int32_t y, int32_t h, uint32_t color) { fillRect(x, y, 1, h, color); }

void Co5300TFT::drawLine(int32_t xs, int32_t ys, int32_t xe, int32_t ye, uint32_t color) {
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

uint16_t Co5300TFT::readPixel(int32_t, int32_t) { return 0; }

static void blitRuns(Co5300TFT& t, TFT_eSprite& s, int32_t x0, int32_t y0, int32_t w, int32_t h,
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

void Co5300TFT::drawChar(int32_t x, int32_t y, uint16_t c, uint32_t color, uint32_t bg, uint8_t size) {
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

int16_t Co5300TFT::drawChar(uint16_t uniCode, int32_t x, int32_t y, uint8_t font) {
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

int16_t Co5300TFT::drawChar(uint16_t uniCode, int32_t x, int32_t y) {
    return drawChar(uniCode, x, y, textfont);
}

void Co5300TFT::setWindow(int32_t xs, int32_t ys, int32_t xe, int32_t ye) {
    _wx0 = _wcx = xs; _wy0 = _wcy = ys; _wx1 = xe; _wy1 = ye;
}

void Co5300TFT::pushColor(uint16_t color) {
    drawPixel(_wcx, _wcy, color);
    if (++_wcx > _wx1) { _wcx = _wx0; if (++_wcy > _wy1) _wcy = _wy0; }
}

#endif  // WAVESHARE_206
