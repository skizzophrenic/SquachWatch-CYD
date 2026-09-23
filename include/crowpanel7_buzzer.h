// SquachWatch-CYD — the CrowPanel 7's buzzer, behind the STC8 helper MCU.
//
// A passive buzzer the helper drives on command: byte 246 to I2C 0x30
// switches it on, 247 off. Both are one I2C write on the drawing thread, so
// a chirp is an "on" now and an "off" from tick() when the time is up --
// never a delay(). Nothing here decides WHEN to chirp; that is the alert
// setting's business (see the SETTINGS row and the first-sighting hook).
#pragma once
#include <stdint.h>

namespace CrowBuzzer {
    // Start a chirp of `ms` milliseconds (clamped to 20..500). A chirp that
    // is already sounding is extended, not doubled.
    void chirp(uint16_t ms);
    // Switches the buzzer off once its time is up. From loop(), every frame.
    void tick();
    // Off now, whatever was running (screen dimming, the security lock).
    void quiet();
    bool sounding();
}
