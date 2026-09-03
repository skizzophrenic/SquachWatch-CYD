// SquachWatch-CYD — rare decorative idle-screen flourishes
#pragma once
#include <TFT_eSPI.h>
#include <stdint.h>

namespace IdleEvents {
    // Rare, purely-decorative overlay events for the CLEAR screen -- a
    // UFO flyby, a burst of stardust sparkles, a bird/bat silhouette
    // crossing the screen, or a brief glitchy one-liner. Picked at
    // random on a long, jittered timer (roughly every 1-3 minutes) and
    // played for a couple of seconds on top of whatever's already
    // drawn (background/Squachy) -- same "draw on top, let next
    // frame's full repaint erase it" pattern the rest of CLEAR already
    // relies on, so this needs no erase logic of its own. Deliberately
    // background-independent (unlike the 10 Settings > BACKGROUND
    // styles) so they show up no matter which one is active -- see the
    // design discussion this shipped from.
    //
    // Call once per real frame from CLEAR only, right after
    // Squachy::tick() so an event can draw over him too, same as he
    // draws over the background. [x0,y0]-[x1,y1] is the same region
    // CLEAR passes its background draw calls (titleBottom..countersTop
    // vertically, the full screen width). advance: same two-pass
    // convention Squachy::tick() uses (CYD35's half-height dual
    // render) -- only pass true on exactly one of the two calls per
    // real frame; the actual drawing runs every call so both bands
    // still get painted. The caller is expected to skip calling this
    // entirely during onboarding or "boring mode", the same way it
    // already skips Squachy::tick() for boring mode.
    void tick(TFT_eSPI& t, uint32_t now, int x0, int y0, int x1, int y1, bool advance);
}
