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
        PETTED,       // the user tapped him directly — see hitTest()
    };

    // Call once from an event site (main.cpp) to make Squachy react.
    // lifetimeTotal (DETECTION only) is the engine's persisted
    // across-reboot detection count — used to fire milestone quips.
    void trigger(Event evt, DetectionType dt = DetectionType::UNKNOWN,
                 uint32_t lifetimeTotal = 0);

    // Hit test against wherever he was actually drawn last tick() call
    // (position/scale tracked internally) — call this before falling
    // through to any other touch handling on the CLEAR screen, so a
    // tap on him pets him instead of doing nothing.
    bool hitTest(int x, int y);

    // Draws Squachy and his speech bubble, and advances his idle
    // animation/quip timers. Call every tick from the CLEAR screen.
    // cx = horizontal center. topY = where the bubble row starts (just
    // below the title bar). availHeight = total vertical room from topY
    // down to wherever the caller's next element starts (e.g. the status
    // line) — Squachy scales himself up to fill it, so give him
    // everything that isn't needed for something else.
    void tick(TFT_eSPI& t, int cx, int topY, int availHeight, uint32_t now);

    // A lightweight cameo draw for screens that just want him standing
    // and waving (the boot splash) without the full idle/quip state
    // machine tick() drives. cx = horizontal center, baseY = where his
    // feet line up (e.g. just past the boot screen's horizon), scale
    // sizes him relative to his normal full size (1.0). line, if given,
    // shows in a static speech bubble above his head.
    void drawWaving(TFT_eSPI& t, int cx, int baseY, uint32_t now, float scale = 1.0f,
                    const char* line = nullptr);
}
