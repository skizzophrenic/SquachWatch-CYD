// TFT_eSPI compatibility adapter; M5GFX owns all physical I/O.
#pragma once
#include <TFT_eSPI.h>
#include "stopwatch_geometry.h"

class StopWatchTFT : public TFT_eSPI {
public:
    StopWatchTFT() : TFT_eSPI(StopWatchGeometry::WIDTH, StopWatchGeometry::HEIGHT) {}

    // ---- hidden, not overridden -------------------------------------------
    // Non-virtual in the base and only ever called by name on the one global
    // `tft`, so name-hiding binds them to ours.
    void init(uint8_t tc = 0);
    void begin(uint8_t tc = 0) { init(tc); }
    void setRotation(uint8_t r);
    // applyColorOrder() in main.cpp sends MADCTL through these on the SPI
    // boards; M5GFX owns the panel's color order and orientation.
    void writecommand(uint8_t) {}
    void writecommand(uint16_t) {}
    void writedata(uint8_t) {}
    void invertDisplay(bool on);
    // No transactions and no address window from the library's side.
    void startWrite() {}
    void endWrite() {}
    void setAddrWindow(int32_t, int32_t, int32_t, int32_t) {}
    void fillScreen(uint32_t color);

    // ---- overridden virtuals ----------------------------------------------
    // The net under the whole app: if the sprite ever fails to allocate,
    // main.cpp reseats `canvas` to point at `tft` itself and keeps drawing.
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

    // Match the Freenove QSPI adapter: render text through a temporary
    // sprite so TFT_eSPI never writes the SPI registers owned by M5GFX.
    void     drawChar(int32_t x, int32_t y, uint16_t c, uint32_t color, uint32_t bg, uint8_t size) override;
    int16_t  drawChar(uint16_t uniCode, int32_t x, int32_t y, uint8_t font) override;
    int16_t  drawChar(uint16_t uniCode, int32_t x, int32_t y) override;
    // The library's write bracket: SPI.beginTransaction() and the chip
    // select, on that same peripheral. Nothing to bracket here.
    void     begin_nin_write() override {}
    void     end_nin_write() override {}

private:
    int32_t _wx0 = 0, _wy0 = 0, _wx1 = 0, _wy1 = 0, _wcx = 0, _wcy = 0;
};
