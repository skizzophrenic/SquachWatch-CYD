// SquachWatch-CYD — emotes as scripts. See include/emote_script.h.
#include "emote_script.h"

#if SQUACH_MESH
#include "state.h"
#include <stdio.h>
#include <string.h>

namespace EmoteScript {

using E = MeshMsg::Emote;
using P = Pose;

// ---- lines ------------------------------------------------------------------------
// Short on purpose: a guest's bubble sits over a small Squachy, and anything
// past about twenty characters wraps into a second row that covers his face.
enum : uint8_t {
    L_NONE,
    L_FIST_CALL, L_FIST_BOOM, L_SHAKE_CALL, L_SHAKE_DONE, L_SALUTE_CALL, L_SALUTE_BACK,
    L_BOW_CALL, L_BOW_BACK, L_HUG_CALL, L_HUG_DONE,
    L_COIN_CALL, L_DICE_CALL, L_ARM_CALL, L_ARM_WIN, L_TUG_CALL, L_TUG_WIN,
    L_LEAP_CALL, L_LEAP_DONE,
    L_PIE_CALL, L_PIE_HIT, L_BALLOON_CALL, L_BALLOON_HIT, L_PLANE_CALL, L_PLANE_HIT,
    L_PILLOW_CALL, L_PILLOW_BACK, L_PILLOW_DONE, L_TICKLE_CALL, L_TICKLE_HIT,
    L_GIFT_CALL, L_GIFT_OPEN, L_SNACK_CALL, L_SNACK_EAT, L_CHEERS_CALL, L_CHEERS_BACK,
    L_CONFETTI_CALL, L_FIREWORK_CALL, L_FIREWORK_AFTER,
    L_HEART_CALL, L_HEART_BACK, L_LAUGH_CALL, L_LAUGH_BACK, L_SAD_CALL, L_SAD_BACK,
    L_GRR_CALL, L_GRR_BACK, L_SLEEPY_CALL, L_SLEEPY_WAKE,
    L_TINFOIL_CALL, L_TINFOIL_BACK, L_CAMERA_CALL, L_CAMERA_AFTER, L_SPOTTED_BACK,
    L_HOWL_CALL, L_HOWL_BACK, L_SELFIE_CALL, L_SELFIE_AFTER,
    L_COUNT
};

static const char* const LINES[L_COUNT][VARIANTS] = {
    { "", "", "" },
    { "Bump it!",        "Pound it!",         "Knuckles!" },
    { "...Boom.",        "Tssssh!",           "Explosion!" },
    { "The usual?",      "Secret shake!",     "You know it." },
    { "Nailed it.",      "Still got it.",     "Flawless." },
    { "Reporting in!",   "Sir, yes sir!",     "Squach squad!" },
    { "At ease.",        "Carry on.",         "Hoo-rah!" },
    { "After you.",      "Your Squachness.",  "M'lord." },
    { "No, after YOU.",  "Why, thank you.",   "Charmed." },
    { "Bring it in!",    "Hug time!",         "C'mere, you!" },
    { "Aww.",            "Needed that.",      "Best buds." },
    { "Call it!",        "Heads or tails?",   "Flip for it!" },
    { "Roll 'em!",       "Come on, sixes!",   "Big money!" },
    { "Arm wrestle. Now.", "Think you're strong?", "Let's settle this." },
    { "Undefeated!",     "Feel the burn!",    "Pure muscle." },
    { "Tug of war!",     "Grab the rope!",    "Heave!" },
    { "Victory!",        "Mine!",             "Too strong!" },
    { "Leapfrog!",       "Hold still!",       "Coming over!" },
    { "Ribbit.",         "Stuck the landing!", "Frog mode!" },
    { "Pie time!",       "Special delivery!", "Catch!" },
    { "...Banana cream.", "REALLY?!",         "Mmm. Coconut." },
    { "Water balloon!",  "Heads up!",         "Splash zone!" },
    { "I'm SOAKED!",     "Cold! Cold!",       "Oh, it's ON." },
    { "Air mail!",       "Incoming memo!",    "Flight 404!" },
    { "Ow! Paper cut!",  "Return to sender.", "What's it say?" },
    { "Pillow fight!",   "En garde!",         "Feathers out!" },
    { "Take THAT!",      "Payback!",          "Oh, you're DONE." },
    { "Truce?",          "Feathers everywhere!", "Best fight ever." },
    { "Tickle attack!",  "Gotcha!",           "Coochie coo!" },
    { "HAHA stop!",      "Not the pits!",     "No fair!" },
    { "Got you something!", "For you!",       "Open it!" },
    { "For me?!",        "You shouldn't have!", "Ooh, shiny!" },
    { "Want a slice?",   "Pizza break!",      "Share?" },
    { "Nom nom nom.",    "Pineapple? Bold.",  "Best. Friend." },
    { "Cheers!",         "To us!",            "Bottoms up!" },
    { "Hear, hear!",     "To the watch!",     "Salud!" },
    { "Party time!",     "Surprise!",         "Celebrate!" },
    { "Look up!",        "Ooooh!",            "Fireworks!" },
    { "Aaahhh.",         "Beautiful.",        "Again! Again!" },
    { "Love ya, buddy.", "You're the best.",  "Pals forever." },
    { "Aww, shucks.",    "Right back atcha.", "Stop it, you." },
    { "HAHAHA!",         "Hehehe!",           "BWAHAHA!" },
    { "What's so funny?!", "HAHAHA!",         "I can't breathe!" },
    { "Rough day...",    "*sniff*",           "Lost my sock." },
    { "There, there.",   "I got you.",        "Chin up, pal." },
    { "GRRRR!",          "Hrrmph!",           "Grr. Argh." },
    { "Whoa, easy!",     "Who hurt you?",     "Deep breaths!" },
    { "*yaaawn*",        "So sleepy...",      "Nap time?" },
    { "Five more minutes.", "Was I snoring?", "Huh? Wha?" },
    { "Hats on!",        "They're listening.", "Foil up." },
    { "Can't track us now.", "Beam-proof.",   "Signal blocked." },
    { "CAMERA!",         "Flock, 12 o'clock!", "Smile, we're on." },
    { "Did it see us?",  "Act natural.",      "...We're fine." },
    { "Where?!",         "No way!",           "I saw nothing." },
    { "AWOOOO!",         "Squatch call!",     "WHOOOOP!" },
    { "AWOOOOOO!",       "WHOOP WHOOP!",      "...woo?" },
    { "Selfie!",         "Squeeze in!",       "Say cheese!" },
    { "Post it!",        "I blinked!",        "Frame-worthy." },
};

uint8_t lineCount() { return L_COUNT; }

const char* line(uint8_t pool, uint8_t variant) {
    if (pool == L_NONE || pool >= L_COUNT) return nullptr;
    return LINES[pool][variant % VARIANTS];
}

bool isDyn(uint8_t code) { return code >= DYN_COIN_CALL && code <= DYN_SPOTTED; }

// ---- the scripts -------------------------------------------------------------------
// {ms, A's pose, B's pose, line, effect, effect's argument, flags}
#define BT(ms, a, b, l, fx, arg, fl) { ms, P::a, P::b, l, Fx::fx, (uint8_t)(arg), fl }
static const uint8_t OB_PIE = (uint8_t)Obj::PIE, OB_BALLOON = (uint8_t)Obj::BALLOON,
                     OB_PLANE = (uint8_t)Obj::PLANE, OB_PILLOW = (uint8_t)Obj::PILLOW,
                     OB_GIFT = (uint8_t)Obj::GIFT, OB_PIZZA = (uint8_t)Obj::PIZZA;
static const uint8_t BU_CREAM = (uint8_t)Burst::CREAM, BU_SPLASH = (uint8_t)Burst::SPLASH,
                     BU_FEATHERS = (uint8_t)Burst::FEATHERS, BU_CONFETTI = (uint8_t)Burst::CONFETTI;

static const Beat FIST_BUMP[] = {
    BT( 900, FIST,  NONE,  L_FIST_CALL,  NONE,  0, 0),
    BT( 700, FIST,  FIST,  L_NONE,       SPARK, 2, 0),
    BT(1000, CHEER, CHEER, L_FIST_BOOM,  NONE,  0, B_SPEAKS),
};
static const Beat HANDSHAKE[] = {
    BT(1100, HIGH_FIVE, HIGH_FIVE, L_SHAKE_CALL, SPARK, 0, 0),
    BT( 650, LOW_FIVE,  LOW_FIVE,  L_NONE,       SPARK, 1, 0),
    BT( 650, FIST,      FIST,      L_NONE,       SPARK, 2, 0),
    BT(1000, LAUGH,     LAUGH,     L_SHAKE_DONE, NONE,  0, B_SPEAKS),
};
static const Beat SALUTE[] = {
    BT(1500, SALUTE, NONE,   L_SALUTE_CALL, NONE, 0, 0),
    BT(1500, SALUTE, SALUTE, L_SALUTE_BACK, NONE, 0, B_SPEAKS),
    BT( 800, LAUGH,  LAUGH,  L_NONE,        NONE, 0, 0),
};
static const Beat BOW[] = {
    BT(1500, BOW,   NONE,  L_BOW_CALL, NONE, 0, 0),
    BT(1500, NONE,  BOW,   L_BOW_BACK, NONE, 0, B_SPEAKS),
    BT(1200, BOW,   BOW,   L_NONE,     NONE, 0, 0),
    BT( 800, LAUGH, LAUGH, L_NONE,     NONE, 0, 0),
};
static const Beat HUG[] = {
    BT( 900, HUG,   NONE,  L_HUG_CALL, NONE,   0, 0),
    BT(1700, HUG,   HUG,   L_NONE,     HEARTS, 0, 0),
    BT(1000, LAUGH, LAUGH, L_HUG_DONE, NONE,   0, B_SPEAKS),
};
static const Beat COIN[] = {
    BT(1100, FIST,  NONE,  L_COIN_CALL,   COIN, 2, 0),
    BT(1000, FIST,  NONE,  DYN_COIN_CALL, COIN, 0, B_SPEAKS),
    BT(1400, CHEER, SAD,   DYN_COIN_SIDE, COIN, 1, OUTCOME),
    BT( 800, LAUGH, LAUGH, L_NONE,        COIN, 1, 0),
};
static const Beat DICE[] = {
    BT(1000, PUMP,  PUMP,  L_DICE_CALL, NONE, 0, 0),
    BT(1100, FIST,  FIST,  L_NONE,      DICE, 0, 0),
    BT(1500, CHEER, SAD,   DYN_DICE,    DICE, 1, OUTCOME),
    BT( 800, LAUGH, LAUGH, L_NONE,      DICE, 1, 0),
};
static const Beat ARM_WRESTLE[] = {
    BT( 900, FIST,   NONE,   L_ARM_CALL, NONE,  0, 0),
    BT(1900, STRAIN, STRAIN, L_NONE,     TABLE, 0, 0),
    BT(1300, CHEER,  SAD,    L_ARM_WIN,  TABLE, 1, OUTCOME),
    BT( 900, LAUGH,  LAUGH,  L_NONE,     NONE,  0, 0),
};
static const Beat TUG[] = {
    BT( 900, FIST,  FIST,     L_TUG_CALL, ROPE, 0, 0),
    BT(1900, PULL,  PULL,     L_NONE,     ROPE, 0, 0),
    BT(1300, CHEER, HANDS_UP, L_TUG_WIN,  ROPE, 1, OUTCOME),
    BT( 800, LAUGH, LAUGH,    L_NONE,     NONE, 0, 0),
};
// The visitor does the leaping on both boards -- see S_HOST_FIRST.
static const Beat LEAPFROG[] = {
    BT( 900, CROUCH, NONE,  L_LEAP_CALL, NONE, 0, B_SPEAKS),
    BT( 900, CROUCH, CHEER, L_NONE,      NONE, 0, 0),
    BT( 600, CROUCH, LAUGH, L_NONE,      NONE, 0, 0),
    BT( 900, CROUCH, CHEER, L_NONE,      NONE, 0, 0),
    BT( 900, LAUGH,  LAUGH, L_LEAP_DONE, NONE, 0, 0),
};
static const Beat PIE[] = {
    BT( 700, FIST,  NONE,  L_PIE_CALL, HOLD,  OB_PIE,   0),
    BT( 650, FIST,  NONE,  L_NONE,     THROW, OB_PIE,   0),
    BT(1500, LAUGH, COVER, L_PIE_HIT,  HIT,   BU_CREAM, B_SPEAKS),
    BT( 900, LAUGH, LAUGH, L_NONE,     HIT,   BU_CREAM, 0),
};
static const Beat BALLOON[] = {
    BT( 700, FIST,  NONE,     L_BALLOON_CALL, HOLD,  OB_BALLOON, 0),
    BT( 650, FIST,  NONE,     L_NONE,         THROW, OB_BALLOON, 0),
    BT(1500, LAUGH, HANDS_UP, L_BALLOON_HIT,  HIT,   BU_SPLASH,  B_SPEAKS),
    BT( 800, LAUGH, LAUGH,    L_NONE,         NONE,  0,          0),
};
static const Beat PLANE[] = {
    BT( 800, FIST,  NONE,  L_PLANE_CALL, HOLD,  OB_PLANE, 0),
    BT(1100, FIST,  NONE,  L_NONE,       THROW, OB_PLANE, 0),
    BT(1400, LAUGH, COVER, L_PLANE_HIT,  BONK,  0,        B_SPEAKS),
    BT( 800, LAUGH, LAUGH, L_NONE,       NONE,  0,        0),
};
static const Beat PILLOW[] = {
    BT( 700, FIST,  NONE,  L_PILLOW_CALL, HOLD,  OB_PILLOW,   0),
    BT( 600, FIST,  NONE,  L_NONE,        THROW, OB_PILLOW,   0),
    BT( 700, NONE,  FIST,  L_PILLOW_BACK, HIT,   BU_FEATHERS, B_SPEAKS),
    BT( 600, NONE,  FIST,  L_NONE,        THROW, OB_PILLOW,   FX_FROM_B),
    BT(1400, LAUGH, LAUGH, L_PILLOW_DONE, HIT,   BU_FEATHERS, FX_FROM_B),
};
static const Beat TICKLE[] = {
    BT( 900, WIGGLE, NONE,  L_TICKLE_CALL, NONE, 0, 0),
    BT(1800, WIGGLE, LAUGH, L_TICKLE_HIT,  HAHA, 0, B_SPEAKS | FX_FROM_B),
    BT( 900, LAUGH,  LAUGH, L_NONE,        NONE, 0, 0),
};
static const Beat GIFT[] = {
    BT( 900, FIST,  NONE,  L_GIFT_CALL, HOLD, OB_GIFT,     0),
    BT( 700, FIST,  FIST,  L_NONE,      LOB,  OB_GIFT,     0),
    BT( 900, NONE,  FIST,  L_GIFT_OPEN, HOLD, OB_GIFT,     B_SPEAKS | FX_FROM_B),
    BT(1400, LAUGH, CHEER, L_NONE,      HIT,  BU_CONFETTI, 0),
};
static const Beat SNACK[] = {
    BT( 900, FIST,  NONE,  L_SNACK_CALL, HOLD, OB_PIZZA, 0),
    BT( 700, FIST,  FIST,  L_NONE,       LOB,  OB_PIZZA, 0),
    BT(1600, NONE,  FIST,  L_SNACK_EAT,  HOLD, OB_PIZZA, B_SPEAKS | FX_FROM_B),
    BT( 800, LAUGH, LAUGH, L_NONE,       NONE, 0,        0),
};
static const Beat CHEERS[] = {
    BT( 900, FIST,  FIST,  L_CHEERS_CALL, CUPS, 0, 0),
    BT( 600, FIST,  FIST,  L_NONE,        CUPS, 1, 0),
    BT(1200, FIST,  FIST,  L_CHEERS_BACK, CUPS, 0, B_SPEAKS),
    BT( 900, LAUGH, LAUGH, L_NONE,        NONE, 0, 0),
};
static const Beat CONFETTI[] = {
    BT( 700, CHEER, NONE,  L_CONFETTI_CALL, NONE,     0, 0),
    BT(2300, CHEER, CHEER, L_NONE,          CONFETTI, 0, 0),
    BT( 800, LAUGH, LAUGH, L_NONE,          NONE,     0, 0),
};
static const Beat FIREWORKS[] = {
    BT( 900, POINT, NONE,  L_FIREWORK_CALL,  NONE,      0, 0),
    BT(2600, CHEER, CHEER, L_NONE,           FIREWORKS, 0, 0),
    BT(1000, LAUGH, LAUGH, L_FIREWORK_AFTER, NONE,      0, B_SPEAKS),
};
static const Beat HEART[] = {
    BT(1800, NONE,  NONE,  L_HEART_CALL, HEARTS, 0, 0),
    BT(1400, NONE,  LAUGH, L_HEART_BACK, HEARTS, 0, B_SPEAKS | FX_FROM_B),
    BT( 600, LAUGH, LAUGH, L_NONE,       NONE,   0, 0),
};
static const Beat LAUGH[] = {
    BT( 700, LAUGH, NONE,  L_LAUGH_CALL, HAHA, 0, 0),
    BT(2000, LAUGH, LAUGH, L_LAUGH_BACK, HAHA, 0, B_SPEAKS | FX_FROM_B),
    BT( 500, LAUGH, LAUGH, L_NONE,       NONE, 0, 0),
};
static const Beat SAD[] = {
    BT(1800, SAD,   NONE,  L_SAD_CALL, NONE, 0, 0),
    BT(1700, SAD,   FIST,  L_SAD_BACK, NONE, 0, B_SPEAKS),
    BT(1000, LAUGH, LAUGH, L_NONE,     NONE, 0, 0),
};
static const Beat GRR[] = {
    BT(1800, GRR,   NONE,     L_GRR_CALL, STEAM, 0, 0),
    BT(1400, GRR,   HANDS_UP, L_GRR_BACK, STEAM, 0, B_SPEAKS),
    BT( 900, LAUGH, LAUGH,    L_NONE,     NONE,  0, 0),
};
static const Beat SLEEPY[] = {
    BT(1300, STRETCH, NONE,    L_SLEEPY_CALL, NONE, 0, 0),
    BT(2300, SLEEPY,  SLEEPY,  L_NONE,        NONE, 0, 0),
    BT(1100, STRETCH, STRETCH, L_SLEEPY_WAKE, NONE, 0, B_SPEAKS),
};
static const Beat TINFOIL[] = {
    BT(1000, NONE,        NONE,        L_TINFOIL_CALL, HATS, 0, 0),
    BT(2000, LOOK_AROUND, LOOK_AROUND, L_TINFOIL_BACK, HATS, 1, B_SPEAKS),
    BT( 900, LAUGH,       LAUGH,       L_NONE,         HATS, 1, 0),
};
static const Beat CAMERA[] = {
    BT( 900, POINT, NONE,  L_CAMERA_CALL,  CAMERA, 0, 0),
    BT(1700, COVER, COVER, L_NONE,         CAMERA, 0, 0),
    BT(1000, LAUGH, LAUGH, L_CAMERA_AFTER, NONE,   0, B_SPEAKS),
};
static const Beat SPOTTED[] = {
    BT(1600, POINT, NONE,        DYN_SPOTTED,    ICON, 0, 0),
    BT(1500, NONE,  LOOK_AROUND, L_SPOTTED_BACK, ICON, 0, B_SPEAKS),
    BT( 800, LAUGH, LAUGH,       L_NONE,         NONE, 0, 0),
};
static const Beat HOWL[] = {
    BT(1600, HOWL,  NONE,  L_HOWL_CALL, WAVES, 0, 0),
    BT(1600, NONE,  HOWL,  L_HOWL_BACK, WAVES, 0, B_SPEAKS | FX_FROM_B),
    BT( 900, LAUGH, LAUGH, L_NONE,      NONE,  0, 0),
};
static const Beat SELFIE[] = {
    BT(1000, SELFIE, NONE,  L_SELFIE_CALL,  NONE,  0, 0),
    BT( 700, SELFIE, CHEER, L_NONE,         NONE,  0, 0),
    BT(1000, SELFIE, CHEER, L_NONE,         FLASH, 0, 0),
    BT(1100, LAUGH,  LAUGH, L_SELFIE_AFTER, FLASH, 1, B_SPEAKS),
};
#undef BT

#define SC(arr, fl, close) { arr, (uint8_t)(sizeof(arr) / sizeof(arr[0])), fl, close }
static const Script SCRIPTS[] = {
    SC(FIST_BUMP,   S_CLOSE, 0),
    SC(HANDSHAKE,   S_CLOSE, 0),
    SC(SALUTE,      0, 0),
    SC(BOW,         0, 0),
    SC(HUG,         S_CLOSE, -24),
    SC(COIN,        0, 0),
    SC(DICE,        0, 0),
    SC(ARM_WRESTLE, S_CLOSE, 0),
    SC(TUG,         0, 0),
    SC(LEAPFROG,    S_LEAP | S_HOST_FIRST, 0),
    SC(PIE,         0, 0),
    SC(BALLOON,     0, 0),
    SC(PLANE,       0, 0),
    SC(PILLOW,      0, 0),
    SC(TICKLE,      S_CLOSE, -6),
    SC(GIFT,        0, 0),
    SC(SNACK,       0, 0),
    SC(CHEERS,      S_CLOSE, 0),
    SC(CONFETTI,    0, 0),
    SC(FIREWORKS,   0, 0),
    SC(HEART,       0, 0),
    SC(LAUGH,       0, 0),
    SC(SAD,         S_CLOSE, 0),
    SC(GRR,         0, 0),
    SC(SLEEPY,      0, 0),
    SC(TINFOIL,     0, 0),
    SC(CAMERA,      0, 0),
    SC(SPOTTED,     0, 0),
    SC(HOWL,        0, 0),
    SC(SELFIE,      S_CLOSE, -28),
};
#undef SC
static const uint8_t FIRST_SCRIPTED = (uint8_t)E::FIST_BUMP;
static_assert(sizeof SCRIPTS / sizeof SCRIPTS[0] == (size_t)E::COUNT - FIRST_SCRIPTED,
              "a script for every emote after the originals");

const Script* script(E e) {
    const uint8_t i = (uint8_t)e;
    if (i < FIRST_SCRIPTED || i >= (uint8_t)E::COUNT) return nullptr;
    return &SCRIPTS[i - FIRST_SCRIPTED];
}

uint32_t totalMs(const Script& s) {
    uint32_t t = 0;
    for (uint8_t i = 0; i < s.n; i++) t += s.beat[i].ms;
    return t;
}

// ---- the setup byte ----------------------------------------------------------------
uint8_t die(uint8_t setup, uint8_t who) {
    const uint8_t v = (uint8_t)(setup % 36);
    return (uint8_t)((who ? v % 6 : v / 6) + 1);
}

uint8_t roll(E e, uint32_t r, uint8_t lastCaught) {
    switch (e) {
        case E::RPS:         return (uint8_t)(r % 9);
        case E::COIN:        return (uint8_t)(r & 3);
        case E::DICE:        return (uint8_t)(r % 36);
        case E::ARM_WRESTLE:
        case E::TUG:         return (uint8_t)(r & 1);
        case E::SPOTTED:     return lastCaught < (uint8_t)DetectionType::COUNT ? lastCaught : 0;
        default:             return 0;
    }
}

Result outcome(E e, uint8_t setup) {
    switch (e) {
        case E::RPS: {
            const uint8_t a = (uint8_t)((setup / 3) % 3), b = (uint8_t)(setup % 3);
            const uint8_t o = (uint8_t)((a + 3 - b) % 3);      // 1: paper over rock
            return o == 0 ? Result::TIE : (o == 1 ? Result::SENDER : Result::RECEIVER);
        }
        case E::COIN:        return (setup & 2) ? Result::RECEIVER : Result::SENDER;
        case E::DICE: {
            const uint8_t a = die(setup, 0), b = die(setup, 1);
            return a == b ? Result::TIE : (a > b ? Result::SENDER : Result::RECEIVER);
        }
        case E::ARM_WRESTLE:
        case E::TUG:         return (setup & 1) ? Result::SENDER : Result::RECEIVER;
        default:             return Result::NONE;
    }
}

// What a detection is called out loud. Short: it has to fit a bubble.
static const char* spokenName(uint8_t t) {
    switch ((DetectionType)t) {
        case DetectionType::FLOCK:       return "FLOCK";
        case DetectionType::AXON:        return "AXON";
        case DetectionType::META:        return "GLASSES";
        case DetectionType::SKIMMER:     return "SKIMMER";
        case DetectionType::RAVEN:       return "RAVEN";
        case DetectionType::AIRTAG:      return "AIRTAG";
        case DetectionType::DRONE:       return "DRONE";
        case DetectionType::ALPR:        return "ALPR";
        case DetectionType::CAMERA:      return "CAMERA";
        case DetectionType::SAMSUNG_TAG: return "SMARTTAG";
        case DetectionType::GOOGLE_TAG:  return "TRACKER";
        case DetectionType::TILE:        return "TILE";
        case DetectionType::RING:        return "RING CAM";
        case DetectionType::DEAUTH:      return "DEAUTH";
        case DetectionType::EVILTWIN:    return "EVIL TWIN";
        case DetectionType::IBEACON:     return "BEACON";
        case DetectionType::HACKER:      return "HACKER";
        default:                         return nullptr;
    }
}

void dynLine(uint8_t code, E e, uint8_t setup, char* out, size_t cap) {
    if (!out || !cap) return;
    out[0] = '\0';
    switch (code) {
        case DYN_COIN_CALL: {
            // The receiver's call: right means it matches how it lands.
            const bool side = setup & 1, right = setup & 2;
            snprintf(out, cap, "%s!", (right ? side : !side) ? "TAILS" : "HEADS");
            break;
        }
        case DYN_COIN_SIDE:
            // Said by whoever won: the caller if they called it, else the flipper.
            snprintf(out, cap, "%s! %s", (setup & 1) ? "TAILS" : "HEADS",
                     (setup & 2) ? "Called it!" : "Ha!");
            break;
        case DYN_DICE: {
            const uint8_t a = die(setup, 0), b = die(setup, 1);
            if (a == b) snprintf(out, cap, "Tie! %u and %u.", (unsigned)a, (unsigned)b);
            else        snprintf(out, cap, "%u beats %u!", (unsigned)(a > b ? a : b),
                                 (unsigned)(a > b ? b : a));
            break;
        }
        case DYN_SPOTTED: {
            const char* n = spokenName(setup);
            if (n) snprintf(out, cap, "See that %s?!", n);
            else   snprintf(out, cap, "Did you see that?!");
            break;
        }
        default: break;
    }
    (void)e;
}

// ---- the picker ----------------------------------------------------------------------
// Six tabs of six. The originals are spread among them rather than kept in a
// tab of their own: a wave belongs with the greetings, not with "the old ones".
const char* const TAB_NAME[TABS] = { "HI", "PLAY", "PRANK", "PARTY", "MOOD", "WATCH" };
static const E TAB[TABS][PER_TAB] = {
    { E::WAVE,     E::HIGH_FIVE, E::FIST_BUMP, E::HANDSHAKE,   E::SALUTE,   E::BOW },
    { E::RPS,      E::COIN,      E::DICE,      E::ARM_WRESTLE, E::TUG,      E::LEAPFROG },
    { E::SNOWBALL, E::PIE,       E::BALLOON,   E::PLANE,       E::PILLOW,   E::TICKLE },
    { E::HUG,      E::GIFT,      E::SNACK,     E::CHEERS,      E::CONFETTI, E::FIREWORKS },
    { E::DANCE,    E::HEART,     E::LAUGH,     E::SAD,         E::GRR,      E::SLEEPY },
    { E::BOO,      E::TINFOIL,   E::CAMERA,    E::SPOTTED,     E::HOWL,     E::SELFIE },
};

E atTab(uint8_t tab, uint8_t i) { return TAB[tab % TABS][i % PER_TAB]; }

static const char* const NAME[(size_t)E::COUNT][2] = {
    { "WAVE", "" }, { "HIGH FIVE", "" }, { "DANCE-OFF", "" }, { "ROCK PAPER", "SCISSORS" },
    { "SNOWBALL", "" }, { "BOO!", "" },
    { "FIST BUMP", "" }, { "SECRET", "HANDSHAKE" }, { "SALUTE", "" }, { "BOW", "" }, { "HUG", "" },
    { "COIN FLIP", "" }, { "DICE ROLL", "" }, { "ARM", "WRESTLE" }, { "TUG OF WAR", "" },
    { "LEAPFROG", "" },
    { "PIE", "" }, { "WATER", "BALLOON" }, { "PAPER", "PLANE" }, { "PILLOW", "FIGHT" },
    { "TICKLE", "" },
    { "GIFT", "" }, { "SNACK", "" }, { "CHEERS", "" }, { "CONFETTI", "" }, { "FIREWORKS", "" },
    { "HEART", "" }, { "LAUGH", "" }, { "SAD", "" }, { "GRR", "" }, { "SLEEPY", "" },
    { "TINFOIL", "HATS" }, { "CAMERA!", "" }, { "SEE THAT?", "" }, { "HOWL", "" },
    { "SELFIE", "" },
};

const char* name(E e) { return (uint8_t)e < (uint8_t)E::COUNT ? NAME[(uint8_t)e][0] : ""; }
const char* sub(E e)  { return (uint8_t)e < (uint8_t)E::COUNT ? NAME[(uint8_t)e][1] : ""; }

} // namespace EmoteScript
#endif // SQUACH_MESH
