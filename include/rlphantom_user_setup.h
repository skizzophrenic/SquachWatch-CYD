// SquachWatch-CYD — TFT_eSPI config for the RL Phantom
// (Sunton ESP32-2432S024, 2.4 inch, ILI9341).
//
// WHAT THIS BOARD IS. A 2.4" CYD with the SAME display wiring and the SAME
// 240x320 panel as the 2.8" board this project was built on -- smaller glass,
// identical pins. Vendor repo: github.com/NoosaHydro/2.4inch_ESP32-2432S024,
// and these numbers come from its own factory User_Setup.h rather than from a
// guess or a forum post.
//
// WHY IT GETS ITS OWN HEADER when it is nearly a copy of the 2.8" ILI9341 one:
//   - The backlight is on GPIO27, not 21. Harmless either way (main.cpp drives
//     all three known backlight pins on their own LEDC channels precisely so it
//     never has to know which board it is), but a header that says 21 on a
//     board wired to 27 is a lie that outlives whoever wrote it.
//   - USER_SETUP_INFO is what the serial banner reports. A tester saying "it
//     says 2.8 inch" when they are holding a 2.4 is a wasted round trip.
//
// TOUCH IS NOT CONFIGURED HERE, deliberately. This board ships in two flavours:
//   ESP32-2432S024R -- resistive, XPT2046, touch CS on GPIO33
//   ESP32-2432S024C -- capacitive, CST820 on I2C, SDA on GPIO33
// The same pin, two entirely different chips. main.cpp already handles this
// without being told which: it probes for the capacitive controller at I2C
// 0x15 first and falls back to the resistive path when nothing answers (see
// the CapTouch::probe() block in setup()). Both variants therefore work from
// one build, and the serial log says which one it found.
//
// The board reportedly lights up under the AWOK firmware but has dead touch,
// which is exactly what a capacitive panel does when the firmware is looking
// for a resistive one on a shared SPI bus. This build should fix that without
// any new code.
#pragma once

#define USER_SETUP_INFO    "SquachWatch-CYD / RL Phantom 2.4 inch / ILI9341"
#define ILI9341_DRIVER

#define TFT_WIDTH   240
#define TFT_HEIGHT  320
// One pixel on this glass, in micrometres: 2.4" 240x320, active area
// 36.72 x 48.96 mm, 0.152 mm a pixel (measured last pass). Same resolution as
// the 2.8" and a sixth smaller, which is exactly why Theme::computeButtonBar
// takes this rather than the resolution.
#define SQW_PIXEL_PITCH_UM 152

// Display, on VSPI. Identical to the 2.8" board -- see the note above.
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

// The one real difference from the 2.8" boards.
#define TFT_BL    27
#define TFT_BACKLIGHT_ON HIGH

// 40MHz, the spec-compliant clock, same as the 2.8" ILI9341 build. The
// overclock is NOT offered for this board: nobody has run one yet, and an
// out-of-spec display clock is a poor thing to discover on hardware you have
// never seen. #ifndef so a -D can still override it for an experiment.
#ifndef SPI_FREQUENCY
#define SPI_FREQUENCY         40000000
#endif
#define SPI_READ_FREQUENCY    20000000
#define SPI_TOUCH_FREQUENCY    2500000

// Fonts: the same set every other board loads. The UI draws its own glyphs
// for anything larger (see theme.cpp's Bangers set), so this is the built-in
// GLCD font plus the two TFT_eSPI faces the status rows use.
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

// Colour handling. Taken from the vendor's factory file, which sets NEITHER
// inversion nor a colour order -- meaning the ILI9341's own defaults. That
// matches the 2.8" ILI9341 header, so both are spelled out the same way here
// rather than left implicit.
//
// If the tester reports colours are swapped (red where blue should be) or the
// whole screen is a photographic negative, these two are the lines to change,
// and CHECK COLORS in the SYSTEM menu is the screen to judge it on.
#define TFT_INVERSION_OFF
#define TFT_RGB_ORDER TFT_RGB
