// SquachWatch-CYD — plain-language explanations of what each detection
// type actually is, shown from LOG's long-press menu ("MORE INFO").
// Kept separate from squachy.cpp (already large, and these are facts
// about the detection types themselves, not part of his personality
// system) even though they're presented through him on screen.
#pragma once
#include "state.h"

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
}
