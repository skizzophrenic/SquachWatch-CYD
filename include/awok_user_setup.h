// SquachWatch-CYD — TFT_eSPI user setup for the AWOK 2.4" board
// (JustCallMeKoko ESP32 Marauder V6.1 hardware — WROOM-32 module,
// ILI9341 240x320 panel, XPT2046 resistive touch and SD card sharing
// the display's VSPI bus). Cross-referenced against the AWOK 2.4"
// board turnover doc and the ESP32-Marauder V6.1 stock repo's own
// User_Setup.h — this is the vendor's config for the exact PCB.
//
// Unlike the 2.8" CYD variants this repo also supports, the 2.4" AWOK
// uses the plain ILI9341_DRIVER (NOT the _2 variant that Marauder MINI
// and some cheap CYDs use — the _2 init sequence leaves this panel
// blank/white on real hardware, per the turnover doc). CS/DC are 17/16,
// NOT the 27/26 that Marauder MINI uses.
#pragma once

#define USER_SETUP_INFO    "SquachWatch-CYD / AWOK 2.4 inch / ILI9341"
#define ILI9341_DRIVER
// Portrait native (240 wide x 320 tall). We rotate to landscape at
// runtime via setRotation(1), same as the sister boards.
#define TFT_WIDTH   240
#define TFT_HEIGHT  320

// SPI pins — VSPI (18/23/19). Same physical bus is shared with the SD
// card (CS=14) and the XPT2046 touch controller (CS=21); anything that
// opens SPI at runtime (radios on this board's free pads, etc.) must
// coexist through TFT_eSPI's own transaction management. CS=17 and
// DC=16 are load-bearing: 27/26 are Marauder MINI pins and produce a
// white screen on this board.
#define TFT_MISO  19
#define TFT_MOSI  23
#define TFT_SCLK  18
#define TFT_CS    17
#define TFT_DC    16
#define TFT_RST    5
#define TFT_BL    32

// Touch (XPT2046, resistive) shares this same VSPI bus rather than
// getting its own dedicated peripheral — see the AWOK-specific touch
// init in main.cpp. TFT_eSPI drives its CS itself once TOUCH_CS is
// defined.
#define TOUCH_CS  21

// Backlight
#define TFT_BACKLIGHT_ON   1
#define PWM_FREQ           5000
#define PWM_MAX_DUTY       255

// SPI frequencies — values from the Marauder V6.1 stock repo. If the
// display goes black under WiFi/BLE load, drop SPI_FREQUENCY to
// 20 MHz — not a crash, just APB scaling corrupting the SPI divider.
#define SPI_FREQUENCY         27000000
#define SPI_READ_FREQUENCY    20000000
#define SPI_TOUCH_FREQUENCY    2500000

// Fonts
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define SMOOTH_FONT

// IMPORTANT: TFT_INVERSION_ON/OFF here does NOT control this board's
// actual inversion polarity -- main.cpp's tft.invertDisplay() call in
// setup() overwrites whatever this sets, unconditionally, for every
// board. The real switch is PANEL_NEEDS_INVERSION in main.cpp's
// setup(), not this define (several rounds of flipping this on real
// hardware with zero visible effect is what found that). Left here at
// its default only because TFT_eSPI expects one of the two to be
// defined; change PANEL_NEEDS_INVERSION in main.cpp instead if colors
// still look wrong.
#define TFT_INVERSION_OFF
// RGB vs BGR: unlike inversion above, there's no runtime override for
// this -- it's genuinely controlled by this define alone. RGB matched
// the AWOK turnover doc's nominal config but read wrong on real
// hardware once PANEL_NEEDS_INVERSION (main.cpp) was corrected to its
// actual value; trying BGR next.
#define TFT_RGB_ORDER TFT_BGR
