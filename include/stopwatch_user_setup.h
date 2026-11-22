// TFT_eSPI is only the application's drawing/sprite interface on StopWatch.
// M5GFX owns CO5300 QSPI and CST820 touch; never initialize TFT_eSPI's bus.
#pragma once
#define USER_SETUP_INFO "SquachWatch / M5Stack StopWatch / CO5300"
#define ST7789_DRIVER
#define TFT_WIDTH 466
#define TFT_HEIGHT 466
#define TFT_MISO -1
#define TFT_MOSI -1
#define TFT_SCLK -1
#define TFT_CS -1
#define TFT_DC -1
#define TFT_RST -1
#define SPI_FREQUENCY 40000000
#define LOAD_GLCD
#define LOAD_FONT2
