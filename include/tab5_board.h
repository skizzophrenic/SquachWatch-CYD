// SquachWatch-CYD — M5Stack Tab5 board facts.
//
// ESP32-P4 (rev 1.x silicon on this unit), 16 MB flash, 32 MB hex PSRAM at
// 200 MHz, a 5" 720x1280 panel on two MIPI-DSI lanes. The panel on this unit
// is an ST7123 with its touch controller in the same chip (I2C 0x55 on
// SDA 31 / SCL 32, INT on 23); earlier units carry an ILI9881C + GT911 and
// M5GFX tells them apart at boot. Two PI4IOE5V6408 expanders (0x43, 0x44)
// hold the panel's reset, the touch reset, the speaker enable and the
// radio's power; M5GFX programs both on init. The ESP32-C6 radio hangs off
// SDIO; src/tab5_radio.cpp drives its WiFi and Bluetooth (docs/TAB5.md).
// USB-C is the P4's own
// USB-serial-JTAG. Nothing here is guessed: the pins are M5GFX's and the
// reference BSP's, the memory numbers were read off the board (2026-10-19).
#pragma once

// ---- Geometry -------------------------------------------------------------
// The glass is portrait as wired: 720 wide, 1280 tall. The app wants it
// landscape, so the blit turns the picture a quarter turn as it writes the
// framebuffer. TAB5_ROTATION is M5GFX's rotation for the panel object (1 =
// landscape, 1280x720); the blit's own mapping follows the same convention
// (tab5_blit.cpp says what that is), so a fallback draw through M5GFX and a
// frame through the blit land in the same place. Portrait later: rotation 0
// here, a second mapping in the blit, and TAB5_PANEL_W/H swap.
#define TAB5_NATIVE_W 720
#define TAB5_NATIVE_H 1280
#define TAB5_ROTATION 1
#define TAB5_PANEL_W  TAB5_NATIVE_H   // 1280, landscape
#define TAB5_PANEL_H  TAB5_NATIVE_W   // 720

// ---- What the UI thinks the screen is -------------------------------------
// The app renders at PANEL / TAB5_SCALE and the blit doubles it up, the
// CrowPanel's arrangement. Scale 2 is 640x360: a 225 KB 8-bit frame that fits
// the P4's internal SRAM with room to spare (largest block 385 KB at boot),
// which is what makes the blit's column-wise reads cheap. Native 1280x720
// would be a 900 KB frame in PSRAM and four times the drawing for the same
// layouts; nothing the UI does needs it.
#ifndef TAB5_SCALE
#define TAB5_SCALE 2
#endif
#define TAB5_LOGICAL_W (TAB5_PANEL_W / TAB5_SCALE)   // 640
#define TAB5_LOGICAL_H (TAB5_PANEL_H / TAB5_SCALE)   // 360
#define TAB5_FRAME_INTERNAL 1

// The touch reports panel pixels (M5GFX applies the rotation), so these make
// main.cpp's initTouchFit() a plain divide by the scale; no calibration.
#define TAB5_CAP_NX_MIN 0
#define TAB5_CAP_NX_MAX TAB5_PANEL_W
#define TAB5_CAP_NY_MIN 0
#define TAB5_CAP_NY_MAX TAB5_PANEL_H

// ---- I2C (M5GFX owns the bus; here for the record) -------------------------
#define TAB5_I2C_SDA    31
#define TAB5_I2C_SCL    32
#define TAB5_TP_INT     23
#define TAB5_PI4IOE1    0x43   // P4 LCD_RST, P5 TP_RST, P1 SPK_EN, P2 EXT5V_EN
#define TAB5_PI4IOE2    0x44   // P0 WLAN_PWR_EN, P3 USB5V_EN, P7 CHG_EN
#define TAB5_TOUCH_ADDR 0x55   // the ST7123's touch half
