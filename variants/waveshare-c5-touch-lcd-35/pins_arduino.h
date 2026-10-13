// Waveshare ESP32-C5-Touch-LCD-3.5 (ESP32-C5-WROOM-1U-N32R8).
//
// Deliberately small. The NM-CYD-C5's variant also #defines that board's
// TFT_eSPI pins (TFT_CS 23, TFT_DC 24, TOUCH_CS 1...), and because Arduino.h
// pulls this file in AFTER the -include'd user setup, those defines would
// quietly win over waveshare_c5_touch_lcd_35_user_setup.h. So nothing display-related
// lives here: the panel's pins are in that header and nowhere else.
//
// Pins from Waveshare's BSP (example/ESP-IDF-V554/.../esp32_c5_touch_lcd_3_5.h)
// and the board schematic.
#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include "soc/soc_caps.h"
#include <stdint.h>

// UART0. The USB-C port is the C5's own USB (GPIO13/14) -- there is no
// USB-to-UART chip on this board -- so the console is USB CDC.
static const uint8_t TX = 11;
static const uint8_t RX = 12;

static const uint8_t USB_DM = 13;
static const uint8_t USB_DP = 14;

// The one I2C bus: the CH32V003 I/O expander (0x24), FT6336 touch (0x38),
// AXP2101, ES8311, QMI8658, PCF85063 and SHTC3 all sit on it.
static const uint8_t SDA = 27;
static const uint8_t SCL = 26;

// The display's SPI bus, which the microSD slot shares (card CS is GPIO9).
static const uint8_t SS   = 9;
static const uint8_t MOSI = 7;
static const uint8_t MISO = 2;
static const uint8_t SCK  = 6;

static const uint8_t A0 = 1;
static const uint8_t A1 = 2;
static const uint8_t A2 = 3;
static const uint8_t A3 = 4;
static const uint8_t A4 = 5;
static const uint8_t A5 = 6;

// LP I2C / LP UART pins are fixed on the ESP32-C5.
static const uint8_t LP_SDA = 4;
static const uint8_t LP_SCL = 5;
#define WIRE1_PIN_DEFINED
#define SDA1 LP_SDA
#define SCL1 LP_SCL

static const uint8_t LP_RX = 12;
static const uint8_t LP_TX = 11;

#endif /* Pins_Arduino_h */
