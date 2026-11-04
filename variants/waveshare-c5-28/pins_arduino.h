#ifndef Pins_Arduino_h
#define Pins_Arduino_h

// Waveshare ESP32-C5-Touch-LCD-2.8. Pins from Waveshare's pin table
// (docs.waveshare.com/ESP32-C5-Touch-LCD-2.8). The display's own pins live in
// include/waveshare_c5_28_user_setup.h; this is only what the Arduino core
// itself wants. No RGB LED on this board.

#include "soc/soc_caps.h"
#include <stdint.h>

static const uint8_t TX = 11;
static const uint8_t RX = 12;

static const uint8_t USB_DM = 13;
static const uint8_t USB_DP = 14;

// The one I2C bus: the CH32V003 helper, the CST3530 touch, the QMI8658,
// PCF85063, SHTC3 and the ES8311 codec.
static const uint8_t SDA = 0;
static const uint8_t SCL = 1;

// The panel's and the SD card's bus.
static const uint8_t SS   = 23;   // the SD card's chip select
static const uint8_t MOSI = 7;
static const uint8_t MISO = 8;
static const uint8_t SCK  = 6;

// LP I2C Pins are fixed on ESP32-C5
// (GPIO5 is the touch interrupt on this board: Wire1 is not used.)
static const uint8_t LP_SDA = 4;
static const uint8_t LP_SCL = 5;
#define WIRE1_PIN_DEFINED
#define SDA1 LP_SDA
#define SCL1 LP_SCL

// LP UART Pins are fixed on ESP32-C5
static const uint8_t LP_RX = 12;
static const uint8_t LP_TX = 11;

#endif /* Pins_Arduino_h */
