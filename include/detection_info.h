// SquachWatch-CYD — plain-language explanations of what each detection
// type actually is, shown from LOG's long-press menu ("MORE INFO").
// Kept separate from squachy.cpp (already large, and these are facts
// about the detection types themselves, not part of his personality
// system) even though they're presented through him on screen.
#pragma once
#include "state.h"

class DetectionEngine;

namespace DetectionInfo {
    // One paragraph per DetectionType, written to explain what the
    // thing actually is and why it's worth knowing about -- distinct
    // from squachy.cpp's DET_LINES, which are short in-the-moment
    // reaction quips, not explanations.
    const char* explain(DetectionType t);

    // Shown once, before the very first explain() a user ever asks
    // for (see Settings::infoPrimerShown()) -- what RSSI and
    // confidence actually mean, since every explain() screen assumes
    // that context already.
    const char* rssiConfidencePrimer();

    // explain() plus whatever the device has actually decoded about this
    // detection, where there is any.
    //
    // Only DRONE has any today: a Remote ID advert carries the aircraft's
    // serial, its position and the operator's, and reciting the generic
    // paragraph about what Remote ID is while holding all of that would be
    // a waste of the panel. Everything else falls straight through to
    // explain(), so callers can use this everywhere without asking.
    //
    // Lives here rather than in the LOG screen's caller so the emulator
    // gets it too -- main.cpp is not part of that build, and a panel that
    // can only be seen on hardware is a panel nobody checks.
    const char* explainLive(DetectionType t, const DetectionEngine& eng);

    // The MORE INFO page for one detection: its DEVICE's own page where
    // device_info.cpp has one -- Flipper Zero, not "wireless testing
    // hardware" -- else the type's paragraph, or for a drone what Remote ID
    // has decoded. titleFor() is the heading that goes with it.
    const char* explainFor(DetectionType t, const char* vendor, const char* name,
                           const DetectionEngine& eng);
    const char* titleFor(DetectionType t, const char* vendor, const char* name);
}
