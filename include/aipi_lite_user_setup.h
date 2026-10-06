// Community-derived XY006PL01 pin map; evidence and caveats: docs/AIPI_LITE.md.
#pragma once
#define USER_SETUP_INFO "SquachWatch / AIPI Lite / ST7735 128x128"
#define ST7735_DRIVER
#define ST7735_REDTAB
#define TFT_WIDTH 128
#define TFT_HEIGHT 128
#define TFT_RGB_ORDER TFT_RGB
#define TFT_MISO -1
#define TFT_MOSI 17
#define TFT_SCLK 16
#define TFT_CS 15
#define TFT_DC 7
#define TFT_RST 18
#define SPI_FREQUENCY 20000000
#define LOAD_GLCD
#define LOAD_FONT2

// Capabilities consumed by the mini runtime, with no board-name branches.
#define SQW_BACKLIGHT_PIN 3
#define SQW_BUTTON_PIN 42
#define SQW_WS2812_PIN 46
// Match MicroPython rotation(1): MADCTL 0x60 is TFT_eSPI rotation 3.
#define SQW_PANEL_ROTATION 3
#define SQW_PANEL_INVERTED 0

// Stock CustomPm constructor and voltage conversion; see battery evidence.
#define SQW_BATTERY_ADC_PIN 2
#define SQW_BATTERY_ADC_NUMERATOR 5
#define SQW_BATTERY_ADC_DENOMINATOR 2
#define SQW_USB_ADC_PIN 8
#define SQW_CHARGING_PIN 47
#define SQW_BATTERY_PULSE_PIN 21
