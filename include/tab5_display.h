// SquachWatch-CYD — the seam between the app and the Tab5's MIPI-DSI panel.
//
// The CrowPanel 7's arrangement, for the same reason: src/main.cpp upcasts
// the off-screen SPRITE to the DEVICE type (`TFT_eSPI* canvas = &frame`) and
// every UI entry point takes a TFT_eSPI&, which only TFT_eSPI's own hierarchy
// allows. So TFT_eSPI stays as the TYPE and stops being a DRIVER: its
// constructor is inert, every hot drawing method is virtual, and the sprite
// never reaches the parent's bus except in pushSprite(), which this board
// never calls. M5GFX drives the panel underneath (tab5_panel.h) and
// Tab5Blit moves the sprite into its framebuffer.
//
// The methods below exist so that if anything ever draws straight to `tft`
// instead of the sprite -- the net under a failed sprite allocation -- it
// lands on the panel rather than in a write to a bus that is not there.
#pragma once

#include <TFT_eSPI.h>
#include "tab5_board.h"

class Tab5TFT : public TFT_eSPI {
public:
    Tab5TFT() : TFT_eSPI(TAB5_LOGICAL_W, TAB5_LOGICAL_H) {}

    // ---- hidden, not overridden -------------------------------------------
    void init(uint8_t tc = 0);
    void begin(uint8_t tc = 0) { init(tc); }

    // Rotation is locked for now: the blit turns the frame landscape, and a
    // second mapping (portrait) is a later round. See tab5_board.h.
    void setRotation(uint8_t) { rotation = 0; }

    // No command/data channel the app may use: applyColorOrder() in main.cpp
    // sends MADCTL through these on the SPI boards.
    void writecommand(uint8_t) {}
    void writecommand(uint16_t) {}
    void writedata(uint8_t) {}

    // Inversion is PANEL_NEEDS_INVERSION=false on this board.
    void invertDisplay(bool) {}

    void startWrite() {}
    void endWrite() {}
    void setAddrWindow(int32_t, int32_t, int32_t, int32_t) {}

    void fillScreen(uint32_t color);

    // ---- overridden virtuals ----------------------------------------------
    int16_t  width(void) override  { return TAB5_LOGICAL_W; }
    int16_t  height(void) override { return TAB5_LOGICAL_H; }
    void     drawPixel(int32_t x, int32_t y, uint32_t color) override;
    void     fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) override;
    void     drawFastHLine(int32_t x, int32_t y, int32_t w, uint32_t color) override;
    void     drawFastVLine(int32_t x, int32_t y, int32_t h, uint32_t color) override;
    void     drawLine(int32_t xs, int32_t ys, int32_t xe, int32_t ye, uint32_t color) override;
    uint16_t readPixel(int32_t x, int32_t y) override;
    void     pushColor(uint16_t color) override;
    void     setWindow(int32_t xs, int32_t ys, int32_t xe, int32_t ye) override;

private:
    int32_t _wx0 = 0, _wy0 = 0, _wx1 = 0, _wy1 = 0, _wcx = 0, _wcy = 0;
};
