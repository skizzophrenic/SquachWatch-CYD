// SquachWatch-CYD — TFT_eSPI setup for the 2.4" CYD, ST7789 variant.
// Display and resistive touch wiring match the ESP32-2432S024R profile;
// only the panel controller and its colour setup differ.
#pragma once

#define USER_SETUP_INFO    "SquachWatch-CYD / 2.4 inch / ST7789 resistive"
#define ST7789_DRIVER

#define TFT_WIDTH   240
#define TFT_HEIGHT  320

// The 2.4" CYD uses the display's HSPI bus for both the TFT and XPT2046.
#define USE_HSPI_PORT
#define TFT_MISO  12
#define TFT_MOSI  13
#define TFT_SCLK  14
#define TFT_CS    15
#define TFT_DC     2
#define TFT_RST   -1
#define TFT_BL    27
#define TFT_BACKLIGHT_ON HIGH
#define TOUCH_CS  33

#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define SMOOTH_FONT

#ifndef SPI_FREQUENCY
#define SPI_FREQUENCY         40000000
#endif
#define SPI_READ_FREQUENCY    20000000
#define SPI_TOUCH_FREQUENCY    2500000

#define TFT_INVERSION_OFF
#define TFT_RGB_ORDER TFT_BGR
