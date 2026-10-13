// SquachWatch-CYD — TFT_eSPI setup for the Waveshare ESP32-C5-Touch-LCD-3.5
// (ESP32-C5-WROOM-1U-N32R8, 320x480 ST7796 on SPI, FT6336 capacitive touch).
//
// Pulled in via -include in platformio.ini's [env:waveshare-c5-touch-lcd-35], ahead of
// TFT_eSPI's own User_Setup, so nothing in the library tree is edited. The
// board's variant (variants/waveshare-c5-touch-lcd-35/pins_arduino.h) deliberately
// defines no display pins, so nothing later overrides these.
//
// Pins from Waveshare's BSP (esp32_c5_touch_lcd_3_5.h) and their Arduino LVGL
// demo: SCLK 6, MOSI 7, MISO 2 (the NM-CYD-C5's bus pins, as it happens), CS 8,
// DC 5. The microSD slot is on this same bus with CS 9 (see sd_log.cpp).
//
// What is NOT a GPIO here: the panel's RESET and BACKLIGHT, and the touch
// chip's reset, all hang off a CH32V003 I/O expander on I2C (0x24). They are
// driven by ws_c5_board.cpp before tft.init(), so TFT_RST is -1 and there is
// no TFT_BL. main.cpp's WAVESHARE_C5 branches never touch a backlight pin.
#pragma once

#define USER_SETUP_INFO    "SquachWatch-CYD / Waveshare ESP32-C5-Touch-LCD-3.5 / ST7796"

#define ST7796_DRIVER
#define TFT_WIDTH   320
#define TFT_HEIGHT  480

#define TFT_MISO  2    // shared with the SD card; the panel itself is write-only
#define TFT_MOSI  7
#define TFT_SCLK  6
#define TFT_CS    8
#define TFT_DC    5
#define TFT_RST   -1   // CH32V003 EXIO1 -- pulsed by WsC5::begin()
// No TFT_BL: CH32V003 PWM register, see WsC5::backlight().
// No TOUCH_CS: the FT6336 is on I2C, not this bus.

// The same fonts every other build loads, and no more.
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define SMOOTH_FONT

// As on the NM-CYD-C5, this number only reaches RockBase's C5 port through
// initDMA(), which SquachWatch never calls: the real clock is
// SQW_C5_DMA_HZ / SQW_C5_SPI_DIV in platformio.ini (frame_push.cpp).
#ifndef SPI_FREQUENCY
#define SPI_FREQUENCY         40000000
#endif
#define SPI_READ_FREQUENCY    20000000

// Waveshare's own init sends MADCTL 0x48 (mirror X + BGR) and INVON (0x21).
// TFT_eSPI's ST7796 rotation 0 is MX | colour order, so BGR here reproduces
// their 0x48 exactly. Inversion is decided by main.cpp's PANEL_NEEDS_INVERSION
// (true for this board), not by this header. If red and blue come out swapped,
// the colour check at first boot (ORDER) fixes it without a rebuild.
#define TFT_INVERSION_ON
#define TFT_RGB_ORDER TFT_BGR
