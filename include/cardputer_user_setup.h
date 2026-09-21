#pragma once
// Original Cardputer LCD, per M5Stack K132 pin map. TFT_eSPI applies the
// ST7789 135x240 panel offsets via CGRAM_OFFSET; landscape is rotation 1.
#define USER_SETUP_INFO "SquachWatch / original Cardputer"
#define ST7789_DRIVER
#define TFT_WIDTH 135
#define TFT_HEIGHT 240
#define CGRAM_OFFSET
#define TFT_MISO -1
#define TFT_MOSI 35
#define TFT_SCLK 36
#define TFT_CS 37
#define TFT_DC 34
#define TFT_RST 33
#define TFT_BL 38
#define TFT_BACKLIGHT_ON HIGH
#define TFT_RGB_ORDER TFT_RGB
#define TFT_INVERSION_ON
// Display on SPI2 (FSPI); SD uses its own SPI3 (HSPI) instance.
#define SPI_FREQUENCY 40000000
#define SPI_READ_FREQUENCY 16000000
#define LOAD_GLCD
#define LOAD_FONT2
