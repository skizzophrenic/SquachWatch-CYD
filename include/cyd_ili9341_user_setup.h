// SquachWatch-CYD — TFT_eSPI user setup (ILI9341 variant)
// Some ESP32-2432S028R ("CYD") batches ship the older ILI9341 panel
// instead of ST7789 -- same 2.8"/240x320 glass and pinout, different
// controller. Wrong driver produces a solid white screen (panel never
// leaves its reset/idle state because the init command sequence
// doesn't match the controller), which is exactly why this exists as
// its own build instead of a single guess: see cyd_user_setup.h (the
// ST7789 variant, the more commonly reported one) for that side of it.
// Identical to that file except the two lines called out below.
#pragma once

#define USER_SETUP_INFO    "SquachWatch-CYD / 2.8 inch / ILI9341"
#define ILI9341_DRIVER
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

// ILI9341 is normal (non-inverted) polarity, RGB order -- the two
// lines that actually differ from cyd_user_setup.h's ST7789 config.
#define TFT_INVERSION_OFF
#define TFT_RGB_ORDER TFT_RGB
