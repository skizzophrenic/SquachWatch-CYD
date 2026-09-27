// SquachWatch-CYD — TFT_eSPI config for the RL Phantom, RESISTIVE variant
// (Sunton ESP32-2432S024R, 2.4 inch, ILI9341).
//
// Identical to rlphantom_user_setup.h in every display respect -- same panel,
// same pins, same backlight -- with one addition: TOUCH_CS.
//
// WHY THAT ONE LINE IS THE WHOLE DIFFERENCE. This board's XPT2046 sits on the
// DISPLAY's SPI bus, not a dedicated one, exactly like AWOK's. Arming TOUCH_CS
// here lets TFT_eSPI read raw touch values off that bus, and main.cpp's
// TOUCH_RAW_SHARED_BUS branches then run the 2.8" board's rotation maths on
// them, so touch follows the screen when it rotates.
//
// The standard path would look for the touch chip on GPIO 25/32/39. On this
// board those are the capacitive controller's reset, the I2C clock, and an
// unrelated input -- so a resistive Phantom finds nothing at all there.
//
// WHICH ONE DO I HAVE? The part number ends in R (resistive, this file) or C
// (capacitive, rlphantom_user_setup.h). Only this resistive build is on the
// public flasher; the capacitive one has never run on a real board.
#pragma once

#define USER_SETUP_INFO    "SquachWatch-CYD / RL Phantom 2.4 inch resistive / ILI9341"
#define ILI9341_DRIVER

#define TFT_WIDTH   240
#define TFT_HEIGHT  320
// One pixel on this glass, in micrometres: the 2.4" (rlphantom_user_setup.h
// has the figures), 0.152 mm.
#define SQW_PIXEL_PITCH_UM 152

// Display, on VSPI. Identical to the 2.8" board and to the capacitive Phantom.
// On the HSPI engine, whose native pins these are. Left unsaid, TFT_eSPI
// takes VSPI, and so does the Arduino `SPI` object the SD card uses on
// 18/19/23: two pin sets on one engine. On this board the touch chip reads
// through the display's MISO, so the SD card's pins being attached to the
// same engine killed touch, and handing the SD card the display's engine to
// avoid that meant the card never saw a clock. Two engines, two buses, no
// sharing: the display and touch here, the SD card on VSPI.
#define USE_HSPI_PORT
#define TFT_MISO  12
#define TFT_MOSI  13
#define TFT_SCLK  14
#define TFT_CS    15
#define TFT_DC     2
#define TFT_RST   -1   // NO software reset — rely on power-on reset.

#define TFT_BL    27
#define TFT_BACKLIGHT_ON HIGH

// The line that matters: touch chip select, on the display's own bus. Taken
// from the vendor's factory User_Setup.h for the resistive demo.
#define TOUCH_CS  33

// 40MHz, spec-compliant. No overclock variant for this board -- see the
// capacitive header for why.
#ifndef SPI_FREQUENCY
#define SPI_FREQUENCY         40000000
#endif
#define SPI_READ_FREQUENCY    20000000
#define SPI_TOUCH_FREQUENCY    2500000

#define LOAD_GLCD
// And font 2, the 16-row proportional face, for the speech bubbles: the
// 6x8 font was a squint on a 2.4" screen and the only other size it comes
// in is double. About 3 KB of glyphs.
// NOTE: editing this file does not rebuild the library -- delete
// .pio/build/<env>/lib*/TFT_eSPI first, or the board runs the old config.
#define LOAD_FONT2
#define LOAD_FONT2
#define LOAD_FONT4
#define SMOOTH_FONT

#define TFT_INVERSION_OFF
#define TFT_RGB_ORDER TFT_RGB
