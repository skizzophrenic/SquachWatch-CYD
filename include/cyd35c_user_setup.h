// SquachWatch-CYD — TFT_eSPI user setup for Sunton ESP32-3248S035C (3.5" Capacitive Touch CYD)
// Board: ESP32-3248S035C / Sunton 3.5" with ST7796 TFT LCD (320x480) & GT911 Capacitive Touch
#pragma once

#define USER_SETUP_INFO    "SquachWatch-CYD / ESP32-3248S035C / 3.5 inch ST7796 / GT911 Cap Touch"

// Driver
#define ST7796_DRIVER

// Portrait native (320 wide x 480 tall). Rotated to landscape (480x320) at runtime via setRotation(1).
#define TFT_WIDTH   320
#define TFT_HEIGHT  480

// Display SPI pins (VSPI)
#define TFT_MISO  12
#define TFT_MOSI  13
#define TFT_SCLK  14
#define TFT_CS    15
#define TFT_DC     2
#define TFT_RST   -1   // Tied to EN on this board
#define TFT_BL    27   // Backlight control pin (PWM)

// Backlight parameters
#define TFT_BACKLIGHT_ON   1
#define PWM_FREQ           5000
#define PWM_MAX_DUTY       255

// Capacitive Touch: Goodix GT911 over I2C
// IMPORTANT: TOUCH_CS is deliberately NOT defined here so TFT_eSPI's
// resistive touch driver does not attempt to drive SPI for touch.
#define PIN_I2C_SDA        33
#define PIN_I2C_SCL        32
#define PIN_TOUCH_RST      25
#define PIN_TOUCH_INT      21

// SPI frequencies
#ifndef SPI_FREQUENCY
#define SPI_FREQUENCY         40000000
#endif
#define SPI_READ_FREQUENCY    20000000

// Fonts
#define LOAD_GLCD
#define LOAD_FONT2

#define TFT_INVERSION_ON
#define TFT_RGB_ORDER TFT_BGR
