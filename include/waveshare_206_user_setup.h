// SquachWatch-CYD — TFT_eSPI "user setup" for the Waveshare
// ESP32-S3-Touch-AMOLED-2.06, which, like the Freenove 3.5"'s, describes a
// display that does not exist.
//
// The panel is a CO5300 AMOLED on a four-lane (QSPI) bus, which TFT_eSPI
// cannot drive. TFT_eSPI stays as a TYPE only: Co5300TFT (co5300_display.h)
// hides every method that would reach a bus, co5300_qspi.cpp drives the
// panel, and FramePush's QSPI route ships each frame. None of the pins below
// are ever touched.
//
// The board otherwise follows the T-Watch S3 build (TWATCH_S3 is defined
// too): an AXP2101 power chip, a power key, an FT3168 touch controller (the
// FT6336's register layout), a PCF85063 clock, a QMI8658 motion sensor, all
// on one I2C bus (SDA 15, SCL 14). No LoRa radio, no GPS, no vibration motor.
//
// Pulled in via -include in platformio.ini's [env:waveshare-206].
#pragma once

#define USER_SETUP_INFO "SquachWatch-CYD / Waveshare ESP32-S3-Touch-AMOLED-2.06 / CO5300 over QSPI"

// A driver has to be named for the library to compile; nothing is sent to it.
#define ST7789_DRIVER
#define TFT_WIDTH   240
#define TFT_HEIGHT  240

#define TFT_MISO  -1
#define TFT_MOSI  -1
#define TFT_SCLK  -1
#define TFT_CS    -1
#define TFT_DC    -1
#define TFT_RST   -1

#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define SMOOTH_FONT
#define LOAD_GFXFF     // FreeSans for the AMOLED's bands (main.cpp's wsBandsTick)

#ifndef SPI_FREQUENCY
#define SPI_FREQUENCY         20000000
#endif
#define SPI_READ_FREQUENCY    20000000
