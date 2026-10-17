// SquachWatch-CYD - TFT_eSPI setup for the Hosyond/LCDWiki 4.0" CYD
// E32R40T-class board: ESP32-32E + ST7796S + XPT2046 resistive touch.
//
// Native glass is 320x480. SquachWatch rotates it to landscape at runtime.
//
// Known board wiring:
//   LCD:   CS=15 DC=2 SCLK=14 MOSI=13 MISO=12 BL=27
//   TOUCH: CS=33 IRQ=36; XPT2046 shares the LCD SPI bus
//   RGB:   R=22 G=16 B=17, common-anode / active-low
//   AUDIO: DAC=26, amplifier shutdown/enable=4, active-low
//   SD:    SCK=18 MISO=19 MOSI=23 CS=5
//   BATT:  ADC=34
#pragma once

#define USER_SETUP_INFO "SquachWatch-CYD / CYD 4.0 / ST7796S"

#define ST7796_DRIVER
#define TFT_WIDTH  320
#define TFT_HEIGHT 480

#define TFT_MISO 12
#define TFT_MOSI 13
#define TFT_SCLK 14
#define TFT_CS   15
#define TFT_DC    2
#define TFT_RST  -1
#define TFT_BL   27

#define TOUCH_CS 33

#define TFT_BACKLIGHT_ON 1
#define PWM_FREQ         5000
#define PWM_MAX_DUTY     255

#ifndef SPI_FREQUENCY
#define SPI_FREQUENCY       40000000
#endif
#define SPI_READ_FREQUENCY  20000000
#define SPI_TOUCH_FREQUENCY  2500000

#define LOAD_GLCD
#define LOAD_FONT2

#define TFT_INVERSION_ON
#define TFT_RGB_ORDER TFT_BGR