// SquachWatch-CYD — the CrowPanel 7's backlight, which is not a GPIO.
//
// There is no backlight pin on this board. An STC8H1K28 helper MCU at I2C
// 0x30 owns it, and is driven with a bare command byte — no register address.
// The scale is INVERTED: 0 is brightest, 244 is dimmest, 245 is off. Elecrow
// states the rest of the command space is undocumented, so nothing else is
// ever sent.
//
// This shares Wire with the GT911, so whoever calls Wire.begin() first wins
// and nobody may call Wire.end().
#pragma once
#include <stdint.h>

namespace CrowBL {
    // Probes for the helper. Wire must already be begun.
    bool begin();
    bool present();
    // level is the firmware's own 0..255 convention, 255 brightest.
    void set(uint8_t level);
}
