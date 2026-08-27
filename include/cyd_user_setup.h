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

// Fonts
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define SMOOTH_FONT

// This ST7789 panel is normal (non-inverted) polarity; BGR order.
#define TFT_INVERSION_OFF
#define TFT_RGB_ORDER TFT_BGR
