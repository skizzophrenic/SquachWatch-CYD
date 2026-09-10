// SquachWatch-CYD — the messages tutorial.
//
// Seven cards that walk through messaging with a pretend visitor, one tap at
// a time so there is time to read each one. It starts by itself the first time
// MESSAGES is switched on, and the "?" on the message screen replays it.
//
// It spans two screens -- the main screen, where the visitor stands and the
// speech bubble is, and the message screen, where a line is picked and sent --
// so the step lives here and each screen asks it what to draw and what a tap
// means.
//
// NOTHING IN IT TRANSMITS. The visitor is drawn from a local stand-in, SEND
// moves the lesson on without calling MeshTalk, and the reply is painted, not
// received. That is what makes it safe to run before anybody has a phrase, and
// it must stay that way: the whole premise of SquachMesh is that the radio does
// nothing the owner did not agree to.
#pragma once
#if SQUACH_MESH
#include <stdint.h>
#include <TFT_eSPI.h>
#include "squachmesh.h"

namespace MeshTutor {

enum class Step : uint8_t {
    OFF,
    VISIT,        // main screen: a Squachy has come to visit      (tap on)
    TAP_ICON,     // main screen: arrow at the speech bubble       (tap the bubble)
    PICK_LINE,    // message screen: arrow at a line               (tap a line)
    PRESS_SEND,   // message screen: arrow at SEND                 (tap SEND)
    REPLY,        // main screen: the pretend answer, in red       (tap on)
    PHRASE,       // main screen: what the phrase is and where     (tap on)
    PRIVACY,      // main screen: what others can still see        (tap to finish)
};
constexpr uint8_t STEPS = 7;

// The pretend visitor's name, and what it says back.
constexpr const char* DEMO_NAME  = "DEMO";
constexpr const char* DEMO_REPLY = "Thanks.";

void start();
void stop();                   // finished or skipped; the visitor walks off
bool active();
Step step();
void setStep(Step s);
void next();
// The steps that move on with a tap anywhere, rather than on one target.
bool waitsForTap();

// The stand-in visitor while the tutorial runs, else nullptr.
const SquachMesh::Peer* guest();

// The card: at the top of the screen or the bottom. It records where it and
// its SKIP corner landed, for cardTap().
void drawCard(TFT_eSPI& t, bool atTop);
enum class Tap : uint8_t { NONE, SKIP, CARD };
Tap  cardTap(int x, int y);

// A bobbing arrow whose tip points at (x, y), and a blinking frame around a
// target. Outlined in the ground colour, so both read over any background.
enum class Dir : uint8_t { UP, DOWN, LEFT, RIGHT };
void drawArrow(TFT_eSPI& t, uint32_t now, int x, int y, Dir d);
void drawFrame(TFT_eSPI& t, uint32_t now, int x, int y, int w, int h);

} // namespace MeshTutor
#endif
