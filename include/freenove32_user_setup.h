// SquachWatch-CYD — TFT_eSPI setup for the Freenove 3.2" CYD (FNK0103L /
// FNK0114L, sold as "Freenove Bitcoin Miner ... 3.2 inch ST7789").
//
// Pulled in via -include in platformio.ini's [env:freenove32], ahead of
// TFT_eSPI's own User_Setup, so nothing in the library tree is edited.
//
// Pins from Freenove's own TFT_eSPI setup for this board
// (Libraries/FNK0114L_3.2inch_ST7789/TFT_eSPI_Setups_v1.4.zip,
// FNK0114L_3.2_240x320_ST7789.h). They are the RL Phantom resistive's pins
// exactly -- display on HSPI 12/13/14, CS 15, DC 2, backlight 27, and the
// XPT2046 resistive touch on the SAME bus with its chip select on 33 -- so
// the build reuses that board's shared-bus touch path (RLPHANTOM_R). Only
// the panel differs: an ST7789, colour order BGR, and inversion ON (set in
// main.cpp's PANEL_NEEDS_INVERSION, which is the control point; the define
// below is what Freenove ship, kept for reference).
//
// The Amazon listing says "capacitive"; the board's own datasheet folder
// has the XPT2046 and a resistive touch panel drawing, and ours reports no
// capacitive chip at boot. Resistive.
#pragma once

#define USER_SETUP_INFO    "SquachWatch-CYD / Freenove 3.2 inch resistive / ST7789"

#define ST7789_DRIVER
#define TFT_WIDTH   240
#define TFT_HEIGHT  320
// One pixel on this glass, in micrometres: 3.2" 240x320, active area
// 48.6 x 64.8 mm on the module datasheet, 0.2025 mm a pixel. From the sheet,
// not the bench -- nobody here has held this board; Theme::computeButtonBar
// reads it, so a measured figure should replace it.
#define SQW_PIXEL_PITCH_UM 203

#define USE_HSPI_PORT
#define TFT_MISO  12
#define TFT_MOSI  13
#define TFT_SCLK  14
#define TFT_CS    15
#define TFT_DC     2
#define TFT_RST   -1   // NO software reset — rely on power-on reset.
#define TFT_BL    27
#define TFT_BACKLIGHT_ON HIGH

#define TOUCH_CS  33

// The same fonts every other CYD build loads, and no more: flash is tight.
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define SMOOTH_FONT

// Freenove run this panel at 80 MHz; so does the 2.8" ST7789 (cyd-fast).
#define SPI_FREQUENCY         80000000
#define SPI_READ_FREQUENCY    20000000
#define SPI_TOUCH_FREQUENCY    2500000

#define TFT_INVERSION_ON
#define TFT_RGB_ORDER TFT_BGR
