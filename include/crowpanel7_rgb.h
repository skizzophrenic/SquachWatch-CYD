// SquachWatch-CYD — the CrowPanel 7's display driver: ESP-IDF's own RGB panel.
//
// WHY NOT LovyanGFX (which this port ran first)
//
// The board showed a slight, constant horizontal twitch under LovyanGFX, and
// none under Elecrow's own examples. The two drivers restart the picture
// differently, and that is the whole difference:
//
//   IDF (esp_lcd_rgb_panel, what Arduino_GFX and Elecrow's examples use):
//     lcd_ll_enable_auto_next_frame(false); then in every VSYNC interrupt
//     gdma_reset / lcd_ll_stop / fifo_reset / gdma_start / lcd_ll_start --
//     the LCD ENGINE and the DMA are restarted TOGETHER. A late interrupt
//     delays the whole frame; sync and data stay in lockstep, so the picture
//     never moves sideways.
//
//   LovyanGFX Bus_RGB:
//     lcd_next_frame_en = true -- the LCD engine free-runs frame after frame
//     in hardware (which is why its VSYNC period measures a perfect 25625 us
//     with 0 us spread), while the interrupt re-points ONLY the DMA at the
//     first descriptor. So the start of the DATA depends on interrupt latency
//     while the SYNC does not. At 16 MHz one microsecond of latency is
//     sixteen pixels. That is the twitch, it is intrinsic to that design,
//     and it is why flash writes (a masked interrupt) made it a step.
//
// So this file drives the panel exactly the way the board's vendor does, and
// the app gets a framebuffer it writes through esp_lcd_panel_draw_bitmap()
// -- the public API, which also writes the cache back for PSRAM, so no
// private struct of the driver is touched and no library needs patching.
//
// Pins and timings: include/crowpanel7_board.h, measured on the board and
// checked against Elecrow's wiki, ESPHome YAML and Arduino examples.
#pragma once
#include <stdint.h>
#include "crowpanel7_board.h"

// Brings the panel up. False means esp_lcd could not allocate the 750 KiB
// framebuffer, which on this board means PSRAM is not in OPI mode.
bool crowPanelBegin();
bool crowPanelUp();

// Writes n whole panel rows starting at panel row y from RGB565 data laid
// out n*PANEL_W long. Handles the PSRAM cache write-back.
void crowPanelWriteRows(int y, int n, const uint16_t* data);

// Writes a w*h RGB565 block at panel x,y.
void crowPanelWriteRect(int x, int y, int w, int h, const uint16_t* data);

// Fills a panel rectangle with one colour (a staging line, then rows).
void crowPanelFillRect(int x, int y, int w, int h, uint16_t color);

// Measured VSYNC period: reads the pin the LCD peripheral drives.
void     crowVsyncWatchBegin();
uint32_t crowVsyncPeriodUs();
void     crowVsyncStatsReset();
void     crowVsyncStats(uint32_t& lo, uint32_t& hi, uint32_t& n);

#if defined(CROWPANEL7_PANELTEST)
// Bring-up: colour bars, a 1-px grid, the frame-period spread quiet vs.
// writing flash. Does not return.
void crowPanelTest();
#endif
