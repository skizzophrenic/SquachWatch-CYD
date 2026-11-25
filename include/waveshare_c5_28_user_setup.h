// TFT_eSPI setup for the Waveshare ESP32-C5-Touch-LCD-2.8: an ESP32-C5 with a
// 2.8" 240x320 ST7789 on SPI, the SD card on the same bus, and capacitive
// touch (CST3530) on I2C -- so no TOUCH_CS here, and the XPT2046 path is never
// compiled in. Pins from Waveshare's pin table and its 06_lvgl_demo
// (Display_ST7789.h):
//   SCLK 6, MOSI 7, MISO 8 (the card's; the panel only listens), DC 9, CS 10.
//   SD card chip select 23 -- see sd_log.cpp.
// Reset and backlight are NOT GPIOs on this board: both belong to the CH32V003
// helper chip (see waveshare_c5_board.h), so TFT_RST is -1 and TFT_BL is left
// undefined, and main.cpp resets the panel and drives the backlight through
// the helper.
#define USER_SETUP_INFO "SquachWatch-CYD / Waveshare ESP32-C5-Touch-LCD-2.8 / ST7789"

#define ST7789_DRIVER
#define TFT_WIDTH   240
#define TFT_HEIGHT  320
// Waveshare's init sends INVON (0x21) and MADCTL 0x00 -- RGB order. The
// inversion that actually reaches the panel is main.cpp's
// PANEL_NEEDS_INVERSION; this line is for the library's own init.
#define TFT_INVERSION_ON
#define TFT_RGB_ORDER TFT_RGB

#define TFT_MISO  8
#define TFT_MOSI  7
#define TFT_SCLK  6
#define TFT_CS    10
#define TFT_DC    9
#define TFT_RST   -1

#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF
#define SMOOTH_FONT

// As on the NM-CYD-C5, this number reaches nothing on the frame push path
// (frame_push.cpp drives the bus itself; see nmcydc5_user_setup.h).
#ifndef SPI_FREQUENCY
#define SPI_FREQUENCY  20000000
#endif
#define SPI_READ_FREQUENCY  20000000
