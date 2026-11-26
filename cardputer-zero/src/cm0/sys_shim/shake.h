// SquachWatch-CM0 -- a shadow of the firmware's include/shake.h.
//
// The firmware's header gives every SQUACHSIM build inline no-ops ("the
// browser emulator has no IMU") -- and this build is SQUACHSIM for the
// keyboard harness (Makefile). The Cardputer Zero does have the BMI270, so
// this directory (first on the include path) shadows the header with the
// real API, implemented over Linux IIO by src/cm0/shake_iio.cpp.
//
// The namespace block below is a VERBATIM copy of the firmware's non-
// SQUACHSIM branch: scripts/shake_sync_check.sh (run by `make test`) diffs
// it, and fails when an upstream bump changes the API.
#pragma once
#include <stdint.h>

namespace Shake {
    // WIGGLE is a lighter shake, only for waking a dimmed screen: someone
    // picking the fob up and jiggling it to see it. SHAKE is a real one.
    enum class Event : uint8_t { NONE, SHAKE, TAP, WIGGLE };
    // Finds the chip on Wire (already begun by the board's own setup),
    // uploads its configuration and starts the accelerometer. False if there
    // is no BMI270 answering; poll() then always says NONE.
    bool begin();
    bool present();
    // Call every loop. At most one event per call.
    Event poll(uint32_t now);
    // IMU on the console: every excursion from 1 g, for tuning.
    void setTrace(bool on);
}
