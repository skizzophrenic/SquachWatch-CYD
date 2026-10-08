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

    // C1iPPY. A catch he should have an opinion about (a DetectionType);
    // whether a tap landed on him; a poke; and where he is, for the cursor on
    // the boards with no touch panel. All harmless when he is not the pet.
    void noteCatch(uint8_t type);
    bool clippyHit(int x, int y);
    void clippyPoke(uint32_t now);
    bool clippyCenter(int& x, int& y);
    // Picking him up. A touch that lands on him grabs him at once; every
    // frame it is held drags him; letting go is a poke when the finger never
    // went anywhere, a throw when it was still moving, and a drop otherwise.
    void clippyGrab(int x, int y, uint32_t now);
    void clippyDrag(int x, int y, uint32_t now);
    void clippyRelease(uint32_t now);

    // The cell block (pet_jail.inc): the arrest itself, sirens and all. Ten
    // flings of a pet inside a minute call it; so do the console's JAIL and
    // the emulator.
    void arrest(uint32_t now);
}
