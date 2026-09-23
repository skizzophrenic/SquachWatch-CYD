// SquachWatch-CYD — pushing the frame buffer without waiting on the wire.
//
// Every screen is drawn into an 8-bit sprite and shipped to the panel by
// TFT_eSPI's own 8-bit push: convert one line of 8-bit colour into the 16-bit
// words the panel wants, hand it to the SPI hardware 64 bytes at a time,
// and SPIN while each 64 bytes goes out. Then the next line. The wire idles
// while a line converts, and the CPU idles while a line transmits -- about
// 7 ms a frame of each waiting on the other, on top of the 15.4 ms (80 MHz)
// or 30.7 ms (40 MHz) the bytes themselves cost.
//
// This reorders that loop and nothing else: kick 64 bytes, and convert the
// NEXT 64 bytes while they go out instead of spinning. Same registers, same
// bus, same chip select and transaction the library's push already holds --
// it is the library's loop with the wait put to use, not a second driver.
// (A second driver is what the DMA attempt was, and the engine never
// completed a single transfer under it. That path is closed; this is the
// other way round to the same 7 ms.)
//
// Gated to the 2.8" CYDs to start with, by agreement, not by mechanism: there
// is nothing here the other boards' bus could not do, and it can be widened
// once it has proven itself on the two boards it can be measured on.
#pragma once
#include <Arduino.h>

class TFT_eSPI;

// Every board, now. The gate started narrow on purpose -- one board at a
// time, each one measured before the next was let in -- and they have all
// been through it: the 2.8" CYDs in v1.15.0, the RL Phantom and the 3.5"
// after that, and AWOK on 2026-09-20, which turned out to be spending two
// thirds of its frame on the wire. Nothing board-specific is left in here,
// so the condition is simply "a board", and a new one inherits it: the push
// is the library's own transaction with the waiting put to use, and it
// declines any frame whose shape it does not recognise rather than guessing.
// Not on the T-Watch S3 yet: the overlapped push talks to the ESP32 SPI
// registers, and the S3 lays them out differently -- the first frame through
// it came out white. pushSprite() until it is ported. And not on the
// CrowPanel 7, which has no SPI display at all: CrowBlit writes rows into
// its RGB framebuffer (crowpanel7_blit.h).
#if defined(ARDUINO_ARCH_ESP32) && !defined(TWATCH_S3) && !defined(CROWPANEL7)
  #define SQW_FRAME_PUSH 1
#else
  #define SQW_FRAME_PUSH 0
#endif

namespace FramePush {

// One pixel of 8-bit colour as the 16 bits that go on the wire, in WIRE
// order: the panel wants the high byte first, and the SPI hardware sends a
// word's bytes lowest address first, so the high byte sits in the low half.
// This is TFT_eSPI's own 8-bit arithmetic, character for character, and it
// lives out here so a host test can check all 256 of them -- a version that
// differs by one bit shifts every colour on the device slightly and nothing
// would look obviously wrong.
//
// Blue has four levels, not eight: two bits in an 8-bit colour. That is why
// the near-neutrals on this device are the handful they are, and the table
// is the library's own rather than derived, so the two can never drift.
inline uint16_t rgb332Wire(uint8_t c) {
    static const uint8_t BLUE4[4] = { 0, 11, 21, 31 };
    //                        --- green ---     ------------ red ------------
    const uint8_t hi = (uint8_t)((c & 0x1C) >> 2 | (c & 0xC0) >> 3 | (c & 0xE0));
    //                        --- green ---    --- blue ---
    const uint8_t lo = (uint8_t)((c & 0x1C) << 3 | BLUE4[c & 0x03]);
    return (uint16_t)(hi | (lo << 8));
}

// Which rows to send, given which rows changed. Each run of changed rows
// becomes a span [r0, r1), widened to whole multiples of `align` rows (a
// span has to be whole 32-pixel bursts: 320 wide any row is, 240 wide two),
// clamped to the frame, and merged with the span before it if they touch.
// Pure arithmetic, out here so the host test can hammer it: a span that
// misses a row by one is a stale line on the panel that nothing else would
// ever catch. Returns the span count; past `maxOut` the last span simply
// runs to the end, which sends more than needed rather than less.
struct RowSpan { int32_t r0, r1; };
inline int frameSpans(const bool* changed, int32_t h, int32_t align, RowSpan* out, int maxOut) {
    int n = 0;
    for (int32_t r = 0; r < h;) {
        if (!changed[r]) { r++; continue; }
        int32_t r0 = r;
        while (r < h && changed[r]) r++;
        int32_t r1 = r;
        r0 -= r0 % align;
        if (r1 % align) r1 += align - (r1 % align);
        if (r1 > h) r1 = h;
        if (n > 0 && out[n - 1].r1 >= r0) {
            if (r1 > out[n - 1].r1) out[n - 1].r1 = r1;
        } else if (n >= maxOut) {
            out[n - 1].r1 = h;
            return n;
        } else {
            out[n].r0 = r0; out[n].r1 = r1; n++;
        }
        r = r1;
    }
    return n;
}

// Builds the lookup table. Once, in setup(), any time after the display is
// up. Returns false on a board this is not built for, and push() then
// declines every frame and the ordinary push carries on.
bool begin();

// True once begin() has succeeded: this build can do it at all.
bool available();

// The switch. Separate from available(), so a board that draws strangely can
// be put back on the ordinary push over the console without reflashing.
void setEnabled(bool on);
bool enabled();

// Forget what the panel shows, so the next push sends every row. Call it
// after anything that could have drawn on the panel without going through
// push(): a rotation, a fall-back to the ordinary push.
void invalidate();

// Rows actually sent, and where the time went, since the last newFrame().
// Accumulated rather than replaced: the 3.5" pushes twice a frame, and a
// figure that only ever showed the second band made a 150-row frame read as
// 74 and the push look twice as slow per row as it is.
void    newFrame();
int32_t lastRows();
uint32_t hashUs();
uint32_t wireUs();

// Ships `h` rows of `w` 8-bit pixels from `src` to the panel at x,y.
//
// It takes the buffer rather than the sprite on purpose. A sprite's
// width()/height() report the VIEWPORT when one is set with a datum, not the
// buffer, and the 3.5" pushes its main screen through a full-screen viewport
// laid over a half-height buffer -- asking the sprite gave twice the rows
// that exist and read straight off the end of it. The caller knows the real
// size (FastSprite::bufW()/bufH()) and passes it.
//
// Returns false if it declined -- wrong board, switched off, no buffer (a
// rotate can lose it), a size that does not divide into the hardware's
// 64-byte bursts -- and the caller must then push it the ordinary way. Never
// partially draws: it either does the whole thing or touches nothing.
bool push(TFT_eSPI& tft, const uint8_t* src, int32_t w, int32_t h, int32_t x, int32_t y);

}  // namespace FramePush
