// Experimental Freenove FNK0103L / FNK0114L 3.2-inch ST7789 IPS.
// Wiring and polarity: Freenove_ESP32_Display, Libraries/
// FNK0114L_3.2inch_ST7789/TFT_eSPI_Setups_v1.4.zip,
// TFT_eSPI_Setups/FNK0114L_3.2_240x320_ST7789.h.
#pragma once

#define USER_SETUP_INFO "SquachWatch / Freenove FNK0103L 3.2 inch / ST7789"
#define ST7789_DRIVER
#define TFT_WIDTH 240
#define TFT_HEIGHT 320
#define TFT_ROTATION 1

// Display and XPT2046 share HSPI. SD remains on Arduino's VSPI bus.
#define USE_HSPI_PORT
#define TFT_MISO 12
#define TFT_MOSI 13
#define TFT_SCLK 14
#define TFT_CS 15
#define TFT_DC 2
#define TFT_RST -1
#define TFT_BL 27
#define TFT_BACKLIGHT_ON HIGH
#define TOUCH_CS 33

// Start conservatively, below the vendor example's 80 MHz display clock.
#define SPI_FREQUENCY 40000000
#define SPI_READ_FREQUENCY 20000000
#define SPI_TOUCH_FREQUENCY 2500000
#define LOAD_GLCD
#define LOAD_FONT2
#define TFT_INVERSION_ON
#define TFT_RGB_ORDER TFT_BGR
