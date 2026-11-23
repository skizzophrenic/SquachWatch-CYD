// SPDX-License-Identifier: MIT
// LC76G GNSS over I2C for the Waveshare ESP32-C5-Touch-LCD-3.5 (ws-c5-35-gps).
//
// The 3.5"'s header UART is not bonded to the chip, so a GPS cannot come in
// on a UART pin; its SDA/SCL do reach the chip, and Waveshare's LC76G speaks
// I2C. This is only the transport: each NMEA byte goes to the caller's sink
// (Gnss::feed in main.cpp), so the fix, the SD rows, the clock and GPS STATUS
// all work as they do on the UART boards.
//
// Ported from the ESP32-C5-BSP's shared LC76G driver (MIT), which follows
// Waveshare's LC76G_RPI_I2C demo. Runs from loop(), the same task as touch, so
// the bus needs no lock: one cycle is a few ms and never interleaves.
#pragma once
#if defined(WS_C5_35) && defined(CYD_GPS)
#include <stdint.h>

namespace WsGps {

void begin();
// One service step, self-throttled to ~2 Hz. Every NMEA byte read goes to
// sink(c, now). Returns the bytes read this call (0 when it did not run).
uint32_t tick(void (*sink)(char c, uint32_t now), uint32_t now);
uint32_t cycles();   // read cycles run
uint32_t fails();    // cycles the bus failed (no module answers: every one)

}  // namespace WsGps
#endif
