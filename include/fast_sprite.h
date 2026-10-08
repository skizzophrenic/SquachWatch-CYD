// SquachWatch-CYD — the frame buffer's slow primitives, done directly.
//
// Every screen draws into one 8-bit sprite. Measured on the board (PRIM on
// the console), the library's horizontal line and filled rectangle are fine
// -- a memset -- but three things are not:
//
//   drawPixel       0.29 us each: fine alone, ruinous when something plots
//                   pixel by pixel, which the next two do
//   drawFastVLine   80 ns a pixel for a plain store, because its loop
//                   re-multiplies the row stride every pixel (the compiler
//                   cannot prove the store does not change the stride)
//   drawLine        35 us for a hundred pixels: one fully clipped call per
//                   run of one or two pixels
//   text            19 us a character at size 1, 50 at size 2: 48 drawPixel
//                   calls, or 48 fillRects, per character of the built-in
//                   font -- and this device is mostly text
//
// All of those are virtual, so this subclass takes them over for the one
// sprite the firmware has: same clipping, same viewport datum, same colour
// conversion, same Bresenham, same font table, but writing bytes into the
// buffer with a local pointer and stride instead of going through a clipped
// call per pixel. Anything that is not the plain 8-bit case -- another depth,
// a custom font, a character or line that crosses the viewport edge -- is
// handed straight back to the library, which is how the clipping stays its
// clipping rather than a second implementation of it.
//
// selfCheck() is the proof: it draws the same patterns through the library
// path and this one, edge cases and a viewport included, and compares
// checksums of the whole buffer. PRIM on the console runs it.
//
// The emulator's sprite is a different class with none of these members, so
// there this is a plain pass-through and nothing changes.
#pragma once
#include <TFT_eSPI.h>

class FastSprite;
// The frame every screen draws into, so code that is handed a TFT_eSPI& can
// tell whether it is this one and read its buffer directly (see row8()).
// nullptr in the emulator and on a board with no frame sprite.
extern FastSprite* g_frameSprite;

class FastSprite : public TFT_eSprite {
public:
    explicit FastSprite(TFT_eSPI* tft) : TFT_eSprite(tft) {}

