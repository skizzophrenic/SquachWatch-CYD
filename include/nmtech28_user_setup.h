// SquachWatch-CYD — TFT_eSPI setup for the NMTech Display 2.8 (ESP32-S3).
//
// Pulled in via -include in platformio.ini's [env:nmtech28], ahead of
// TFT_eSPI's own User_Setup, so nothing in the library tree is edited.
//
// An ESP32-S3 (QFN56, 8 MB octal PSRAM in the package, 16 MB quad flash),
// native USB on the USB-C, and a 240x320 ST7789 on SPI. No schematic was
// to be had; the pins were read off the board itself (2026-10-20): the GPIO
// matrix of the running stock firmware, then its decompiled display driver
// (pins 3/-1/-1/6 for DC/RST/CS/BL, SPI mode 3, 80 MHz, the init table).
//
// Display on SCK 5 / MOSI 1, DC 3, no MISO and NO CHIP SELECT: CS is tied
// low on the board, which is why the panel wants SPI mode 3 (TFT_eSPI picks
// that itself for an ST7789) and why it never resynchronises after a stray
// clock edge -- only a hardware reset does that. The reset line is not a
// GPIO either: it is P1 of the TCA9554 expander at 0x20 on the I2C bus
// (SDA 8, SCL 7), pulsed in nmtechPanelReset() in main.cpp. Backlight on
// GPIO6, HIGH is on, PWM-able.
//
// Touch is not on this bus: an FT6336U on the same I2C at 0x38, no reset
// line of its own. Also on that bus: the AXP2101 (0x34, the battery and
// every rail -- see axpBegin()), the ES8311 codec (0x18, sound.cpp), a
// QMI8658 (0x6B) and a PCF85063 RTC (0x51), those two not used.
#pragma once

#define USER_SETUP_INFO    "SquachWatch-CYD / NMTech Display 2.8 / ST7789"

#define ST7789_DRIVER
#define TFT_WIDTH   240
#define TFT_HEIGHT  320

#define TFT_MISO  -1
#define TFT_MOSI   1
#define TFT_SCLK   5
#define TFT_CS    -1   // tied low on the board
#define TFT_DC     3
#define TFT_RST   -1   // on the expander, see above
#define TFT_BL     6
#define TFT_BACKLIGHT_ON HIGH

// The same fonts every other CYD build loads, and no more.
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define SMOOTH_FONT

// The stock firmware runs it at 80 MHz. If a panel ever shows torn or
// speckled rows, build with -DSPI_FREQUENCY=40000000.
#ifndef SPI_FREQUENCY
#define SPI_FREQUENCY         80000000
#endif
#define SPI_READ_FREQUENCY    20000000

// The stock init sends 0x21 (inversion on) and MADCTL with the BGR bit set;
// confirmed by eye on the bench. Inversion is really set by main.cpp's
// PANEL_NEEDS_INVERSION; the define is kept for reference.
#define TFT_INVERSION_ON
#define TFT_RGB_ORDER TFT_BGR
