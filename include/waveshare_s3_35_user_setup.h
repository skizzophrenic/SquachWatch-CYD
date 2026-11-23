// SquachWatch-CYD — TFT_eSPI setup for the Waveshare ESP32-S3-Touch-LCD-3.5
// (SKU 30733) and the -C variant (30934, with case).
//
// Pulled in via -include in platformio.ini's [env:waveshare-s3-35].
//
// A 320x480 ST7796 on plain SPI, so this builds as a member of the Freenove
// S3 4.0" family (FREENOVE_S3 + FREENOVE_S3_40 are defined by the env), with
// the Waveshare-specific hardware behind WS_S3_35. Pins from Waveshare's own
// Arduino examples (github.com/waveshareteam/ESP32-S3-Touch-LCD-3.5) and the
// Rev 2.0 schematic:
//   MOSI 1, MISO 2, SCLK 5, DC 3, backlight 6 (HIGH is on)
//   no CS pin (the panel's CS is tied low) and no reset GPIO: reset is the
//   TCA9554 expander's EXIO1, pulsed by ws35PowerUp() in main.cpp BEFORE
//   tft.init().
// Colour order BGR and inversion ON, as Waveshare ship it (their BSP:
// LCD_RGB_ELEMENT_ORDER_BGR, invert_color(true), mirror X).
#pragma once

#define USER_SETUP_INFO    "SquachWatch-CYD / Waveshare ESP32-S3-Touch-LCD-3.5 / ST7796"

#define ST7796_DRIVER
#define TFT_WIDTH   320
#define TFT_HEIGHT  480

#define USE_HSPI_PORT
#define TFT_MISO  -1   // never read; leaving it off keeps GPIO2 free
#define TFT_MOSI  1
#define TFT_SCLK  5
#define TFT_CS    -1
#define TFT_DC    3
#define TFT_RST   -1   // EXIO1 on the I/O expander
#define TFT_BL    6
#define TFT_BACKLIGHT_ON HIGH

#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define SMOOTH_FONT

// 80 MHz, as requested (Waveshare ship 40). If rows tear or speckle, build with
// -DSPI_FREQUENCY=40000000 for the slower, safer clock.
#ifndef SPI_FREQUENCY
#define SPI_FREQUENCY         80000000
#endif
#define SPI_READ_FREQUENCY    20000000

#define TFT_INVERSION_ON
#define TFT_RGB_ORDER TFT_BGR