    // The buffer's own size and start, which is NOT what width()/height()
    // report: with a datum viewport set those return the VIEWPORT's size, and
    // the 3.5" pushes its main screen through a 480x320 viewport laid over a
    // 480x160 buffer. Anything that walks the buffer row by row has to ask
    // here or it reads straight off the end -- a LoadStoreError panic, found
    // on that board on 2026-09-20 and the reason FramePush::push() takes a
    // buffer and a size rather than a sprite.
#if defined(CYD35)
    // The window-write guard. TFT_eSprite::setWindow() clamps the window it
    // is handed to width()/height(), and those report the VIEWPORT, not the
    // buffer -- so on a 480x160 buffer under a 480x320 viewport it will
    // happily accept row 319 and let pushColor() store at _img8[319*480],
    // 76 KB past the end of the allocation.
    //
    // That is a real crash, not a theoretical one: TFT_eSPI::drawWedgeLine()
    // (behind drawWideLine, which the art uses) starts its scan at the line's
    // RAW start row -- clipWindow() crops the end of the range but never the
    // start -- and feeds it straight to setWindow(). On every other board the
    // viewport IS the buffer and the datum is zero, so raw equals clipped and
    // this has been correct for years. Only the two-band buffer separates
    // them, which is why this is built for that board alone.
    //
    // The fix is the library's own logic against the right numbers: a window
    // wholly outside the buffer goes to the spare pixel callocSprite() puts
    // at the end for exactly this purpose, and one that overlaps is cropped
    // to the buffer. pushColor() wraps back to the window's start when it
    // runs off the end, so a cropped window can never walk out of it.
    //
    // It stops the corruption; it does not make a drawWideLine() land in the
    // right band, because that call reads the datum inconsistently to begin
    // with. Art drawn into the band sprite wants the four virtual primitives.
    void setWindow(int32_t x0, int32_t y0, int32_t x1, int32_t y1) override {
        if (x0 > x1) { const int32_t t = x0; x0 = x1; x1 = t; }
        if (y0 > y1) { const int32_t t = y0; y0 = y1; y1 = t; }
        if ((x0 >= _iwidth) || (x1 < 0) || (y0 >= _iheight) || (y1 < 0)) {
            _xs = 0; _ys = _dheight; _xe = 0; _ye = _dheight;   // the spare pixel
        } else {
            if (x0 < 0) x0 = 0;
            if (y0 < 0) y0 = 0;
            if (x1 >= _iwidth)  x1 = _iwidth  - 1;
            if (y1 >= _iheight) y1 = _iheight - 1;
            _xs = x0; _ys = y0; _xe = x1; _ye = y1;
        }
        _xptr = _xs; _yptr = _ys;
    }
#endif

#if defined(ARDUINO_ARCH_ESP32)
    int32_t  bufW() const { return _dwidth; }
    int32_t  bufH() const { return _dheight; }
    uint8_t* buf()  const { return _img8; }
    // Swap the 8-bit buffer for another of the same size and return the old
    // one: the C5's push task reads one while the loop draws into the other.
    uint8_t* swapBuf(uint8_t* nb) { uint8_t* o = _img8; _img8 = nb; return o; }
    // One row of the 8-bit buffer to read, in panel coordinates (the datum
    // applied): byte x is panel column x. nullptr when that row is not in
    // this pass's buffer, or this is not the plain 8-bit case. Reading
    // through readPixel() costs a virtual call, the clipping and a colour
    // conversion per pixel -- half a microsecond, which a loop over a few
    // thousand pixels a frame turns into milliseconds.
    const uint8_t* row8(int32_t y) const {
        if (!_created || _bpp != 8 || _xDatum != 0) return nullptr;
        y += _yDatum;
        if (y < 0 || y >= _iheight) return nullptr;
        return _img8 + y * _iwidth;
    }
#else
    // The emulator's sprite is a different class and keeps none of those, but
    // it has no fast push to feed either -- these exist so the one call site
    // compiles there.
    int32_t  bufW() { return width(); }
    int32_t  bufH() { return height(); }
    uint8_t* buf()  { return nullptr; }
#endif

#if defined(ARDUINO_ARCH_ESP32)
    // FAST OFF on the console: every override hands straight to the library,
    // so the two can be compared on one boot.
    static void setFast(bool on) { s_fast = on; }
    static bool fast() { return s_fast; }

    void drawPixel(int32_t x, int32_t y, uint32_t color) override;
    void drawFastVLine(int32_t x, int32_t y, int32_t h, uint32_t color) override;
    void drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color) override;
    void drawChar(int32_t x, int32_t y, uint16_t c, uint32_t color, uint32_t bg, uint8_t size) override;

    // The library's own versions, reachable for the self-check and for PRIM
    // to time side by side. (The library's line and character still call
    // drawPixel and friends virtually, so they run on THIS class's
    // primitives; the pixel and vertical line are the pure originals, and
    // they are checked first for that reason.)
    void basePixel(int32_t x, int32_t y, uint32_t c)                            { TFT_eSprite::drawPixel(x, y, c); }
    void baseVLine(int32_t x, int32_t y, int32_t h, uint32_t c)                 { TFT_eSprite::drawFastVLine(x, y, h, c); }
    void baseLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t c)   { TFT_eSPI::drawLine(x0, y0, x1, y1, c); }
    void baseChar(int32_t x, int32_t y, uint16_t ch, uint32_t c, uint32_t bg, uint8_t s) { TFT_eSprite::drawChar(x, y, ch, c, bg, s); }

    // FNV-1a over the whole buffer.
    uint32_t checksum() const;

    // Draws every pattern twice, library then fast, and compares. Prints one
    // line per primitive; returns how many differed. Scribbles on the
    // sprite, which the next frame repaints.
    int selfCheck(Print& out);

private:
    static bool s_fast;
#endif
};
