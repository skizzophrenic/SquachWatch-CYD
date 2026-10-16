// SquachWatch-CYD — the speaker, on the boards that have one.
//
// The two Freenove S3 boards carry an ES8311 codec on the touch controller's
// I2C bus and a small mono amplifier behind it, with a speaker plug on the
// edge; the Waveshare ESP32-C5-Touch-LCD-2.8 has the same codec, and the
// T-Deck Plus an I2S amp. Nothing else in the fleet has a speaker; this header is a set of
// no-ops everywhere else, so the pets and the mascot can ask for a sound
// without knowing whether anything is listening.
//
// Every sound is made on the fly from a short recipe (tones, a little voice,
// FM bells, filtered air, an echo), picked in the two Sound Labs of
// 2026-10-11 and 2026-10-14: no recordings in flash. The
// SOUND row in Settings is OFF (the default), ALERTS or ALL; VOLUME is LOW,
// MED or HIGH. The rules for when a sound may play are the CrowPanel
// buzzer's, applied to all of them: never with the screen dimmed, never at
// night (except a watch-list hit, which is what a watch list is for), never
// at boot, never through the security wipe. The alert sounds also follow the
// alert gate in main.cpp (alertMayInterrupt): a device the log never held.
#pragma once
#include <stdint.h>

// SQW_SPEAKER is more than the speaker: on the Freenove and T-Deck boards it
// also brings POWER OFF, the power diary and their use of GPIO0. SQW_SOUND is
// the speaker alone -- the Sound API, the SOUND rows -- and adds the Waveshare
// ESP32-C5-Touch-LCD-2.8, whose GPIO0 is its I2C bus's SDA.
#if defined(FREENOVE_S3) || defined(FREENOVE_S3_35) || defined(TDECK_PLUS)
#define SQW_SPEAKER 1
#else
#define SQW_SPEAKER 0
#endif
#if SQW_SPEAKER || defined(WAVESHARE_C5)
#define SQW_SOUND 1
#else
#define SQW_SOUND 0
#endif

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
        DRONE,            // ALERTS: a swoosh, a drone (Remote ID) goes by
        OINK,             // ALERTS: a pig's oink, an Axon body camera
        COUNT
    };

#if SQW_SOUND
    // Once in setup(), after the touch controller has started the I2C bus.
    // Finds the codec, sets it up for 16 kHz playback and parks the amplifier
    // off. Boots silent: nothing plays until something asks.
    void begin();
    // Ask for a sound. Returns at once; a small task renders and plays it.
    // Honours the SOUND setting, night and the hush below unless `sample` is
    // set, which is how the Settings rows let the owner hear each level.
    void play(Id id, bool sample = false);
    // The screen is dimmed by the saver: whoever let it dim wanted the board
    // quiet. Nothing plays while this is set.
    void hush(bool on);
    // The security wipe: the amplifier off and the queue dropped, now.
    void quiet();
    // Whether the codec answered at begin(); the SYSTEM page reads it.
    bool present();
    // The bench: flip the amplifier enable's polarity if the schematic lied.
    void ampLow(bool activeLow);
#else
    inline void begin() {}
    inline void play(Id, bool = false) {}
    inline void hush(bool) {}
    inline void quiet() {}
    inline bool present() { return false; }
    inline void ampLow(bool) {}
#endif
}
