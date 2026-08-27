// SquachWatch-CYD — Squachy, the main character
// A small animated mascot with a speech bubble, drawn on the idle
// (CLEAR) screen. Reacts to detections/buttons/rotation, and cracks
// jokes on his own when nothing else is happening.
#pragma once
#include <TFT_eSPI.h>
#include "state.h"

namespace Squachy {
    enum class Event {
        DETECTION,    // pass the DetectionType that triggered it
        LOG_OPENED,
        LOG_CLEARED,
        ROTATED,
        BOOTED,
    };

    // Call once from an event site (main.cpp) to make Squachy react.
    // lifetimeTotal (DETECTION only) is the engine's persisted
    // across-reboot detection count — used to fire milestone quips.
    void trigger(Event evt, DetectionType dt = DetectionType::UNKNOWN,
                 uint32_t lifetimeTotal = 0);

    // Draws Squachy and his speech bubble, and advances his idle
    // animation/quip timers. Call every tick from the CLEAR screen.
    // cx = horizontal center. topY = where the bubble row starts (just
    // below the title bar). availHeight = total vertical room from topY
    // down to wherever the caller's next element starts (e.g. the status
    // line) — Squachy scales himself up to fill it, so give him
    // everything that isn't needed for something else.
    void tick(TFT_eSPI& t, int cx, int topY, int availHeight, uint32_t now);
}
