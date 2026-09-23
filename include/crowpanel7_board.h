// SquachWatch-CYD — Elecrow CrowPanel ADVANCE 7.0-HMI board facts.
//
// ESP32-S3-WROOM-1-N16R8: 16 MB flash, 8 MB OCTAL PSRAM, an 800x480 RGB
// parallel panel (no SPI display bus at all), a GT911 capacitive controller
// and an STC8H1K28 helper MCU that owns the backlight, both on I2C 15/16.
// USB-C reaches UART0 through a CH340K, so there is no native USB.
//
// Every number here was measured on this board (2026-09) and checked against
// Elecrow's wiki, their ESPHome YAML and their Arduino examples. Nothing is
// derived, guessed or read off a product page. Mind the look-alike: the plain
// HMI 7.0 (DIS08070H) shares almost no pin with the Advance, and its numbers
// are NOT the ones here.
#pragma once

// ---- Geometry -------------------------------------------------------------
#define PANEL_W 800
#define PANEL_H 480

// ---- RGB565 data pins -----------------------------------------------------
// Blue first: esp_lcd's data_gpio_nums[0..15] are wired B0..B4, G0..G5, R0..R4.
#define LCD_B0 21
#define LCD_B1 47
#define LCD_B2 48
#define LCD_B3 45
#define LCD_B4 38
#define LCD_G0  9
#define LCD_G1 10
#define LCD_G2 11
#define LCD_G3 12
#define LCD_G4 13
#define LCD_G5 14
#define LCD_R0  7
#define LCD_R1 17
#define LCD_R2 18
#define LCD_R3  3
#define LCD_R4 46

#define LCD_DE    42
#define LCD_VSYNC 41
#define LCD_HSYNC 40
#define LCD_PCLK  39

// ---- Timing ---------------------------------------------------------------
// The pixel clock has a hard ceiling between 16 and 18 MHz on this panel, and
// it is a delivery ceiling (framebuffer reads off the octal bus), not a
// timing-shape one: 20 MHz divides 240 MHz evenly and still runs away
// horizontally; 16 MHz does not divide evenly and is rock steady. Widening
// the blanking does not buy anything past it. Below ~14 MHz the panel stops
// holding a picture at all and cycles black/white/R/G/B.
//
//   15 MHz  36.5 Hz  locks, brightness flickers
//   16 MHz  39.0 Hz  locks, steady          <-- this one
//   18 MHz  43.9 Hz  runs away horizontally
//   20 MHz  48.8 Hz  runs away horizontally
// Elecrow's own porches. Widening them (40/4/40, 16/4/16) was tried against
// the sideways step and did not move it; the cause was elsewhere.
#define LCD_HSYNC_POLARITY    0
#define LCD_HSYNC_FRONT_PORCH 8
#define LCD_HSYNC_PULSE_WIDTH 4
#define LCD_HSYNC_BACK_PORCH  8
#define LCD_VSYNC_POLARITY    0
#define LCD_VSYNC_FRONT_PORCH 8
#define LCD_VSYNC_PULSE_WIDTH 4
#define LCD_VSYNC_BACK_PORCH  8
#define LCD_DE_IDLE_HIGH      0
// Data is latched on the falling edge of the pixel clock; the rising edge
// tears (measured).
#define LCD_PCLK_ACTIVE_NEG   1
#define LCD_PCLK_HZ           16000000L

// ---- I2C: touch, backlight helper, clock ----------------------------------
#define PIN_I2C_SDA    15
#define PIN_I2C_SCL    16
// GT911 INT — and its I2C address strap. Held low across the release of reset
// it latches the controller to 0x5D rather than 0x14.
#define PIN_TOUCH_INT   1
#define I2C_HZ         100000   // the speed Elecrow's own examples use

#define STC8_ADDR      0x30     // helper MCU: backlight, buzzer, touch wake
#define PCF8563_ADDR   0x51     // real-time clock (unused here)
#define GT911_ADDR_A   0x5D
#define GT911_ADDR_B   0x14

#define GT911_REG_PRODUCT_ID 0x8140
#define GT911_REG_STATUS     0x814E
// 0x814F, NOT 0x8150. One byte late makes x out of (x_hi | y_lo << 8), so
// every real finger lands past 1024 and looks like a ghost contact.
#define GT911_REG_POINT1     0x814F

// STC8 command bytes. A bare byte, no register address. Elecrow states the
// rest of the command space is undocumented, so nothing else is ever sent.
#define STC8_BL_BRIGHTEST 0     // yes, inverted: 0 is brightest
#define STC8_BL_DIMMEST   244
#define STC8_BL_OFF       245
#define STC8_TOUCH_WAKE   250

// ---- What the UI thinks the screen is -------------------------------------
//
// The app renders at PANEL / CROWPANEL_SCALE and the blit doubles it up.
//
// Scale 2 (the default): 400x240, doubled to exactly 800x480. The 96 KB frame
// lives in INTERNAL RAM, and that is the point, not a memory economy: on this
// board the picture twitches sideways whenever the CPU writes into PSRAM
// while the panel's DMA is reading its framebuffer from the same bus -- the
// app's scattered per-pixel writes into a PSRAM sprite are exactly that.
// Measured on the bench with a static test picture: stable alone, stable
// with WiFi and BLE scanning underneath, twitching hard the moment the UI
// drew into a PSRAM sprite, stable again with the sprite in internal RAM.
// The blit's own writes are large and sequential and do not upset it.
//
// Scale 1: native 800x480. The 384 KB frame can only live in PSRAM, so the
// picture twitches on this core (arduino-esp32 2.0.14, PSRAM at 80 MHz).
// Kept for a core with bounce buffers or faster PSRAM (IDF 5.x / Elecrow's
// 120 MHz libraries), where it should be the mode of choice.
#ifndef CROWPANEL_SCALE
#define CROWPANEL_SCALE 2
#endif
#define SQW_BLIT_SCALE CROWPANEL_SCALE
#define SQW_LOGICAL_W (PANEL_W / SQW_BLIT_SCALE)   // 400 or 800
#define SQW_LOGICAL_H (PANEL_H / SQW_BLIT_SCALE)   // 240 or 480
#define SQW_BLIT_X0   0
#define SQW_BLIT_Y0   0
// The frame fits internal RAM only at scale 2.
#define CROWPANEL_FRAME_INTERNAL (CROWPANEL_SCALE >= 2)

// The raw touch ranges that make main.cpp's pre-calibration fit come out as
// the right affine map. The GT911 reports panel pixels, so this is a plain
// divide by the scale and no calibration is ever needed.
#define CROW_CAP_NX_MIN 0
#define CROW_CAP_NX_MAX PANEL_W
#define CROW_CAP_NY_MIN 0
#define CROW_CAP_NY_MAX PANEL_H
