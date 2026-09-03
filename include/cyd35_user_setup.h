// SquachWatch-CYD — TFT_eSPI user setup for the 3.5" Sunton-family CYD
// (ESP32-3248S035R — "R" = resistive touch; confirmed against a physical
// unit whose silkscreen reads "3.5" LCD Display / ESP32-32E 320x480 /
// Resistance Touch"). Pinout cross-referenced against esp3d.io's
// sunton-35-3248 hardware page, and several of these pins already match
// known-good constants elsewhere in this codebase for the OTHER board
// variants (SCK/MOSI/MISO/DC/CS identical to the 2.8" board; BL=27
// already exists as BL_PIN_CAP in main.cpp) — one real fresh unknown
// here is the driver chip itself.
//
// UNCONFIRMED ON REAL HARDWARE (post color-inversion-bug-fix): the
// TFT_INVERSION_ON/TFT_RGB_ORDER combo below is a first guess, not a
// verified one -- see main.cpp's PANEL_NEEDS_INVERSION comment for why
// this header's TFT_INVERSION_ON/OFF define alone is dead code for
// choosing polarity (a real bug found on AWOK: tft.invertDisplay() at
// runtime overwrites it unconditionally). If colors still look wrong,
// PANEL_NEEDS_INVERSION in main.cpp is the one to flip, not this file
// -- TFT_RGB_ORDER below is still live and worth trying too.
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
// own dedicated peripheral. Driven entirely through TFT_eSPI's own
// calibrateTouch()/setTouch()/getTouch() path (see the CYD35-specific
// touch init in main.cpp) rather than the standalone XPT2046_Touchscreen
// library -- an independent SPIClass fighting TFT_eSPI for the same
// physical bus is what produced garbage reads and a free-running IRQ
// when that was tried. TFT_eSPI drives its own CS once TOUCH_CS is
// defined here, same as AWOK; no separate SCK/MOSI/MISO or IRQ pin
// needed for this path.
#define TOUCH_CS  33

// Backlight
#define TFT_BACKLIGHT_ON   1
#define PWM_FREQ           5000
#define PWM_MAX_DUTY       255

// SPI frequencies -- 40MHz write was a cautious first guess; a friend's
// independently-verified working config for this exact panel (QDtech
// E32R35T / Sunton 3248S035R) runs the write clock at 55MHz, so trying
// that here for the "everything feels slow" complaint.
#define SPI_FREQUENCY         55000000
#define SPI_READ_FREQUENCY    20000000
#define SPI_TOUCH_FREQUENCY    2500000

// Fonts. Only the built-in GLCD font (font 1) is loaded: nothing in
// this project ever calls setTextFont(), setFreeFont() or loadFont(),
// and there are no .vlw assets -- every screen uses font 1 via
// setTextSize(), and the one custom typeface (Bangers, on the ALERT
// and CLEAR headings) ships as its own glyph tables in
// include/bangers_font.h rather than through TFT_eSPI at all.
// LOAD_FONT2/4/6/7/8 and SMOOTH_FONT were ~11KB of glyph data plus the
// .vlw renderer that nothing could reach.
#define LOAD_GLCD

// This define alone does nothing at runtime -- main.cpp's
// tft.invertDisplay() call always overwrites it (see
// PANEL_NEEDS_INVERSION there, the actual control point). Left at
// TFT_INVERSION_ON only because TFT_eSPI expects one of the two to be
// defined; the earlier "tried both, no effect" testing that produced
// this file's original TFT_RGB_ORDER guess was unknowingly toggling
// this dead define instead of the real one.
#define TFT_INVERSION_ON
#define TFT_RGB_ORDER TFT_BGR
