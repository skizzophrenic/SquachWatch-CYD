// SquachWatch-CYD — TFT_eSPI user setup
// Some ESP32-2432S028R ("CYD") batches — including the AITRIP-branded
// board this repo was field-tested against — ship an ST7789 panel
// instead of ILI9341, same 2.8"/240x320 glass and pinout. Wrong driver
// here produces a solid white screen (panel never leaves its reset/idle
// state because the init command sequence doesn't match the controller).
// If yours is the older ILI9341 variant, swap ST7789_DRIVER back to
// ILI9341_DRIVER and TFT_RGB_ORDER back to TFT_RGB.
#pragma once

#define USER_SETUP_INFO    "SquachWatch-CYD / 2.8 inch / ST7789"
#define ST7789_DRIVER
// Portrait native. We rotate to landscape at runtime via setRotation(1).
#define TFT_WIDTH   240
#define TFT_HEIGHT  320
#define TFT_ROTATION 1

// SPI pins (canonical 2.8" CYD pinout, multiple sources)
#define TFT_MISO  12
#define TFT_MOSI  13
#define TFT_SCLK  14
#define TFT_CS    15
#define TFT_DC     2
#define TFT_RST   -1   // NO software reset — rely on power-on reset.
                       // Setting RST to a wrong GPIO can hold the panel in reset
                       // and produce a solid white screen.
#define TFT_BL    21

// Touch is on its own dedicated SPI bus, not this one — see the
// TOUCH_* pin defines in main.cpp.

// Backlight
#define TFT_BACKLIGHT_ON   1
#define PWM_FREQ           5000
#define PWM_MAX_DUTY       255

// SPI frequencies.
//
// Guarded rather than hard-defined so a build can override it from the
// command line: [env:cyd-fast] in platformio.ini passes
// -DSPI_FREQUENCY=80000000 and everything else about that build is
// identical to [env:cyd]. That is why there is no duplicate copy of this
// header for the overclocked variant -- and, usefully, a -D flag change
// forces a rebuild, where editing this file does not (it arrives via
// -include, which PlatformIO's dependency scanner cannot see).
//
// 80MHz is an overclock, not a spec-compliant setting: the ST7789's
// rated write cycle is ~16ns, about 62.5MHz. It is widely used on these
// boards but board-dependent -- trace quality varies unit to unit, and
// the failure modes (white screen, torn lines, colour corruption) look
// identical to the wrong-driver problem this board already has a
// support burden around. The ESP32 SPI divider snaps to 80/40/26.7/20,
// so there is no intermediate value to retreat to: it is 40 or 80.
//
// Measured on real hardware. At 40MHz: push 38ms, frame 44.4ms (22fps).
// At 80MHz: push 22.6ms, frame 29ms (34fps). Only ~30.7ms of the 40MHz
// push is the bus itself (153,600 bytes at 16bpp); the remaining ~7.3ms
// is the 8bpp->RGB565 palette conversion, which is CPU-bound and does
// not scale with this clock -- which is why 80MHz gives ~1.7x, not 2x.
#ifndef SPI_FREQUENCY
#define SPI_FREQUENCY         40000000
#endif
#define SPI_READ_FREQUENCY    20000000

// Fonts. Only the built-in GLCD font (font 1) is loaded: nothing in
// this project ever calls setTextFont(), setFreeFont() or loadFont(),
// and there are no .vlw assets -- every screen uses font 1 via
// setTextSize(), and the one custom typeface (Bangers, on the ALERT
// and CLEAR headings) ships as its own glyph tables in
// include/bangers_font.h rather than through TFT_eSPI at all.
// LOAD_FONT2/4/6/7/8 and SMOOTH_FONT were ~11KB of glyph data plus the
// .vlw renderer that nothing could reach.
#define LOAD_GLCD

// This ST7789 panel is normal (non-inverted) polarity; BGR order.
#define TFT_INVERSION_OFF
#define TFT_RGB_ORDER TFT_BGR
