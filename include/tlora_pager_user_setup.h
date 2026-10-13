// SquachWatch-CYD -- TFT_eSPI user setup for the LilyGo T-LoRa Pager.
//
// ESP32-S3 with 16 MB flash and 8 MB OPI PSRAM, a 2.33" 222x480 ST7796 in a
// 320x480 controller (the glass starts 49 columns in), an XL9555 expander at
// 0x20 that powers the keyboard, radio, GPS and SD card, and an AW9364
// backlight on GPIO42 that counts pulses rather than taking PWM -- see
// pagerPowerUp() and pagerBacklight() in main.cpp. Pins from arduino-esp32's
// variants/lilygo_tlora_pager/pins_arduino.h.
//
// The panel shares SCK 35 / MISO 33 / MOSI 34 with the SX1262 (CS 36), the
// ST25R3916 NFC (CS 39) and the SD card (CS 21). TFT_eSPI drives the global
// SPI object, the radio's RadioLib Module is handed the same one, and every
// transfer on both sides goes through SPIClass::beginTransaction(), whose
// lock is what keeps them off each other's frames.
#pragma once

#define USER_SETUP_INFO    "SquachWatch-CYD / LilyGo T-LoRa Pager / ST7796 222x480"
#define ST7796_DRIVER
#define TFT_WIDTH   222
#define TFT_HEIGHT  480
#define CGRAM_OFFSET         // TFT_eSPI knows the 49-column offset of a 222-wide ST7796

#define TFT_MISO  33
#define TFT_MOSI  34
#define TFT_SCLK  35
#define TFT_CS    38
#define TFT_DC    37
#define TFT_RST   -1   // no reset line of its own; the expander's rail brings it up
// GPIO42 is the backlight. TFT_eSPI is not given it: main.cpp counts the
// AW9364's pulses on it.

#define TFT_INVERSION_ON     // LilyGoLib inverts this panel

#ifndef SPI_FREQUENCY
#define SPI_FREQUENCY         40000000
#endif
#define SPI_READ_FREQUENCY    16000000
#define SUPPORT_TRANSACTIONS  // the bus is shared: see above

#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define SMOOTH_FONT
