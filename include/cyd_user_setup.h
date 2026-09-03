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

// SPI frequencies
#define SPI_FREQUENCY         40000000
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
