// SquachWatch-CYD — TFT_eSPI "user setup" for the M5Stack Tab5, which describes
// a display that does not exist.
//
// The Tab5's panel is a 720x1280 ST7123 on MIPI-DSI; TFT_eSPI cannot drive it
// and never tries. It is kept on this board purely as a TYPE: the app upcasts
// its off-screen sprite to TFT_eSPI* (src/main.cpp's `canvas`), so the sprite
// and the device must share a base class, and only TFT_eSPI's hierarchy gives
// that. Tab5TFT (tab5_display.h) hides every method that would reach a bus,
// M5GFX drives the real panel underneath, and tab5_blit.cpp moves the sprite
// into its framebuffer. tft.init() never runs the library's own init, so none
// of the pins below are ever touched. Same arrangement as the CrowPanel 7.
//
// It exists only to satisfy USER_SETUP_LOADED and to name a driver. The
// processor header is TFT_eSPI's Generic one: the ESP32-P4 has no branch in
// the library, and extra_script.py adds the dispatch line for this env.
#pragma once

#define USER_SETUP_INFO "SquachWatch / M5Stack Tab5 / MIPI-DSI via M5GFX"

// A driver has to be named for the library to compile; nothing is sent to it.
#define ST7789_DRIVER
#define TFT_WIDTH   640
#define TFT_HEIGHT  360

#define TFT_MISO  -1
#define TFT_MOSI  -1
#define TFT_SCLK  -1
#define TFT_CS    -1
#define TFT_DC    -1
#define TFT_RST   -1

// The fonts the app actually loads: the GLCD 6x8 cell and font 2 are what
// theme.cpp measures against, font 4 and the smooth loader are what the 3.5"
// large-panel screens use.
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define SMOOTH_FONT

#ifndef SPI_FREQUENCY
#define SPI_FREQUENCY 20000000
#endif
