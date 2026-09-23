// SquachWatch-CYD — the CrowPanel 7's buzzer, behind the STC8 helper MCU.
//
// A passive buzzer the helper drives on command: STC8_BUZZ_ON (246) to I2C
// 0x30 switches it on, STC8_BUZZ_OFF (247) off, and it stays where it was
// last put. Both are one I2C write on the drawing thread, so a chirp is an
// "on" now and an "off" from tick() when the time is up -- never a delay().
//
// Off by default, and switched on it has exactly one job: one chirp for a
// device this board has not seen before. The rules, all four of them:
//
//   1. A new device only. A device that went quiet and came back gets the
//      alert card again, because it is news to the screen; it is not news
//      to the room, so it gets no chirp.
//   2. Not while the power saver has dimmed the screen. Whoever let it dim
//      wanted the board quiet, in every sense.
//   3. Not at night: the hours the alert card calls AT NIGHT, eleven to
//      five by a clock that has been set.
//   4. Never at boot, and never through the security wipe. begin() only
//      makes sure the buzzer is silent, and a duress restart has to sound
//      like any other restart, which is to say not at all.
//
// Nothing here decides WHEN to chirp. The one place that does is
// alertMayInterrupt() in main.cpp, the gate every automatic announcement
// passes and no manual one does. The SETTINGS row chirps once on being
// switched on, so the owner hears the sound before a camera earns it; that
// is the only chirp outside a sighting.
#pragma once
#include <stdint.h>

namespace CrowBuzzer {
    // Once in setup(), after the touch controller is up (the bus is
    // running and the helper has answered by then). Sends OFF and nothing
    // else: the helper is its own MCU and keeps its state across an ESP32
    // reset, so a crash or a watchdog mid-chirp would otherwise leave the
    // buzzer sounding until the next chirp happened to end it. No boot beep.
    void begin();
    // Start a chirp of `ms` milliseconds (clamped to 20..500). A chirp that
    // is already sounding is extended, not doubled. An ON the helper does
    // not acknowledge is dropped: nothing is sounding, so nothing is timed.
    void chirp(uint16_t ms);
    // Switches the buzzer off once its time is up. From loop(), every pass,
    // in every state, so the OFF lands whatever screen the alert opened.
    // An OFF the helper does not acknowledge is sent again next pass.
    void tick();
    // Off now, whatever was running. The security wipe calls it.
    void quiet();
    bool sounding();
}
