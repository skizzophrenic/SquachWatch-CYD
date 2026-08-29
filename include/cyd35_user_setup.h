// SquachWatch-CYD — TFT_eSPI user setup for the 3.5" Sunton-family CYD
// (ESP32-3248S035R — "R" = resistive touch; confirmed against a physical
// unit whose silkscreen reads "3.5" LCD Display / ESP32-32E 320x480 /
// Resistance Touch"). Pinout cross-referenced against esp3d.io's
// sunton-35-3248 hardware page, and several of these pins already match
// known-good constants elsewhere in this codebase for the OTHER board
// variants (SCK/MOSI/MISO/DC/CS identical to the 2.8" board; BL=27 and
// touch CS=33/IRQ=36 already exist as BL_PIN_CAP/TOUCH_CS/TOUCH_IRQ in
// main.cpp) — one real fresh unknown here is the driver chip itself.
//
// UNVERIFIED ON REAL HARDWARE: inversion/RGB order below are TFT_eSPI's
// usual ST7796 defaults, not confirmed against this specific panel yet.
// If first boot shows inverted or wrong-hued colors, that's the first
// thing to flip (see TFT_INVERSION_ON / TFT_RGB_ORDER below) — same
// troubleshooting posture as the ST7789-vs-ILI9341 note in
// cyd_user_setup.h.
#pragma once

#define USER_SETUP_INFO    "SquachWatch-CYD / 3.5 inch / ST7796"
#define ST7796_DRIVER
// Portrait native (320 wide x 480 tall glass). We rotate to landscape
// at runtime via setRotation(1), same as the 2.8" board.
#define TFT_WIDTH   320
#define TFT_HEIGHT  480

// SPI pins — identical bus to the 2.8" board's display (VSPI: 12/13/14),
// which is what lets the touch controller share this same bus below
// instead of needing a fully separate SPI peripheral.
#define TFT_MISO  12
#define TFT_MOSI  13
#define TFT_SCLK  14
#define TFT_CS    15
#define TFT_DC     2
#define TFT_RST   -1   // Tied to EN on this board — no software reset pin.
#define TFT_BL    27

// Touch (XPT2046, resistive) shares this SPI bus rather than getting its
// own dedicated peripheral — see the CYD35-specific touch init in
// main.cpp. CS=33/IRQ=36 only, no separate SCK/MOSI/MISO needed here.

// Backlight
#define TFT_BACKLIGHT_ON   1
#define PWM_FREQ           5000
#define PWM_MAX_DUTY       255

// SPI frequencies
#define SPI_FREQUENCY         40000000
#define SPI_READ_FREQUENCY    20000000

// Fonts
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define SMOOTH_FONT

// Confirmed on real hardware: still looked "inverted" at BOTH
// TFT_INVERSION_OFF and TFT_INVERSION_ON (verified via
// main.cpp's tft.invertDisplay() runtime call, which sends an
// absolute INVON/INVOFF command regardless of this define -- see its
// comment). That rules out inversion as the actual problem; trying
// TFT_RGB_ORDER (red/blue channel swap) next.
#define TFT_INVERSION_ON
#define TFT_RGB_ORDER TFT_BGR
