// SquachWatch-CYD -- TFT_eSPI setup for the M5Stack Tough.
//
// Pulled in via -include in platformio.ini's [env:m5tough], ahead of
// TFT_eSPI's own User_Setup, so nothing in the library tree is edited.
//
// An original ESP32 (D0WDQ6-V3, 16 MB flash, 8 MB PSRAM -- the PSRAM is left
// alone, see the env's comment) behind a 2.0" 320x240 ILI9342C. Pins from
// M5GFX's own autodetect for the board (M5GFX.cpp, board_M5Tough):
//
//   MOSI 23, MISO 38, SCLK 18, CS 5, DC 15 -- and the SD card on the SAME
//   three bus pins with its own chip select, GPIO4 (sd_log.cpp).
//
// What is NOT a GPIO here, and so is not in this file:
//   - RESET: AXP192 GPIO4 (shared with nothing else). M5Tough::powerUp()
//     pulses it before tft.init(); TFT_eSPI then only sees "no reset pin"
//     and does its software reset.
//   - BACKLIGHT: AXP192 LDO3, a voltage, not a PWM pin. TFT_BL must stay
//     undefined or TFT_eSPI will drive a GPIO that is something else.
//   - TOUCH: a CHSC6540 on I2C (SDA 21, SCL 22, 0x2E, INT 39), not on this bus.
//
// The ILI9342C is the ILI9341's 320x240-native cousin. TFT_eSPI drives it with
// the ILI9341 driver plus the M5STACK flag, which swaps in a rotation table
// that makes rotations 0..3 look like a stock ILI9341's (portrait/landscape in
// the same order), so the app's rotation and touch maths carry over unchanged.
// The panel needs inversion ON (M5GFX: invert = true); that is applied by
// main.cpp's PANEL_NEEDS_INVERSION, which is the control point, not a define
// here (see the long comment above it).
#pragma once

#define USER_SETUP_INFO    "SquachWatch-CYD / M5Stack Tough / ILI9342C"

#define ILI9341_DRIVER
#define M5STACK
#define TFT_WIDTH   240
#define TFT_HEIGHT  320

#define TFT_MISO  38
#define TFT_MOSI  23
#define TFT_SCLK  18
#define TFT_CS     5
#define TFT_DC    15
#define TFT_RST   -1   // AXP192 GPIO4, pulsed by M5Tough::powerUp()
// No TFT_BL: LDO3 on the AXP192 lights the panel (M5Tough::backlight()).

// The same fonts every other CYD build loads, and no more: flash is tight.
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define SMOOTH_FONT

// M5GFX runs this panel at 40 MHz write / 16 MHz read, and so does the
// original CYD's ILI9341. A -DSPI_FREQUENCY=... build flag wins.
#ifndef SPI_FREQUENCY
#define SPI_FREQUENCY         40000000
#endif
#define SPI_READ_FREQUENCY    16000000
