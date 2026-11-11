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
    // 2 on the large panels: the pets live in a half-size world that the
    // caller doubles onto the screen (see pet.cpp's zoom). Positions in and
    // out of this module stay in SCREEN pixels either way; tick()'s
    // arguments are the exception, in the pets' own (layer) pixels.
    void setZoom(int k);
    int  zoom();
    // Whether a squad crowd is on screen this frame: M3@ty steps down a size
    // so he doesn't stand in front of them.
    void setCrowd(bool on);
    // The line Squachy's feet stand on, in screen pixels. The pets stand on
    // their own line above it (the old counter line); M3@ty stands here.
    void setGround(int y);
    // Whether tick() can draw anything this frame (a pet earned and chosen,
    // or the ball out). The caller skips the double-size layer when not.
    bool anyOut(uint32_t now);

    // Cancels whatever he is doing and puts him off screen. Used when the
    // screen changes underneath him, so he does not reappear mid-jump on a
    // screen that has been away for a minute.
    void reset();
    bool mothmanTap(int x, int y, uint32_t now);
    void drawMothman(TFT_eSPI& t, int x, int y, int scale, uint32_t now, bool powered = false);
    void drawMothmanPerch(TFT_eSPI& t, int x, int y, uint32_t now);

    // C1iPPY. A catch he should have an opinion about (a DetectionType);
    // whether a tap landed on him; a poke; and where he is, for the cursor on
    // the boards with no touch panel. All harmless when he is not the pet.
    void noteCatch(uint8_t type);
    bool clippyHit(int x, int y);
    void clippyPoke(uint32_t now);
    bool clippyCenter(int& x, int& y);
    // The pet as drawn this frame (C1iPPY, T0@$TY, MEATY or the ball), top left, in
    // the pet's own coordinates; false when none is on screen. For the
    // emulator's motion checks.
    bool drawnAt(int& x, int& y);
    // The balloons a pet asked for while drawing into the 3.5in's doubled
    // layer, drawn now at the screen's own size. Call after the layer is
    // laid down; does nothing when none is waiting.
    void drawHeldBalloons(TFT_eSPI& t);
    // A pet's balloon saying `text`, its tail on a box at (x, y) w wide: the
    // FONTS page's preview of PETS.
    void drawSampleBalloon(TFT_eSPI& t, int x, int y, int w, const char* text);
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

    // The yeti's dances (pet_yeti.inc): 0 SATURDAY NIGHT YETI, 1 THE FLOSS.
    // He dances it now if he is standing there, otherwise he comes on next
    // and dances it. The console's YETI DISCO / YETI FLOSS and the emulator.
    void yetiDance(int8_t which);

    // MEATY's shapes on demand (pet_meaty.inc): 0 the igloo, 1 the hot dog,
    // 2 Shake it, 3 three of him. The console's MEATY IGLOO and the rest, and
    // the emulator.
    void meatyAct(int8_t which);
}
