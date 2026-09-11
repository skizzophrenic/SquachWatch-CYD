// SquachWatch-CYD — emotes as scripts: the pure half.
//
// The six original emotes are set pieces with their own code in ui_clear.cpp,
// three of them shared with the clock that starts pieces on its own. The other
// thirty are DATA: a handful of beats each, and every beat says what the two of
// them do for how long, who says what, and what flies about. One small engine
// in ui_clear.cpp plays any of them. Thirty hand-written pieces would have been
// thirty copies of the same timing code, and roughly five times the flash.
//
// Beats are written from the SENDER's side: A sent it, B received it. On the
// sending board A is the host, on the other board A is the visitor, so the two
// screens tell the same story from their two sides -- the same rule the
// original pieces follow.
//
// Everything here is plain tables and arithmetic, so it compiles and is tested
// on a desktop (test/emote_script_test.cpp).
#pragma once
#if SQUACH_MESH
#include <stdint.h>
#include <stddef.h>
#include "meshmsg.h"

namespace EmoteScript {

// What one of them is doing for a beat. ui_clear.cpp maps these onto
// Squachy::VisitPose; kept apart so this file needs no display to compile.
enum class Pose : uint8_t {
    NONE, LAUGH,
    HIGH_FIVE, LOW_FIVE, FIST, PUMP, DANCE, SLEEPY, STRETCH,   // poses he already had
    COVER, LOOK_AROUND, HANDS_UP,                              // his detection reactions
    SALUTE, BOW, HUG, SAD, GRR, CROUCH, PULL, WIGGLE, CHEER,   // new for emotes
    SELFIE, HOWL, POINT, STRAIN,
    COUNT
};

// What is drawn over the two of them during a beat.
enum class Fx : uint8_t {
    NONE,
    SPARK,      // where two hands meet; arg: 0 up, 1 down, 2 level
    THROW,      // an Obj, from one of them to the other's head
    LOB,        // an Obj, from one hand to the other's -- a gentle toss
    HIT,        // a Burst at the target's head
    HOLD,       // an Obj in one hand
    HEARTS,     // hearts drifting from one to the other
    STEAM,      // puffs off one head
    WAVES,      // sound rings off one head
    FLASH,      // a camera flash, then the photo's frame
    HATS,       // tinfoil; arg 1: both of them, 0: only A
    CAMERA,     // a camera up above, its light blinking
    ICON,       // the detection type in the setup byte, with a "?!"
    COIN,       // tossed from A's hand; arg 1: landed, showing its face
    DICE,       // arg 0: tumbling, 1: landed, showing the setup's faces
    TABLE,      // an arm-wrestling table; arg 1: somebody has won
    ROPE,       // a rope between them; arg 1: somebody has won
    CUPS,       // a cup in each of their hands; arg 1: the clink
    CONFETTI,   // falling over both of them
    FIREWORKS,  // bursting in the sky
    HAHA,       // "HA"s popping off one head
    BONK,       // stars circling one head
    COUNT
};
// What THROW, LOB and HOLD carry.
enum class Obj : uint8_t { PIE, BALLOON, PLANE, PILLOW, GIFT, PIZZA, COUNT };
// What HIT bursts into.
enum class Burst : uint8_t { PUFF, CREAM, SPLASH, FEATHERS, CONFETTI, COUNT };

// Beat flags.
constexpr uint8_t B_SPEAKS  = 0x01;   // the receiver says the line, not the sender
constexpr uint8_t FX_FROM_B = 0x02;   // the effect starts at the receiver, not the sender
constexpr uint8_t OUTCOME   = 0x04;   // a is the WINNER's pose and b the loser's, the
                                      // line is the winner's; a tie is both laughing
// Script flags.
constexpr uint8_t S_CLOSE      = 0x01;  // the visitor walks in for it, and back after
constexpr uint8_t S_LEAP       = 0x02;  // the visitor leaps over the host and back
constexpr uint8_t S_HOST_FIRST = 0x04;  // A is the host on both boards, whoever sent it

struct Beat {
    uint16_t ms;
    Pose     a, b;
    uint8_t  line;      // a line pool, a DYN_ code, or 0 for nothing said
    Fx       fx;
    uint8_t  fxArg;
    uint8_t  flags;
};
struct Script {
    const Beat* beat;
    uint8_t     n;
    uint8_t     flags;
    // With S_CLOSE: how much nearer than a high five's reach he comes, in
    // body units -- 0 is where two held-out hands meet, -24 is a hug.
    int8_t      closeUnits;
};

// nullptr for the six originals, which ui_clear.cpp plays itself.
const Script* script(MeshMsg::Emote e);
uint32_t      totalMs(const Script& s);
// The longest any script runs. An emote waits for a gap in the conversation
// and a set piece holds the visit until it is done, so this is bounded.
constexpr uint32_t MAX_MS = 9000;

// ---- lines --------------------------------------------------------------------
// Three of each, taken in turn, so the same emote twice does not repeat itself.
constexpr uint8_t VARIANTS = 3;
uint8_t     lineCount();                        // pools, index 0 included
const char* line(uint8_t pool, uint8_t variant);
// Lines built from the setup byte, written into `out`.
constexpr uint8_t DYN_COIN_CALL = 0xF0;   // the receiver calls it: "HEADS!"
constexpr uint8_t DYN_COIN_SIDE = 0xF1;   // how it landed
constexpr uint8_t DYN_DICE      = 0xF2;   // the winner's gloat, or a tie
constexpr uint8_t DYN_SPOTTED   = 0xF3;   // "See that FLOCK?!"
bool isDyn(uint8_t code);
void dynLine(uint8_t code, MeshMsg::Emote e, uint8_t setup, char* out, size_t cap);

// ---- the setup byte --------------------------------------------------------------
// Rolled once, by the sender, and sent: both boards act out the same result.
//   COIN        bit 0 how it landed (0 heads), bit 1 the receiver called it right
//   DICE        the sender's die times six plus the receiver's, each 0..5
//   ARM_WRESTLE,
//   TUG         bit 0 set: the sender wins
//   SPOTTED     the sender's last detection, a DetectionType
//   RPS         as MeshMsg describes; rolled here too
// `r` is a fresh random number; `lastCaught` a DetectionType value.
uint8_t roll(MeshMsg::Emote e, uint32_t r, uint8_t lastCaught);
enum class Result : uint8_t { NONE, TIE, SENDER, RECEIVER };
Result  outcome(MeshMsg::Emote e, uint8_t setup);
// A die's face, 1..6, for the sender (0) or the receiver (1).
uint8_t die(uint8_t setup, uint8_t who);

// ---- the picker ---------------------------------------------------------------------
constexpr uint8_t TABS = 6, PER_TAB = 6;
extern const char* const TAB_NAME[TABS];
MeshMsg::Emote atTab(uint8_t tab, uint8_t i);
const char*    name(MeshMsg::Emote e);
const char*    sub(MeshMsg::Emote e);     // a second line under the name, or ""

} // namespace EmoteScript
#endif
