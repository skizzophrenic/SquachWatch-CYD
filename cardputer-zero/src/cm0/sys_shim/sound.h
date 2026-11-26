// SquachWatch-CM0 -- a shadow of the firmware's include/sound.h.
//
// The firmware's own gate enables the speaker only on its four speaker
// boards and never under SQUACHSIM -- and this build is SQUACHSIM, because
// the keyboard harness needs it (Makefile). So the firmware's gate can
// never fire here; the header is shadowed instead (this directory comes
// first on the include path, the same way as esp_random.h and esp_system.h) and
// the speaker is on unconditionally. The Cardputer Zero's ES8389 is driven
// over an ALSA card, not the firmware's I2S + ES8311 path: the I2S, I2C
// and FreeRTOS calls the unmodified sound.cpp makes are answered by the
// shims in this directory (driver/i2s.h, Wire.h, freertos_shim.h) and by
// i2s_alsa.cpp (src/cm0).
//
// The Sound::Id enum and the API below are a VERBATIM copy of the
// firmware's include/sound.h: `make sound-enum-check` diffs the two
// blocks and fails the build if the firmware's list drifts.
#pragma once
#include <stdint.h>

#define SQW_SPEAKER 1

namespace Sound {
    enum class Id : uint8_t {
        NEW_DEVICE = 0,   // ALERTS: three climbing blips
        WATCH_HIT,        // ALERTS: the Bigfoot whoop, twice, with an echo
        LOW_BATTERY,      // ALERTS: a toy winding down, once per dip under 10%
        MESSAGE,          // ALL: two glass chimes, a squad message
        VISITOR,          // ALL: knock knock, a visitor walks in
        THROW,            // ALL: wheee, Squachy or a pet in the air
        LAND,             // ALL: splat, the first bounce off the floor
        CLIPPY,           // ALL: a doorstop spring, when C1iPPY speaks
        TOAST_POP,        // ALL: ptoing, a slice pops
        PLUG_IN,          // ALL: a warm swoop up, the lamp sees the cable
        UNPLUG,           // ALL: a warm fall, the cable comes out
        LAND_TOASTER,     // ALL: T0@$TY's landing, a tin clank and a rattle
        LAND_YETI,        // ALL: the yeti's landing, a grumpy "OOF"
        HEADS_UP,         // ALERTS: one Bigfoot whoop, a squadmate's serious catch
        WD_FIX,           // WARDRIVE SOUND: two rising beeps, the GPS has a fix
        WD_LOST,          // WARDRIVE SOUND: a falling slide, the fix is gone
        WD_TICK,          // WARDRIVE SOUND: a soft tick, new rows written
        COUNT
    };

    void begin();
    void play(Id id, bool sample = false);
    void hush(bool on);
    void quiet();
    bool present();
    void ampLow(bool activeLow);
}
