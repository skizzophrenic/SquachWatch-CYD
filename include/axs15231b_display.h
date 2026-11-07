// SquachWatch-CYD — the seam between the app and the JC3248W535's AXS15231B.
//
// Same arrangement as st77922_display.h: main.cpp upcasts its off-screen
// sprite to TFT_eSPI*, so TFT_eSPI has to stay as the TYPE, and it stops
// being a DRIVER. Its constructor is inert, the hot drawing methods are
// virtual, and the sprite never reaches the parent's bus except through
// pushSprite(), which this board never calls: FramePush's QSPI route ships
// the frame instead, through the push task, and Axs15231b (axs15231b_qspi.h)
// owns the bus.
//
// Rotation: same as the ST77922 path — the panel is always addressed in
// portrait, and rotations 1 and 3 are done in software.
#pragma once
#include <TFT_eSPI.h>

class Axs15231bTFT : public TFT_eSPI {
public:
    Axs15231bTFT() : TFT_eSPI(320, 480) {}

    // ---- hidden, not overridden -------------------------------------------
    void init(uint8_t tc = 0);
    void begin(uint8_t tc = 0) { init(tc); }
    void setRotation(uint8_t r);
    void writecommand(uint8_t) {}
    void writecommand(uint16_t) {}
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

    void     drawChar(int32_t x, int32_t y, uint16_t c, uint32_t color, uint32_t bg, uint8_t size) override;
    int16_t  drawChar(uint16_t uniCode, int32_t x, int32_t y, uint8_t font) override;
    int16_t  drawChar(uint16_t uniCode, int32_t x, int32_t y) override;
    void     begin_nin_write() override {}
    void     end_nin_write() override {}

    static void toPanel(uint8_t rot, int32_t x, int32_t y, int32_t w, int32_t h,
                        int32_t& px, int32_t& py, int32_t& pw, int32_t& ph);
private:
    int32_t _wx0 = 0, _wy0 = 0, _wx1 = 0, _wy1 = 0, _wcx = 0, _wcy = 0;
};
