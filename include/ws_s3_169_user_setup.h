// SquachWatch-CYD -- TFT_eSPI user setup for the Waveshare ESP32-S3-Touch-LCD-1.69.
//
// ESP32-S3R8 (16 MB flash, 8 MB OPI PSRAM), a 1.69" 240x280 ST7789V2 on its
// own SPI, a CST816T capacitive touch controller on I2C (SDA 11, SCL 10,
// reset 13, interrupt 14, address 0x15), shared with a QMI8658 IMU and a
// PCF85063 RTC. Pins from Waveshare's own board_config/pin_config.h and their
// ESP-IDF ST7789 demo (02_ESP_IDF_ST7789_LVGL), which also sets the panel's
// 20-row gap and turns inversion on.
#pragma once

#define USER_SETUP_INFO    "SquachWatch-CYD / Waveshare ESP32-S3 1.69 / ST7789V2 240x280"
#define ST7789_DRIVER
// 240x280: TFT_eSPI's ST7789 driver applies the 20-row offset itself for this
// height (rowstart 20 in ST7789_Rotation.h), in every rotation.
#define TFT_WIDTH   240
#define TFT_HEIGHT  280

#define TFT_MISO  -1
#define TFT_MOSI   7
#define TFT_SCLK   6
#define TFT_CS     5
#define TFT_DC     4
#define TFT_RST    8
// GPIO15 is the backlight. TFT_eSPI is not given it: main.cpp runs the LEDC
// dimmer on it (BL_PIN_S3) the way it does on every other S3 board.

#define TFT_INVERSION_ON     // Waveshare's init: esp_lcd_panel_invert_color(true)

// Waveshare's demos drive it at 40 MHz. Leave headroom to try 80 with
// -DSPI_FREQUENCY=80000000 once a unit has been looked at.
#ifndef SPI_FREQUENCY
#define SPI_FREQUENCY         40000000
#endif
#define SPI_READ_FREQUENCY    20000000

#define LOAD_GLCD
#define LOAD_FONT2
