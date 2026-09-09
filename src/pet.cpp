// SquachWatch-CYD — the pet. See pet.h.
#include "pet.h"
#include "theme.h"
#include "squachy.h"
#include "lil_guy.h"
#include <math.h>
#include <stdlib.h>

namespace Pet {

// ---- his material -------------------------------------------------------
// Kept short on purpose. The bubble sits beside a forty-pixel sprite, and
// anything much past thirty characters stops looking like it came out of
// him and starts looking like a caption.
static const char* const QUIPS[] = {
    "nice shades",
    "how do you even walk",
    "you make a good chair",
    "great view up here",
    "i live here now",
    "this is my throne",
    "i can see my house",
    "he's with me",
    "still scanning?",
    "scan me, coward",
    "untrackable",
    "flock who?",
    "i'm not on any list",
    "we're the surveillance now",
    "watch this",
    "geronimo",
    "i meant to do that",
    "nailed it",
    "some cryptid you are",
    "i'd hide better",
    "seen you on a poster",
    "the 80s called",
};
static const uint8_t QUIPS_N = sizeof(QUIPS) / sizeof(QUIPS[0]);

// ---- the arc ------------------------------------------------------------
// One parabola, paused at the top. That is the whole trick, and it is why
// he comes down exactly the way he went up: the ascent and the descent are
// two halves of the SAME flight under the same constant, not two animations
// that happen to look similar. Freezing at the apex to sit and talk does not
// change either half.
//
// G is in pixels per millisecond squared. At this value a jump to a typical
// head height takes a little under half a second, which is quick enough to
// read as a leap rather than a float.
static const float    G        = 0.0018f;
static const uint32_t PERCH_MS = 3200;      // long enough to read the line
// One visit in four ends up on his head. The climb is the best thing he
// does and it was happening every single time, which is the fastest way to
// make a good gag ordinary -- by the third viewing it is a cutscene. The
// other three visits he just walks on, insults him, and leaves.
static const int      PERCH_ODDS = 4;
// Four now, which is what was wanted all along. It was three while the
// title bar existed: standing on Squachy's crown there were only about 28
// pixels above his skull, and a 40-tall sprite had to either lose a third
// of itself behind the bar or sink its feet to his eyes. The bar is gone,
// those rows are the background's, and he fits at full size.
static const int      SCALE    = 3;
static const int      SPR      = LILGUY_W * SCALE;   // 40 across and tall
static const float    RUN_PXMS = 0.075f;    // 75 px a second, a trot

// HECKLE is the common visit and PERCH is the rare one. He turns up, stops
// beside Squachy, says his piece and carries on; only occasionally does he
// bother climbing. Nothing about the jump changed -- it just stopped being
// the only thing he does, which is what made it stop reading as a routine.
enum class Phase : uint8_t { AWAY, RUN_IN, HECKLE, UP, PERCH, DOWN, RUN_OUT };

static Phase     s_phase   = Phase::AWAY;
static uint32_t  s_nextAt  = 0;             // when he next turns up
static uint32_t  s_at      = 0;             // when the current phase began
// Decided once, on arrival, so every phase after it agrees about where this
// visit is going.
static bool      s_willPerch = false;
static bool      s_fromLeft= true;
static uint8_t   s_quip    = 0;
static float     s_x       = -100.0f;
static float     s_y       = 0.0f;
// Set when the jump starts and reused by both halves, so the descent cannot
// drift from the ascent even if the head moves under him mid-flight.
static float     s_launchX = 0.0f, s_landX = 0.0f, s_groundY = 0.0f;
static float     s_apexY   = 0.0f, s_tHalf = 0.0f;
// Where he actually was when he stepped off, which is not where he landed:
// he rides the bob while perched, so the drop starts from wherever the head
// happened to be at that instant.
static float     s_dropX   = 0.0f, s_dropY = 0.0f;

void reset() {
    s_phase  = Phase::AWAY;
    s_x      = -100.0f;
    s_nextAt = 0;
}

// A small bubble of his own rather than Squachy's. His is drawn at text
// size 1 with a hard edge and no tail curve -- it should read as an
// interruption, not as the device speaking.
static void bubble(TFT_eSPI& t, int x, int y, int screenW, const char* s) {
    t.setTextSize(1);
    const int w = t.textWidth(s) + 8;
    const int h = t.fontHeight() + 5;
    if (x + w > screenW - 2) x = screenW - 2 - w;
    if (x < 2) x = 2;
    t.fillRect(x, y, w, h, Theme::BG);
    t.drawRect(x, y, w, h, Theme::VAPOR_PINK);
    t.setTextColor(Theme::WHITE, Theme::BG);
    t.setCursor(x + 4, y + 3);
    t.print(s);
}

void tick(TFT_eSPI& t, uint32_t now, int screenW, int bandTop, int bandBottom) {
    if (!Squachy::petUnlocked() || !Squachy::petEnabled()) { s_phase = Phase::AWAY; return; }

    // Where Squachy actually is THIS frame. Everything below hangs off
    // this rather than off constants, so bob, squash and his idle amble
    // all come along for free.
    int cx, halfW, top, bot;
    if (!Squachy::lastFootprint(cx, halfW, top, bot)) return;

    // Not while he is being carried or is dangling from something. A pet
    // perching on a head that is itself flying through the air reads as a
    // bug rather than as a joke.
    if (Squachy::isHeld()) { if (s_phase != Phase::AWAY) reset(); return; }

    // lastFootprint() gives the floor and the width, but NOT the head: its
    // `top` is a generous, un-bobbed hit box sitting twenty scaled units
    // above his crest, and perching on that launched him off the screen.
    // crownY() is the head as actually drawn this frame, which is also what
    // makes riding the bob free.
    (void)top;
    const float ground = (float)(bot - SPR);                       // feet on the floor
    const float crown  = (float)Squachy::crownY();                 // top of his head
    // Feet exactly on the crown. No fudge needed now that this is the real
    // drawn head rather than a hit box: his crest spikes rise either side.
    // Feet a little INTO the fur, not balanced on the skull line.
    //
    // crownY() is the top of the head BOX. Squachy's cowlick rises another
    // seven of his units above that, which at his current size is about
    // twenty pixels -- so standing exactly on the crown put the pet in a
    // trough with the tufts either side of him, looking like he had fallen
    // in rather than climbed up. Sinking him three of Squachy's units puts
    // his boots in the fur with the cowlick beside his shoulders.
    //
    // Three of SQUACHY's units, not three pixels: halfW is 24 of them, so
    // this tracks him through every scale change and every costume that
    // resizes him, the same way the rest of this file hangs off
    // lastFootprint rather than off constants.
    const float sqScale = (float)halfW / 24.0f;
    float       headY   = crown + 3.0f * sqScale - (float)SPR;

    // No clamp. There used to be one holding headY at 0 so he could not go
    // above the band, and it was the whole bug: Squachy has grown enough
    // that his crown sits about 26 rows down, a 40-tall pet could never
    // reach it, and the clamp quietly parked the pet 14 rows inside his
    // skull instead. Worse, it re-clamped every frame as Squachy bobbed, so
    // the pet sank and rose independently of the head he was standing on --
    // which is exactly the not-attached look.
    //
    // He is 30 tall now rather than 40 for the same reason. There is only
    // so much sky above a character this size, and a sprite that cannot fit
    // in it has to either shrink or be cropped. At 30 with the sink above he
    // is fully on screen at rest and loses a few rows of hair at the top of
    // a bounce, which is the same bargain Squachy's own tall hats take.

    switch (s_phase) {
    case Phase::AWAY: {
        if (!s_nextAt) s_nextAt = now + 8000u;      // first appearance, after a beat
        if (now < s_nextAt) return;
        // Random side every time, so he is not a metronome.
        s_fromLeft = (random(0, 2) == 0);
        s_quip     = (uint8_t)random(0, QUIPS_N);
        s_willPerch = (random(0, PERCH_ODDS) == 0);
        s_x        = s_fromLeft ? -(float)SPR : (float)screenW;
        s_y        = ground;
        s_phase    = Phase::RUN_IN;
        s_at       = now;
        return;
    }
    case Phase::RUN_IN: {
        // He runs to a launch point one sprite-width clear of Squachy, on
        // whichever side he came from.
        const float target = s_fromLeft ? (float)(cx - halfW - SPR)
                                        : (float)(cx + halfW);
        s_x += (s_fromLeft ? RUN_PXMS : -RUN_PXMS) * (float)(now - s_at);
        s_at = now;
        s_y  = ground;
        if ((s_fromLeft && s_x >= target) || (!s_fromLeft && s_x <= target)) {
            s_x = target;
            // Most visits stop here and talk from the floor.
            if (!s_willPerch) { s_phase = Phase::HECKLE; s_at = now; break; }
            // Lock the flight in now. Apex height and half-time come out of
            // G, so the two halves are the same parabola by construction.
            s_launchX = s_x;
            s_landX   = s_fromLeft ? (float)(cx + halfW) : (float)(cx - halfW - SPR);
            s_groundY = ground;
            s_apexY   = headY;
            const float dh = s_groundY - s_apexY;
            s_tHalf   = (dh > 1.0f) ? sqrtf(2.0f * dh / G) : 1.0f;
            s_phase   = Phase::UP;
            s_at      = now;
        }
        break;
    }
    case Phase::HECKLE: {
        // Standing still beside him, on the ground he ran in on. He holds
        // the same beat the perch does, so the line gets the same time to be
        // read whichever way he delivered it.
        s_y = ground;
        if (now - s_at >= PERCH_MS) { s_phase = Phase::RUN_OUT; s_at = now; }
        break;
    }
    case Phase::UP: {
        const float k = (float)(now - s_at) / s_tHalf;   // 0 at launch, 1 at apex
        if (k >= 1.0f) { s_x = (s_launchX + s_landX) * 0.5f; s_y = s_apexY;
                         s_phase = Phase::PERCH; s_at = now; break; }
        const float tt = k * s_tHalf;
        // Straight kinematics: rise = v0*t - 0.5*G*t^2, with v0 set so the
        // apex lands exactly on his head.
        const float v0 = G * s_tHalf;
        s_y = s_groundY - (v0 * tt - 0.5f * G * tt * tt);
        s_x = s_launchX + (( (s_launchX + s_landX) * 0.5f) - s_launchX) * k;
        break;
    }
    case Phase::PERCH: {
        // Glued to the crown rather than frozen where he landed. headY and
        // cx are recomputed from lastFootprint() every frame anyway, so
        // riding the bob and the squash exactly costs nothing -- the values
        // were being thrown away. Horizontal too: Squachy ambles, and
        // following his bob but not his drift would leave the pet hovering
        // beside his head, which is worse than not following at all.
        s_x = (float)(cx - SPR / 2);
        s_y = headY;
        if (now - s_at >= PERCH_MS) {
            // The drop starts from HERE, and the landing is worked out from
            // where Squachy is now rather than where he was when the jump
            // began. The fall time is re-derived from the same G, so "the
            // same gravity he went up with" still holds -- only the height
            // it is solving for has changed.
            s_dropX = s_x;
            s_dropY = s_y;
            s_landX = s_fromLeft ? (float)(cx + halfW) : (float)(cx - halfW - SPR);
            const float dh = ground - s_dropY;
            s_tHalf = (dh > 1.0f) ? sqrtf(2.0f * dh / G) : 1.0f;
            s_groundY = ground;
            s_phase = Phase::DOWN;
            s_at = now;
        }
        break;
    }
    case Phase::DOWN: {
        // The same half-parabola, played the other way: from rest at the
        // apex, falling under the same G for the same s_tHalf. Nothing here
        // is tuned separately from the way up.
        const float tt = (float)(now - s_at);
        if (tt >= s_tHalf) { s_y = s_groundY; s_x = s_landX;
                             s_phase = Phase::RUN_OUT; s_at = now; break; }
        s_y = s_dropY + 0.5f * G * tt * tt;
        s_x = s_dropX + (s_landX - s_dropX) * (tt / s_tHalf);
        break;
    }
    case Phase::RUN_OUT: {
        s_x += (s_fromLeft ? RUN_PXMS : -RUN_PXMS) * (float)(now - s_at);
        s_at = now;
        s_y  = ground;
        if (s_x > (float)screenW + 4.0f || s_x < -(float)SPR - 4.0f) {
            s_phase  = Phase::AWAY;
            // 45 to 90 seconds. The behaviour is the joke; the rarity is
            // what keeps it one.
            s_nextAt = now + 45000u + (uint32_t)random(0, 45000);
        }
        break;
    }
    }

    if (s_phase == Phase::AWAY) return;
    if (s_y + SPR > (float)bandBottom) s_y = (float)(bandBottom - SPR);

    // He travels the same way for the whole visit -- in, up, over, down and
    // out again -- so the facing is settled once, by the side he arrived
    // from. Without this he ran in forwards and left backwards.
    Theme::drawLilGuy(t, (int)s_x, (int)s_y + SPR, now, SCALE, !s_fromLeft);
    // Beside him, not above. Perched on the crown he is already as high as
    // the band goes, so a bubble over his head lands behind the title bar --
    // which is where the whole joke went the first time. Level with him, and
    // on whichever side has the room.
    if (s_phase == Phase::PERCH || s_phase == Phase::HECKLE) {
        t.setTextSize(1);
        const int bw = t.textWidth(QUIPS[s_quip]) + 8;
        const int bx = ((int)s_x + SPR + 4 + bw <= screenW - 2)
                       ? (int)s_x + SPR + 4
                       : (int)s_x - bw - 4;
        // Beside him on the head, ABOVE him on the ground.
        //
        // Perched on the crown he is already as high as the band goes, so a
        // bubble over his head lands behind the corner buttons -- which is
        // where the whole joke went the first time this was wired up. Level
        // with him is the only place that is always safe up there.
        //
        // On the floor the opposite is true: level with him puts the bubble
        // straight through the SOMETHING'S NEARBY headline, which is drawn
        // after the pet and would paint over the line he came to say. Above
        // his head there is nothing but Squachy, and a speech bubble in
        // front of him reads exactly as intended.
        int by = (s_phase == Phase::PERCH) ? (int)s_y + 6
                                           : (int)s_y - 18;
        if (by < 22) by = 22;
        (void)bandTop;
        bubble(t, bx, by, screenW, QUIPS[s_quip]);
    }
}

}  // namespace Pet
