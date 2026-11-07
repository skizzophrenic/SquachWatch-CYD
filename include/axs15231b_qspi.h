// SquachWatch-CYD — the DIYmalls JC3248W535's panel: an AXS15231B,
// 320x480, on a four-lane QSPI bus.
//
// Same architecture as st77922_qspi.h: NOT a TFT_eSPI driver. This module
// owns the bus and the panel, and two callers use it: Axs15231bTFT
// (axs15231b_display.h) for the few things that draw straight at the panel,
// and FramePush's QSPI route (frame_push.cpp), which ships each frame a
// chunk at a time by DMA from the push task.
//
// The wire protocol follows the same MIPI DBI Type C conventions as the
// ST77922: a register write is command 0x02 on one lane with the register
// in the top byte of a 24-bit address, then its bytes on one lane; pixels
// are command 0x32 with 0x2C (memory write) or 0x3C (continue) in that
// address, then the data on all four lanes, 16 bits a pixel, high byte
// first. Unlike the ST77922 the AXS15231B's MADCTL has an MV bit, but
// SquachWatch addresses it in portrait (320 wide, 480 tall) anyway, for
// consistency with the rest of the QSPI path.
//
// Only ever called from one task at a time: the push task owns the bus while
// a frame goes out, and the direct draws take FramePush::busLock() first.
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace Axs15231b {

static const int16_t PANEL_W = 320;   // the panel's own frame, portrait
static const int16_t PANEL_H = 480;

// Pins, from the JC3248W535 schematic / reference driver.
// These are NOT the FSPI IO_MUX pins — they need the GPIO matrix, which
// limits practical QSPI speed to ~40 MHz on this board (vs 80 MHz for the
// Freenove S3 3.5" whose ST77922 lands on the IO_MUX pins).
static const int IO_SCLK = 47, IO_D0 = 21, IO_D1 = 48, IO_D2 = 40, IO_D3 = 39;
static const int IO_CS   = 45;
static const int IO_BL   = 1;        // HIGH is on

// The largest single transfer, in pixels: under the driver's 64 KB DMA cap.
static const size_t MAX_PIXELS = 0x4000;

// The bus and the panel's initialisation sequence. The backlight is left to
// main.cpp's PWM. False if the SPI driver would not start, in which case
// every call below does nothing and the screen stays dark.
bool begin();
bool ready();

// The initialisation sequence again, in case a reset pulse wiped it.
void reinit();

// A register write: `reg`, then `len` bytes (0 for none).
void writeReg(uint8_t reg, const uint8_t* data, size_t len);

// The write window, in PANEL pixels, inclusive corners.
void setWindow(int16_t x0, int16_t y0, int16_t x1, int16_t y1);

// `count` pixels in wire order (high byte first in memory) from a
// DMA-capable buffer, into the window. `first` starts the write at the
// window's origin; false carries on from where the last one stopped. The
// transfer is queued and this returns at once, so the caller can convert
// the next chunk while this one is on the wire; the next call, or wait(),
// blocks until it has gone. At most one is ever in flight, so a buffer
// handed in here is free again once the call after it returns.
bool queuePixels(const uint16_t* dma, size_t count, bool first);
void wait();

// A solid rectangle in PANEL pixels, synchronously: the direct draws.
void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t rgb565);

// Display inversion on or off: the panel's 0x21 / 0x20.
void invert(bool on);

// One RGB565 colour as the panel wants it in memory: high byte first.
inline uint16_t wire(uint16_t rgb565) { return (uint16_t)((rgb565 << 8) | (rgb565 >> 8)); }

}  // namespace Axs15231b
