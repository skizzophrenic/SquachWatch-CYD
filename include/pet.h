// SquachWatch-CYD — the pet.
//
// VAPOR SHAGGY, promoted from the cameo who crosses the flying toasters to
// something that turns up on the CLEAR screen, climbs Squachy, insults him
// and leaves. Drawn at four device pixels an art pixel rather than the
// cameo's two, which is the size where a speech bubble beside him reads as
// HIS rather than as one of Squachy's.
//
// "Pet" is the user-facing word. Internally this is the companion, because
// squachy.cpp already uses s_petCount for how many times you have STROKED
// him, and one file with both meanings of the word would be a trap.
#pragma once
#include <TFT_eSPI.h>

namespace Pet {
    // Called once per CLEAR-screen frame, after Squachy has been drawn --
    // he perches on top of him, so he has to go on top of him. Reads
    // Squachy::lastFootprint() for where the head actually is this frame,
    // which is why nothing here needs to know about bob or squash.
    //
    // Does nothing at all unless a pet is unlocked AND switched on, so the
    // call site does not need to check either.
    void tick(TFT_eSPI& t, uint32_t now, int screenW, int bandTop, int bandBottom);

    // Cancels whatever he is doing and puts him off screen. Used when the
    // screen changes underneath him, so he does not reappear mid-jump on a
    // screen that has been away for a minute.
    void reset();
}
