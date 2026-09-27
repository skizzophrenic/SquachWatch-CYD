// SquachWatch-CYD -- TFT_eSPI user setup for the LilyGo T-Watch S3.
//
// ESP32-S3 with 16 MB flash and 8 MB OPI PSRAM, a 1.54" 240x240 ST7789 on
// its own SPI, an FT6336 capacitive touch controller on I2C (SDA 39, SCL 40,
// INT 16), and an AXP2101 power chip on a second I2C (SDA 10, SCL 11) that
// has to be told to power the backlight and the touch chip before either
// answers -- see twatchPowerUp() in main.cpp. Pins from LilyGo's
// utilities.h and their own Setup212 for TFT_eSPI.
#pragma once

#define USER_SETUP_INFO    "SquachWatch-CYD / T-Watch S3 / ST7789 240x240"
#define ST7789_DRIVER
#define TFT_WIDTH   240
#define TFT_HEIGHT  240
// One pixel on this glass, in micrometres: 1.54" 240x240, active area
// 27.72 x 27.72 mm on the ST7789 module sheet, 0.1155 mm a pixel. From the
// sheet, not measured. Theme::computeButtonBar caps what it asks of a screen
// this small; the arithmetic there says what a bar comes to on the wrist.
#define SQW_PIXEL_PITCH_UM 116

#define TFT_MISO  -1
#define TFT_MOSI  13
#define TFT_SCLK  18
#define TFT_CS    12
#define TFT_DC    38
#define TFT_RST   -1   // no reset line; the panel comes up with power
// GPIO45 is the backlight. TFT_eSPI is not given it: main.cpp runs the
// LEDC dimmer on it the way it does on every other board.

#define TFT_INVERSION_ON     // the panel wants it, per LilyGo's setup

#ifndef SPI_FREQUENCY
#define SPI_FREQUENCY         40000000
#endif
#define SPI_READ_FREQUENCY    20000000

#define LOAD_GLCD
#define LOAD_FONT2
