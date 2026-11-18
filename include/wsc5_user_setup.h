// SquachWatch-CYD — TFT_eSPI user setup for the Waveshare
// ESP32-C5-Touch-LCD-3.5 (ST7796 320x480, FT6336 capacitive touch).
//
// Shares the display SPI bus (6/7/2) with the microSD card, exactly as the
// NM-CYD-C5 does. Unlike every resistive CYD here there is NO TOUCH_CS: touch
// is an FT6336 on I2C (SDA 27 / SCL 26), driven by ws_c5_touch.cpp, so
// TFT_eSPI's own resistive-touch path must not claim a pin.
//
// The panel's reset and backlight are NOT GPIOs -- they live on the 0x24 I/O
// expander (LCD reset EXIO1, backlight/power EXIO5), brought up once before
// tft.init() by ws_c5_expander.cpp. Hence TFT_RST -1 and no TFT_BL here.
//
// Hardware map per Waveshare's ESP32-C5-Touch-LCD-3.5 docs and on-device bring-up.
#pragma once

#define USER_SETUP_INFO    "SquachWatch-CYD / Waveshare C5 3.5 / ST7796 / FT6336"
#define ST7796_DRIVER

// Portrait native (320 wide x 480 tall glass); rotated to landscape at runtime
// via setRotation(), same as the other 3.5" ST7796 boards.
#define TFT_WIDTH   320
#define TFT_HEIGHT  480

// Display SPI bus. Same three bus pins as the NM-CYD-C5 (SCLK 6 / MOSI 7 /
// MISO 2); this board's own DC 5 and CS 8. Reset is on the expander, so -1.
#define TFT_MISO   2
#define TFT_MOSI   7
#define TFT_SCLK   6
#define TFT_CS     8
#define TFT_DC     5
#define TFT_RST   -1

// No TFT_BL: the backlight is the expander's PWM (EXIO5, reg 0x05), set through
// ws_c5_expander.cpp / applyBrightness(), not a GPIO LEDC channel.

#define LOAD_GLCD
#define LOAD_FONT2

// Colour inversion ON for this ST7796 panel (validated). As on every other
// board, the live control point is PANEL_NEEDS_INVERSION in main.cpp -- this
// define alone is overwritten by tft.invertDisplay() at boot. Kept because
// TFT_eSPI expects one of the two inversion defines to be present.
#define TFT_INVERSION_ON
#define TFT_RGB_ORDER TFT_BGR

// The write clock. As the NM-CYD-C5 setup documents at length, on the C5 the
// per-frame push (frame_push.cpp) drives the SPI registers itself and does not
// read SPI_FREQUENCY -- its clock is SQW_C5_DMA_HZ from platformio.ini. This
// value only governs TFT_eSPI's own init-time writes; 60 MHz is the panel's
// validated write-clock ceiling. Overridable from the env.
#ifndef SPI_FREQUENCY
#define SPI_FREQUENCY         60000000
#endif
#define SPI_READ_FREQUENCY    20000000
