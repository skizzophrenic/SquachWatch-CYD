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

    // Loads a previously-saved calibration into `out`. Returns false
    // (leaving `out` untouched) if none has been saved yet -- caller
    // should keep its compiled-in defaults in that case.
    bool load(Cal& out);

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
    // hold on each (averaging a few samples for stability), computes
    // the resulting Cal, saves it, and returns it. Blocking -- this
    // has the user's full attention by definition, so a simple
    // synchronous loop is simpler than threading it through the
    // caller's normal state machine.
    Cal runInteractive(TFT_eSPI& t, RawReader readRaw,
                       uint16_t bg, uint16_t fg, uint16_t accent);
}
