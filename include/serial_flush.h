// SquachWatch-CYD -- Serial.flush() that cannot hang the board.
//
// On the ESP32-S3 boards (SQW_S3) the console is the chip's native USB. Its flush()
// spins until the host has read every byte, and with nothing listening on
// the port that is never: the first-boot calibration filled the buffer and
// the board sat in setup() on a black screen for as long as the port stayed
// closed (2026-09-22). Flush only when a host is actually there.
#pragma once
#include <Arduino.h>
static inline void serialFlush() {
#if defined(SQW_S3)
    if (Serial) Serial.flush();
#else
    Serial.flush();
#endif
}
