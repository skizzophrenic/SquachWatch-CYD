// SquachWatch-CM0 -- TFT_eSPI user setup for the M5Stack Cardputer Zero.
//
// Raspberry Pi CM0 (Cortex-A53), the 3.2" 320x170 ST7789 panel, no touch,
// the 46-key TCA8418 keyboard on I2C, an ES8389 codec and an SD slot.
// The panel is driven by the OS (fb0); this app draws into the TFT_eSPI
// framebuffer the way every board does and the display harness
// (main_linux.cpp) blits it to an SDL window -- so the SPI pins are
// absent here, the way the firmware's sim boards have them too.
//
// Geometry is the one thing this header really carries: 320x170, no
// CGRAM offset (a full-glass panel, unlike the 135x240 ADV glass).
#pragma once

#define USER_SETUP_INFO    "SquachWatch-CM0 / M5Stack Cardputer Zero / ST7789 320x170"
#define ST7789_DRIVER
#define TFT_WIDTH   320
#define TFT_HEIGHT  170

#define TFT_MISO  -1
#define TFT_MOSI  -1
#define TFT_SCLK  -1
#define TFT_CS    -1
#define TFT_DC    -1
#define TFT_RST   -1

#define TFT_INVERSION_ON

#ifndef SPI_FREQUENCY
#define SPI_FREQUENCY         40000000
#endif
#define SPI_READ_FREQUENCY    16000000

#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define SMOOTH_FONT
