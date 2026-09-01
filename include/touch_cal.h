// SquachWatch-CYD — persistent touch calibration
// Touch-type-agnostic: works for either the resistive XPT2046 or the
// capacitive CST816/820, since both reduce to the same shape of
// problem -- two raw axes, each with a min (one screen edge) and max
// (the opposite edge) value. The caller supplies a raw-sample reader
// and interprets the returned Cal according to its own known raw-axis
// layout (see main.cpp for how each touch type maps aTop/aBottom/
// bLeft/bRight onto its existing map() calls).
#pragma once
#include <TFT_eSPI.h>
#include <stdint.h>

namespace TouchCal {
    struct Cal {
        int16_t aTop, aBottom;   // raw "a" axis at the top row vs. bottom row
        int16_t bLeft, bRight;   // raw "b" axis at the left column vs. right column
    };

    // True and fills a/b with a fresh raw sample if a finger is
    // currently down, false otherwise -- same shape as each project's
    // own touch poll, just reduced to the two raw axis values.
    typedef bool (*RawReader)(int16_t& a, int16_t& b);

    // minSpread (load/runInteractive below): the minimum acceptable
    // |a1-a2| / |b1-b2| raw-unit spread for a calibration to count as
    // valid. This has to come from the caller rather than being a
    // fixed constant in here -- capacitive and resistive touch have
    // very different legitimate raw ranges (capacitive: often only a
    // few hundred units end to end; resistive: up to a 12-bit ADC's
    // 4095), so one universal threshold can only ever be tuned for the
    // smaller of the two. Confirmed on real hardware: a resistive
    // calibration with a spread of ~180-190 (nowhere near a real
    // full-range calibration) still passed a threshold loose enough
    // not to break capacitive -- pass a much stricter value for
    // resistive (main.cpp keeps board-appropriate constants for this).

    // Loads a previously-saved calibration into `out`. Returns false
    // (leaving `out` untouched) if none has been saved, or if what's
    // saved doesn't pass the spread check -- caller should keep its
    // compiled-in defaults in that case.
    bool load(Cal& out, int16_t minSpread);

    // Saves a calibration so it survives reboots.
    void save(const Cal& cal);

    // Erases a saved calibration — the recovery path for a bad one
    // that makes touch too inaccurate to reliably hit a calibrate
    // button. Caller should offer this as a boot-time gesture (hold
    // touch anywhere for ~1s right after boot) that needs no
    // precision, since precision is exactly what's missing when this
    // is needed.
    void reset();

    // Runs the interactive 4-corner calibration flow: draws a
    // crosshair near each screen corner in turn, waits for a tap-and-
    // hold on each (averaging a few samples for stability), and
    // computes the resulting Cal. Blocking -- this has the user's full
    // attention by definition, so a simple synchronous loop is simpler
    // than threading it through the caller's normal state machine.
    // Returns false (leaving `out` untouched, and saving nothing) if
    // the result doesn't pass the spread check -- caller should keep
    // whatever calibration it already had rather than apply or persist
    // known-bad data; the user can just long-press to try again.
    bool runInteractive(TFT_eSPI& t, RawReader readRaw,
                        uint16_t bg, uint16_t fg, uint16_t accent,
                        Cal& out, int16_t minSpread);
}
