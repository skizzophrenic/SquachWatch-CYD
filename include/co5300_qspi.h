// SquachWatch-CYD — the Waveshare ESP32-S3-Touch-AMOLED-2.06's panel: a
// CO5300 AMOLED, 410x502, on a four-lane QSPI bus.
//
// Not a TFT_eSPI driver, the same arrangement as the Freenove 3.5"'s ST77922
// (st77922_qspi.h): this module owns the bus and the panel, Co5300TFT
// (co5300_display.h) uses it for the few things that draw straight at the
// panel, and FramePush's QSPI route ships the frame a chunk at a time by DMA.
//
// The watch UI is the T-Watch S3's, 240x240. It is shown SCALED: every three
// logical pixels become five panel pixels, so the 240x240 frame fills a
// 400x400 square centred on the glass (OFF_X, OFF_Y), with black AMOLED --
// which is off -- above and below it. edge() is the one mapping from a
// logical edge to a panel offset, and the push, the direct draws and the
// touch ranges in main.cpp all go through it, so a tap lands where it was
// drawn.
//
// The wire protocol is the SH8601 family's, which the CO5300 shares (the
// board's own BSP drives it with esp_lcd_sh8601): a register write is command
// 0x02 on one lane with the register in the middle byte of a 24-bit address,
// then its bytes on one lane; pixels are command 0x32 with 0x2C (memory write)
// or 0x3C (continue) in that address, then the data on all four lanes, 16 bits
// a pixel, high byte first. The glass's first visible column is controller
// column 22 (COL_GAP), and windows must start on an even column and row and
// be an even number wide and tall.
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace Co5300 {

static const int16_t PANEL_W = 410;   // the visible glass, portrait
static const int16_t PANEL_H = 502;
static const int16_t COL_GAP = 22;    // controller column of panel column 0

// The scaled watch frame: LOG x LOG logical pixels as OUT x OUT panel pixels.
static const int16_t LOG   = 240;
static const int16_t OUT   = 400;
static const int16_t OFF_X = 4;       // even, so every window starts even
static const int16_t OFF_Y = 50;
// A logical edge (0..LOG) to its panel offset inside the square (0..OUT).
// Logical pixel v covers panel offsets [edge(v), edge(v + 1)): two, two, one.
inline int32_t edge(int32_t v) { return (v * 5 + 2) / 3; }
// The logical pixel a panel offset inside the square shows (0..OUT-1).
inline int32_t logicalOf(int32_t o) { return (o * 3) / 5; }

// Pins, from the board's BSP (esp32_s3_touch_amoled_2_06.h). Not the S3's
// FSPI IO_MUX pins, so the bus goes through the GPIO matrix.
static const int IO_SCLK = 11, IO_D0 = 4, IO_D1 = 5, IO_D2 = 6, IO_D3 = 7;
static const int IO_CS   = 12;
static const int IO_RST  = 8;

// The largest single transfer, in pixels: under the driver's 64 KB DMA cap.
static const size_t MAX_PIXELS = 0x4000;

// The bus, the reset pulse and the initialisation sequence; the whole glass
// is cleared to black and the brightness left at 0 (applyBrightness() in
// main.cpp sets it). False if the SPI driver would not start, in which case
// every call below does nothing and the screen stays dark. The AXP2101's
// ALDO rails feed the panel, so this runs after the power chip is up.
bool begin();
bool ready();

// A register write: `reg`, then `len` bytes (0 for none). Synchronous.
void writeReg(uint8_t reg, const uint8_t* data, size_t len);

// The write window, in PANEL pixels (0..PANEL_W-1), inclusive corners. The
// column gap is added here.
void setWindow(int16_t x0, int16_t y0, int16_t x1, int16_t y1);

// `count` pixels in wire order from a DMA-capable buffer, into the window.
// `first` starts the write at the window's origin. Queued: the next call, or
// wait(), blocks until it has gone. At most one is ever in flight.
bool queuePixels(const uint16_t* dma, size_t count, bool first);
void wait();

// A solid rectangle in PANEL pixels, synchronously, widened to even edges.
void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t rgb565);

// Display inversion on or off: 0x21 / 0x20.
void invert(bool on);
// The AMOLED's brightness, 0..255: register 0x51. There is no backlight.
void brightness(uint8_t level);

// One RGB565 colour as the panel wants it in memory: high byte first.
inline uint16_t wire(uint16_t rgb565) { return (uint16_t)((rgb565 << 8) | (rgb565 >> 8)); }

}  // namespace Co5300
