// SquachWatch-CYD — TFT_eSPI setup for LCDWiki 2.8-inch ESP32-S3 display.
//
// Based on the manufacturer's Demo.zip supplied alongside this project:
//   ILI9341 controller; CS=10, DC=46, reset not connected, backlight=45;
//   SPI SCLK=12, MOSI=11, MISO=13. The demo uses the standard ILI9341
// register initialization (including inversion command 0x21 and MADCTL
// 0x08), so this intentionally uses ILI9341_DRIVER rather than
// Freenove's ILI9341_2_DRIVER. Do not change the Freenove setup for this.
//
// The demo's low-level register writes use FSPI and 80 MHz. This build uses
// TFT_eSPI's normal SPI transaction path at 40 MHz as a conservative starting
// point for the full UI. If it is stable, 80 MHz can be tested independently.
#pragma once

#define USER_SETUP_INFO "SquachWatch-CYD / LCDWiki ESP32-S3 2.8 inch / ILI9341"

#define ILI9341_DRIVER
#define TFT_WIDTH  240
#define TFT_HEIGHT 320

// Do not define USE_HSPI_PORT: use the default ESP32-S3 SPI peripheral.
#define TFT_MISO 13
#define TFT_MOSI 11
#define TFT_SCLK 12
#define TFT_CS   10
#define TFT_DC   46
#define TFT_RST  -1
#define TFT_BL   45
#define TFT_BACKLIGHT_ON HIGH

#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define SMOOTH_FONT

#ifndef SPI_FREQUENCY
#define SPI_FREQUENCY 40000000
#endif
#define SPI_READ_FREQUENCY 20000000

#define TFT_INVERSION_ON
#define TFT_RGB_ORDER TFT_BGR
