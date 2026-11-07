// SquachWatch-CYD — Axs15231bTFT. See axs15231b_display.h.
#include "axs15231b_display.h"
#if defined(JC3248W535)
#include "axs15231b_qspi.h"
#include "frame_push.h"

void Axs15231bTFT::init(uint8_t) {
    // Deliberately does NOT call TFT_eSPI::init(): that would emit an ST7789
    // initialisation sequence down a bus this board does not have. The real
    // panel comes up here instead.
    Axs15231b::begin();
    _init_width  = _width  = Axs15231b::PANEL_W;
    _init_height = _height = Axs15231b::PANEL_H;
    rotation = 0;
    resetViewport();
}

void Axs15231bTFT::setRotation(uint8_t r) {
    rotation = (uint8_t)(r & 3);
    if (rotation & 1) { _width = Axs15231b::PANEL_H; _height = Axs15231b::PANEL_W; }
    else              { _width = Axs15231b::PANEL_W; _height = Axs15231b::PANEL_H; }
    resetViewport();
    FramePush::invalidate();
}

void Axs15231bTFT::invertDisplay(bool on) {
    FramePush::busLock();
    Axs15231b::invert(on);
    FramePush::busUnlock();
}

void Axs15231bTFT::toPanel(uint8_t rot, int32_t x, int32_t y, int32_t w, int32_t h,
                            int32_t& px, int32_t& py, int32_t& pw, int32_t& ph) {
    const int32_t W = Axs15231b::PANEL_W, H = Axs15231b::PANEL_H;
    switch (rot & 3) {
        case 0:  px = x;         py = y;         pw = w; ph = h; break;
        case 1:  px = W - y - h; py = x;         pw = h; ph = w; break;
        case 2:  px = W - x - w; py = H - y - h; pw = w; ph = h; break;
        default: px = y;         py = H - x - w; pw = h; ph = w; break;
    }
}

void Axs15231bTFT::fillScreen(uint32_t color) { fillRect(0, 0, _width, _height, color); }

void Axs15231bTFT::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) {
    if (w <= 0 || h <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > _width)  w = _width - x;
    if (y + h > _height) h = _height - y;
    if (w <= 0 || h <= 0) return;
    int32_t px, py, pw, ph;
    toPanel(rotation, x, y, w, h, px, py, pw, ph);
    FramePush::busLock();
    Axs15231b::fillRect((int16_t)px, (int16_t)py, (int16_t)pw, (int16_t)ph, (uint16_t)color);
    FramePush::busUnlock();
    FramePush::invalidate();
}

void Axs15231bTFT::drawPixel(int32_t x, int32_t y, uint32_t color) { fillRect(x, y, 1, 1, color); }
void Axs15231bTFT::drawFastHLine(int32_t x, int32_t y, int32_t w, uint32_t color) { fillRect(x, y, w, 1, color); }
void Axs15231bTFT::drawFastVLine(int32_t x, int32_t y, int32_t h, uint32_t color) { fillRect(x, y, 1, h, color); }

void Axs15231bTFT::drawLine(int32_t xs, int32_t ys, int32_t xe, int32_t ye, uint32_t color) {
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

uint16_t Axs15231bTFT::readPixel(int32_t, int32_t) { return 0; }

static void blitRuns(Axs15231bTFT& t, TFT_eSprite& s, int32_t x0, int32_t y0, int32_t w, int32_t h,
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

void Axs15231bTFT::drawChar(int32_t x, int32_t y, uint16_t c, uint32_t color, uint32_t bg, uint8_t size) {
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

int16_t Axs15231bTFT::drawChar(uint16_t uniCode, int32_t x, int32_t y, uint8_t font) {
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

int16_t Axs15231bTFT::drawChar(uint16_t uniCode, int32_t x, int32_t y) {
    return drawChar(uniCode, x, y, textfont);
}

void Axs15231bTFT::setWindow(int32_t xs, int32_t ys, int32_t xe, int32_t ye) {
    _wx0 = _wcx = xs; _wy0 = _wcy = ys; _wx1 = xe; _wy1 = ye;
}

void Axs15231bTFT::pushColor(uint16_t color) {
    drawPixel(_wcx, _wcy, color);
    if (++_wcx > _wx1) { _wcx = _wx0; if (++_wcy > _wy1) _wcy = _wy0; }
}

#endif  // JC3248W535
