// SquachWatch-CYD — TFT_eSPI setup for the Freenove ESP32-S3 Display 2.8"
// (FNK0104B, capacitive touch; FNK0104A is the same board without touch).
//
// Pulled in via -include in platformio.ini's [env:freenove-s3], ahead of
// TFT_eSPI's own User_Setup, so nothing in the library tree is edited.
//
// Not a CYD, though the panel is a CYD's: an ESP32-S3R8 (8 MB octal PSRAM in
// the package, 16 MB quad flash beside it), native USB on the USB-C, and a
// 240x320 ILI9341 on SPI. Pins from Freenove's own TFT_eSPI setup for this
// board (Libraries/FNK0104AB/TFT_eSPI_Setups_v1.3.zip,
// FNK0104AB_2.8_240x320_ILI9341.h), checked against the 2.8" schematic:
// display on 11/12/13, CS 10, DC 46, backlight 45 through a BSS138 (HIGH is
// on), and no reset line. Freenove pick the alternative ILI9341 init
// (ILI9341_2_DRIVER), BGR, inversion ON -- inversion is really set by
// main.cpp's PANEL_NEEDS_INVERSION, which is the control point; the define
// below is what Freenove ship, kept for reference.
//
// Touch is not on this bus: an FT6336 on I2C (SDA 16, SCL 15, reset 18,
// 0x38), shared with the ES8311 audio codec at 0x18. See setup().
#pragma once

#define USER_SETUP_INFO    "SquachWatch-CYD / Freenove ESP32-S3 2.8 inch / ILI9341"

#define ILI9341_2_DRIVER
#define TFT_WIDTH   240
#define TFT_HEIGHT  320

#define USE_HSPI_PORT
#define TFT_MISO  13
#define TFT_MOSI  11
#define TFT_SCLK  12
#define TFT_CS    10
#define TFT_DC    46
#define TFT_RST   -1   // not wired; the panel comes up with power
#define TFT_BL    45
#define TFT_BACKLIGHT_ON HIGH

// The same fonts every other CYD build loads, and no more.
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define SMOOTH_FONT

// Freenove's own rate for this panel. The CYD's ILI9341 does not hold 80 MHz
// either (cyd-ili9341 has no -fast build), so this is not pushed until it is
// measured on a board.
#define SPI_FREQUENCY         40000000
#define SPI_READ_FREQUENCY    20000000

#define TFT_INVERSION_ON
#define TFT_RGB_ORDER TFT_BGR
