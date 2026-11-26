// SquachWatch-CYD -- TFT_eSPI user setup for the LilyGo T-Deck Plus.
//
// ESP32-S3FN16R8 (16 MB flash, 8 MB OPI PSRAM) with a 2.8" 320x240 ST7789,
// a GT911 capacitive panel on I2C (SDA 18, SCL 8, INT 16) shared with the
// keyboard's helper chip at 0x55, an SX1262 LoRa radio and the SD slot on
// the DISPLAY's SPI bus (CS 9 and 39), a MIA-M10Q GPS on UART 43/44, and a
// 2000 mAh cell behind a 1:2 divider on GPIO4. Pins from LilyGo's
// utilities.h in the T-Deck repo (examples/UnitTest), 2026-10-12.
//
// The backlight (GPIO42) is NOT given to TFT_eSPI: it is a 16-step driver
// chip that counts pulses, not a PWM pin -- main.cpp's tdeckBacklight()
// steps it.
#pragma once

#define USER_SETUP_INFO    "SquachWatch-CYD / LilyGo T-Deck Plus / ST7789 320x240"
#define ST7789_DRIVER
#define TFT_WIDTH   240
#define TFT_HEIGHT  320

#define TFT_MISO  38
#define TFT_MOSI  41
#define TFT_SCLK  40
#define TFT_CS    12
#define TFT_DC    11
#define TFT_RST   -1   // not wired; the panel comes up with power

#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define SMOOTH_FONT

// 40 MHz for the bring-up; LilyGo's own setup runs the panel there. Try 80
// once the picture is known good.
#ifndef SPI_FREQUENCY
#define SPI_FREQUENCY         40000000
#endif
#define SPI_READ_FREQUENCY    16000000

#define TFT_INVERSION_ON     // reference only: main.cpp's PANEL_NEEDS_INVERSION decides
