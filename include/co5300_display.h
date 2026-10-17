// SquachWatch-CYD — the seam between the app and the Waveshare 2.06"'s CO5300.
//
// St77922TFT's arrangement (st77922_display.h says why): TFT_eSPI stays as
// the TYPE the app's sprite upcasts to and stops being a DRIVER. Every frame
// goes out through FramePush's QSPI route; this class only answers the few
// things that draw straight at the panel, and hides every method that would
// reach a bus.
//
// The app sees the T-Watch's 240x240 screen. Direct draws are scaled onto
// the panel the same way the push scales frames (Co5300::edge()), and only
// rotations 0 and 2 exist: the screen is square, so 1 and 3 would only turn
// the picture sideways on a watch that has one way up.
#pragma once
#include <TFT_eSPI.h>

class Co5300TFT : public TFT_eSPI {
public:
    Co5300TFT() : TFT_eSPI(240, 240) {}

    // ---- hidden, not overridden -------------------------------------------
    void init(uint8_t tc = 0);
    void begin(uint8_t tc = 0) { init(tc); }
    void setRotation(uint8_t r);
    // The watch's panel sleep (applyBrightness() in main.cpp) sends DISPOFF,
    // SLPIN, SLPOUT and DISPON through these; they go to the panel. MADCTL
    // from applyColorOrder() and anything else is dropped: the panel's
    // MADCTL stays at 0 (portrait, RGB).
    void writecommand(uint8_t c);
    void writedata(uint8_t) {}
    void invertDisplay(bool on);
    void startWrite() {}
    void endWrite() {}
    void setAddrWindow(int32_t, int32_t, int32_t, int32_t) {}
    void fillScreen(uint32_t color);

    // ---- overridden virtuals ----------------------------------------------
    int16_t  width(void) override  { return _width; }
    int16_t  height(void) override { return _height; }
    void     drawPixel(int32_t x, int32_t y, uint32_t color) override;
    void     fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) override;
    void     drawFastHLine(int32_t x, int32_t y, int32_t w, uint32_t color) override;
    void     drawFastVLine(int32_t x, int32_t y, int32_t h, uint32_t color) override;
    void     drawLine(int32_t xs, int32_t ys, int32_t xe, int32_t ye, uint32_t color) override;
    uint16_t readPixel(int32_t x, int32_t y) override;
    void     pushColor(uint16_t color) override;
    void     setWindow(int32_t xs, int32_t ys, int32_t xe, int32_t ye) override;

    // Text through a small sprite, as on the 3.5": the library's own text
    // routines write straight to the S3's SPI2 registers, which this panel's
    // QSPI driver owns.
    void     drawChar(int32_t x, int32_t y, uint16_t c, uint32_t color, uint32_t bg, uint8_t size) override;
    int16_t  drawChar(uint16_t uniCode, int32_t x, int32_t y, uint8_t font) override;
    int16_t  drawChar(uint16_t uniCode, int32_t x, int32_t y) override;
    void     begin_nin_write() override {}
    void     end_nin_write() override {}

    // The AMOLED's brightness, 0..255 (there is no backlight to PWM).
    void     setBrightness(uint8_t level);

private:
    int32_t _wx0 = 0, _wy0 = 0, _wx1 = 0, _wy1 = 0, _wcx = 0, _wcy = 0;
};
