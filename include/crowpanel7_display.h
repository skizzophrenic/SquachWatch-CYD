// SquachWatch-CYD — the seam between the app and the CrowPanel 7's RGB panel.
//
// WHY THIS IS A TFT_eSPI SUBCLASS AND NOT A LovyanGFX OBJECT
//
// src/main.cpp does `TFT_eSPI* canvas = &frame;` — it upcasts the off-screen
// SPRITE to the DEVICE type, and every UI entry point in the tree takes a
// `TFT_eSPI&` (49 such parameters in include/theme.h alone). That only works
// because in TFT_eSPI the sprite derives from the device.
//
// LovyanGFX has no such relationship: LGFX_Sprite and LGFX_Device are
// siblings, both deriving from LovyanGFX, and neither derives from the other.
// Its own compat header does `using TFT_eSPI = LGFX;`, under which that
// upcast does not compile. Adopting it would mean retyping every drawing
// signature in the repo — a permanent merge conflict with upstream on
// essentially every UI file, to gain nothing this board needs.
//
// So TFT_eSPI stays as the TYPE and stops being a DRIVER. Its constructor is
// inert (pure member initialisation — no pinMode, no SPI.begin, no register
// write), every hot drawing method is virtual so a subclass can intercept it,
// and the sprite never reaches the parent's bus except in pushSprite(), which
// this board never calls. ESP-IDF's esp_lcd RGB driver (crowpanel7_rgb.h)
// drives the panel underneath.
//
// The practical upshot: the app renders into its 8-bit sprite exactly as it
// does on every other board, and CrowBlit moves that sprite into the RGB
// framebuffer. The methods below exist to make sure that if anything ever
// draws straight to `tft` instead, it lands on the panel rather than in a
// silent write to a bus that is not there.
#pragma once

#include <TFT_eSPI.h>
#include "crowpanel7_board.h"

class CrowPanelTFT : public TFT_eSPI {
public:
    CrowPanelTFT() : TFT_eSPI(SQW_LOGICAL_W, SQW_LOGICAL_H) {}

    // ---- hidden, not overridden -------------------------------------------
    // These are non-virtual in the base and are only ever called by name on
    // the one global `tft`, so name-hiding binds them statically to ours.
    void init(uint8_t tc = 0);
    void begin(uint8_t tc = 0) { init(tc); }

    // Rotation is locked on this board: the panel is natively landscape and
    // there is no MADCTL to rotate it with — it would be a per-pixel software
    // transform of an 800x480 buffer, every frame, for nothing.
    void setRotation(uint8_t) { rotation = 0; }

    // There is no command/data channel on an RGB panel; both of these would
    // be writes into a bus that does not exist. applyColorOrder() in main.cpp
    // sends MADCTL through them on the SPI boards.
    void writecommand(uint8_t) {}
    void writecommand(uint16_t) {}
    void writedata(uint8_t) {}

    // An RGB panel has no inversion register; the app's INVERT setting is
    // handled by PANEL_NEEDS_INVERSION being false on this board.
    void invertDisplay(bool) {}

    // No transactions and no address window: the framebuffer is memory.
    void startWrite() {}
    void endWrite() {}
    void setAddrWindow(int32_t, int32_t, int32_t, int32_t) {}

    void fillScreen(uint32_t color);

    // ---- overridden virtuals ----------------------------------------------
    // The net under the whole app: if the 384 KB sprite ever fails to
    // allocate, main.cpp reseats `canvas` to point at `tft` itself and keeps
    // drawing. On this board that has to reach the panel.
    int16_t  width(void) override  { return SQW_LOGICAL_W; }
    int16_t  height(void) override { return SQW_LOGICAL_H; }
    void     drawPixel(int32_t x, int32_t y, uint32_t color) override;
    void     fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) override;
    void     drawFastHLine(int32_t x, int32_t y, int32_t w, uint32_t color) override;
    void     drawFastVLine(int32_t x, int32_t y, int32_t h, uint32_t color) override;
    void     drawLine(int32_t xs, int32_t ys, int32_t xe, int32_t ye, uint32_t color) override;
    uint16_t readPixel(int32_t x, int32_t y) override;
    void     pushColor(uint16_t color) override;
    void     setWindow(int32_t xs, int32_t ys, int32_t xe, int32_t ye) override;

private:
    // setWindow/pushColor keep a cursor, for the few library paths that push
    // a run of pixels into a window rather than calling a primitive.
    int32_t _wx0 = 0, _wy0 = 0, _wx1 = 0, _wy1 = 0, _wcx = 0, _wcy = 0;
};
