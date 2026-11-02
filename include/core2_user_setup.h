// SquachWatch-CYD -- TFT_eSPI setup for the M5Stack Core2 (v1.1 and later).
//
// Pulled in via -include in platformio.ini's [env:core2], ahead of TFT_eSPI's
// own User_Setup, so nothing in the library tree is edited.
//
// Not a CYD: an ESP32-D0WDQ6-V3 with a 320x240 ILI9342C (some later units an
// ILI9342E) on VSPI -- MOSI 23, MISO 38, SCLK 18, CS 5, DC 15. There is no
// reset pin and no backlight pin: both belong to the power chip (an AXP192 on
// the first Core2, an AXP2101 from v1.1), so TFT_RST and TFT_BL are left
// undefined here and core2_power.cpp pulses the reset and dims the light.
// The microSD slot is on the SAME bus (CS 4); sd_log.cpp hands SD this
// library's own SPI instance for that reason.
//
// The ILI9342C is the ILI9341 with its scan turned a quarter: its native frame
// is 320x240 landscape. TFT_eSPI knows this as M5STACK, which swaps the
// rotation table so that, with TFT_WIDTH 240 / TFT_HEIGHT 320 as on every
// other board in this project, rotation 0 is portrait and rotation 1 is
// landscape, upright with the USB-C port at the bottom. main.cpp's
// madctlRotationBits() carries the same table for CORE2.
#pragma once

#define USER_SETUP_INFO    "SquachWatch-CYD / M5Stack Core2 / ILI9342C"

#define ILI9341_DRIVER
#define M5STACK
#define TFT_WIDTH   240
#define TFT_HEIGHT  320

#define TFT_MISO  38
#define TFT_MOSI  23
#define TFT_SCLK  18
#define TFT_CS     5
#define TFT_DC    15
#define TFT_RST   -1   // on the power chip; see core2_power.cpp

// The same fonts every other build loads, and no more.
#define LOAD_GLCD
#define LOAD_FONT2

// 40 MHz writes, as M5GFX runs this panel. The microSD shares the bus but
// sets its own clock per transaction.
#ifndef SPI_FREQUENCY
#define SPI_FREQUENCY         40000000
#endif
#define SPI_READ_FREQUENCY    16000000

// M5GFX sets invert on for this panel; main.cpp's PANEL_NEEDS_INVERSION is the
// control point (see the note there), this is kept for reference.
#define TFT_INVERSION_ON
// BGR order, as for every ILI9341-family panel here.
#define TFT_RGB_ORDER TFT_BGR
