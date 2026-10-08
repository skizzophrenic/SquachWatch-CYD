// SquachWatch-CYD — the pet. See pet.h.
#include "pet.h"
#include "theme.h"
#include "squachy.h"
#include "lil_guy.h"
#include "settings.h"
#include "clock.h"
#include "state.h"
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>


namespace Pet {

// ---- his material -------------------------------------------------------
// Kept short on purpose. The bubble sits beside a forty-pixel sprite, and
// anything much past thirty characters stops looking like it came out of
// him and starts looking like a caption.
static const char* const QUIPS[] = {
    "nice shades",
    "PCBWAY!",
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

// ---- the other one ------------------------------------------------------
// THE YETI, off the ski hill. He is drawn at the size the hill draws him --
// no new art, nothing scaled -- and everything that makes him him is in what
// he shouts. Fourteen characters is the limit the hill's own bubbles work
// to, and his register is the same one he chases skiers in: all caps, one to
// three words, no articles, himself in the third person.
static const char* const YETI_QUIPS[] = {
    "YETI HERE.", "YETI! YETI!", "SNOW? NO SNOW.", "YETI BORED.",
    "WHERE SNOW?", "ROOM QUIET.", "YETI HUNGRY.", "SMALL FRIEND.",
    "YOU TALK MUCH.", "STOP WAVE.", "NOBODY. GOOD.", "YETI NAP?",
    "DARK GOOD.", "YETI GO.", "BYE. HUNGRY.", "YETI BACK SOON",
};
static const uint8_t YETI_QUIPS_N = sizeof(YETI_QUIPS) / sizeof(YETI_QUIPS[0]);

// He walks on, stands, says one thing and leaves. No climbing: he is three
// times the lil guy's weight and the joke is that he does not do tricks.
//
// Always in from the left and out to the right, because snowYeti() draws him
// facing one way and a mirrored version would be new art -- which is the one
// thing this was meant not to need.
enum class YPhase : uint8_t { AWAY, IN, TALK, NAP, OUT };
static YPhase    s_yPhase  = YPhase::AWAY;
static uint32_t  s_yNextAt = 0, s_yAt = 0;
static float     s_yX      = -100.0f;
static uint8_t   s_yQuip   = 0;
static const char* s_yLine = nullptr;        // what he is saying this visit
static bool      s_yNap    = false;          // this visit he lies down instead
static bool      s_yAnswered = false;        // the host has had his turn
static uint32_t  s_yFlinchAt = 0;            // ...and it made him jump
static const float    YETI_PXMS = 0.048f;    // he lumbers; the lil guy trots
static const uint32_t YETI_TALK_MS = 3400;
// Long enough to read as asleep rather than as fallen over, short enough
// that a glance at the screen is not just a yeti lying on the floor.
static const uint32_t YETI_NAP_MS  = 6200;
static const uint32_t YETI_ANSWER_MS = 1500;  // the host waits a beat first
static const uint32_t YETI_FLINCH_MS = 700;

// A line about WHERE he has turned up. He already said "WHERE SNOW?" into
// an empty room; this is that thought finished. Only the backgrounds he
// would plainly have an opinion about -- the rest fall back to the general
// set, because a line for every one of them would be eleven jokes thin.
static const char* placeLine(uint8_t pick) {
    switch (Settings::background()) {
    case Settings::Background::SNOWFALL:
        { static const char* const L[3] = { "SNOW! GOOD.", "YETI HOME.", "MY HILL." };        return L[pick % 3]; }
    case Settings::Background::FIRE:
        { static const char* const L[3] = { "TOO HOT.", "YETI MELT.", "NO. NO. NO." };        return L[pick % 3]; }
    case Settings::Background::AQUARIUM:
        { static const char* const L[3] = { "WET.", "FISH SMALL.", "YETI NO SWIM." };         return L[pick % 3]; }
    case Settings::Background::TOASTERS:
        { static const char* const L[3] = { "BREAD FLY?", "CATCH TOAST.", "WHAT THAT." };     return L[pick % 3]; }
    case Settings::Background::STARFIELD:
        { static const char* const L[3] = { "SKY MOVING.", "STARS COLD.", "YETI DIZZY." };    return L[pick % 3]; }
    case Settings::Background::TERMINAL:
        { static const char* const L[3] = { "WORDS FALL.", "YETI NO READ.", "GREEN. HUH." };  return L[pick % 3]; }
    default: return nullptr;
    }
}

// What Squachy says back. The point of these is that they are addressed to
// YOU about him, not to him -- which is what makes the pair read as a
// double act instead of two things sharing a screen.
static const char* const YETI_REPLY[] = {
    "he does this",
    "ignore him",
    "that's my guy",
    "he's harmless",
    "don't make eye contact",
    "he found the stairs again",
    "big fella. few words.",
    "we don't talk about it",
};
static const uint8_t YETI_REPLY_N = sizeof(YETI_REPLY) / sizeof(YETI_REPLY[0]);
static const char* const NAP_REPLY[3] = {
    "make yourself at home",
    "right there. sure.",
    "he's out cold",
};

// ---- C1iPPY -------------------------------------------------------------
// A paperclip that has been unbent into a lock pick. He does not visit: he
// stays beside Squachy, hops to the other spot now and then, and about once
// a minute offers advice nobody asked for. After a catch the advice is about
// the catch, and it is always the most obvious thing there is to say.
//
// No mouth. Everything is the eyelids, the eyebrows and the pick. The wire
// is laid out each frame as points in a 32 x 46 box and stamped into a small
// grid, then drawn a row at a time, so the hook, the squash and the twitch
// cost a few thousand byte writes rather than a few thousand pixels.
//
// Lines are three balloon lines of 24 characters at most, 72 in all.
static const char* const CLIP_TIPS[] = {
    "It looks like you're being watched. Would you like to be watched less?",
    "Tip: cameras can see you. Try standing behind something.",
    "It looks like nothing is nearby. Would you like me to worry anyway?",
    "Did you know? WiFi has no wires. I checked.",
    "Fun fact: I am also scanning you. Professionally.",
    "Tip: Squachy works best when he is switched on.",
    "It looks like you want privacy. Have you tried a hat?",
    "Battery low? Have you tried charging it?",
    "Tip: a paperclip opens most locks. Your own locks.",
    "Need a SIM tray tool? I'm right here. Bent, but here.",
    "Pro tip: if it says FLOCK, it is a Flock.",
    "I used to hold papers together. Now I hold grudges.",
    "Would you like help? No? I'll just stand here, then.",
    "Tip: look both ways before crossing a WiFi network.",
    "It looks like you're reading this. Keep going.",
    "Tip: nothing on the counters is the good kind of nothing.",
};
static const uint8_t CLIP_TIPS_N = sizeof(CLIP_TIPS) / sizeof(CLIP_TIPS[0]);
static const char* const CLIP_NIGHT[] = {
    "It looks like it's late. Would you like to sleep? I don't.",
    "Night tip: cameras still see in the dark.",
    "Can't sleep? Me neither. I'm a paperclip.",
};
static const char* const CLIP_SQUAD[] = {
    "It looks like a friend. Want me to clip you two together?",
    "Visitor detected. Tip: say hello.",
    "Two Squachys. Twice the advice.",
};
static const char* const CLIP_POKE[] = {
    "Would you like help?",
    "Great! Here is a tip: you poked me again.",
    "It looks like you poked me. Would you like to stop?",
    "Okay. I'll be right here. Watching. Helpfully.",
};
// People will throw him. A lot. So there is a dozen and more of these:
// THROWN after a plain landing, LANDED after a bouncy one.
static const char* const CLIP_THROWN[] = {
    "It looks like you're throwing a paperclip.",
    "This is not a supported workflow!",
    "Wheeee. Would you like help with that?",
    "Filing myself under DRAFTS!",
    "It looks like gravity. Would you like to turn it off?",
    "I'd rate that throw a six. Low effort.",
    "Office supplies have feelings. Allegedly.",
    "Have you tried throwing Squachy? He's rounder.",
};
static const uint8_t CLIP_THROWN_N = sizeof(CLIP_THROWN) / sizeof(CLIP_THROWN[0]);
static const char* const CLIP_LANDED[] = {
    "Bent, but not broken. Mostly bent.",
    "Would you like to undo that? You can't.",
    "Tip: paperclips do not fly. Noted.",
    "Tip: aim for the recycling. I'm metal.",
    "It looks like you're venting. Tip: stop.",
    "Attached to nothing now. Thanks for that.",
};
static const uint8_t CLIP_LANDED_N = sizeof(CLIP_LANDED) / sizeof(CLIP_LANDED[0]);
// Thrown again within a minute of the last one, and he starts counting.
static const char* const CLIP_STREAK[] = {
    "It looks like throw %d. Would you like a hobby?",
    "Throw %d. I'm filing a complaint.",
    "%d throws. Tip: that's a lot.",
    "That's %d. I'm still smarter than you.",
    "%d. Would you like to stop? No? Okay.",
    "Throw %d logged. Nobody reads the log.",
};
static const uint8_t CLIP_STREAK_N = sizeof(CLIP_STREAK) / sizeof(CLIP_STREAK[0]);
static char     s_cStreakLine[48];
static uint8_t  s_cStreak = 0;
static uint32_t s_cStreakAt = 0;
static const char* clipStreakLine(uint32_t now) {
    if (now - s_cStreakAt > 60000) s_cStreak = 0;
    s_cStreakAt = now;
    if (s_cStreak < 255) s_cStreak++;
    if (s_cStreak < 3 || random(0, 2)) return nullptr;
    snprintf(s_cStreakLine, sizeof(s_cStreakLine), CLIP_STREAK[random(0, CLIP_STREAK_N)], (int)s_cStreak);
    return s_cStreakLine;
}
// When it is Squachy who gets thrown.
static const char* const CLIP_SQTHROWN[] = {
    "It looks like you're throwing Squachy. Need a hand?",
    "Tip: cryptids are not aerodynamic.",
    "I'd give that a seven. Bad landing.",
};
// What Squachy says back, in his own bubble, after a tip has had its turn.
static const char* const CLIP_BACK[] = {
    "nobody asked, wire", "go hold a paper", "i'm the detector here",
    "bent AND smug. wow", "you're a staple's understudy", "thanks. ignoring that.",
};

static const char* clipCatchLine(uint8_t type) {
    switch ((DetectionType)type) {
        case DetectionType::FLOCK:       return "It looks like you found a Flock camera. Try driving past it.";
        case DetectionType::AXON:        return "Body camera nearby. Tip: it is recording. Smile.";
        case DetectionType::META:        return "Camera glasses nearby. Would you like to make a face?";
        case DetectionType::SKIMMER:     return "That's a card skimmer. Have you considered paying in cash?";
        case DetectionType::RAVEN:       return "Gunshot sensor nearby. Tip: please do not test it.";
        case DetectionType::AIRTAG:
        case DetectionType::SAMSUNG_TAG:
        case DetectionType::GOOGLE_TAG:
        case DetectionType::TILE:        return "It looks like a tracker. Is it yours? Check your keys.";
        case DetectionType::DRONE:       return "A drone! Tip: look up.";
        case DetectionType::ALPR:        return "Plate reader nearby. Have you considered a bicycle?";
        case DetectionType::CAMERA:      return "It looks like there's a camera. You're on TV!";
        case DetectionType::RING:        return "A doorbell camera. Tip: do not ring it forty times.";
        case DetectionType::DEAUTH:      return "Someone is knocking devices off WiFi. Turn it off and on?";
        case DetectionType::EVILTWIN:    return "That WiFi has an evil twin. Tip: don't join the evil one.";
        case DetectionType::HACKER:      return "It looks like a hacker tool. Hello, colleague.";
        case DetectionType::IBEACON:     return "A shop beacon wants to sell you things. Tip: walk faster.";
        default:                         return nullptr;
    }
}

enum class ClipFace : uint8_t { IDLE, TALK, SURPRISED, SMUG };
static const int      CLIP_W = 32, CLIP_H = 46;
static const uint32_t CLIP_TALK_MS  = 5500;
static const uint32_t CLIP_HOP_MS   = 700, CLIP_LAND_MS = 160;
static const uint32_t CLIP_SMUG_MS  = 3000;

static const char* s_cLine   = nullptr;     // in his balloon until s_cSaidAt + CLIP_TALK_MS
static uint32_t    s_cSaidAt = 0, s_cNextTip = 0, s_cHopAt = 0, s_cNextHop = 0;
static uint32_t    s_cSmugAt = 0, s_cPokeAt = 0;
static bool        s_cCaught = false, s_cAnswered = true, s_cSpotB = false;
static uint8_t     s_cTip = 0, s_cPoke = 0;
static int16_t     s_cX = -100, s_cY = 0;   // the box's top-left this frame, for hits
static uint8_t     s_cPending = 0;          // a catch to talk about, type + 1
static int16_t     s_cFromX = 0;

// Picked up and thrown. HELD follows the finger; AIR is gravity and bounces;
// SIT is a moment on the floor before he hops home. While any of it runs he
// is off his spot, and his spot is worked out from where Squachy STANDS --
// remembered from the last frame Squachy was standing -- so a carried or
// thrown Squachy does not take C1iPPY with him.
enum class ClipThrow : uint8_t { NONE, HELD, AIR, SIT };
static ClipThrow   s_cTh = ClipThrow::NONE;
static float       s_cfX = 0, s_cfY = 0, s_cvX = 0, s_cvY = 0;
static const float CLIP_G = 0.0028f, CLIP_MIN_V = 0.32f, CLIP_MAX_V = 1.8f;
// A finger on a pet: where it went down, where it is, how fast it is going
// (smoothed the same way Squachy's is), and whether it has gone anywhere.
// C1iPPY and T0@$TY share it -- they used to carry a copy each.
struct Grip {
    int offX = 0, offY = 0, downX = 0, downY = 0, fingerX = 0, fingerY = 0, gX = 0, gY = 0;
    uint32_t grabAt = 0, gT = 0;
    float gvX = 0, gvY = 0;
    bool moved = false;
};
static void gripStart(Grip& g, int x, int y, int petX, int petY, uint32_t now) {
    g.offX = x - petX; g.offY = y - petY;
    g.downX = g.fingerX = g.gX = x;
    g.downY = g.fingerY = g.gY = y;
    g.grabAt = g.gT = now;
    g.gvX = g.gvY = 0;
    g.moved = false;
}
static void gripMove(Grip& g, int x, int y, uint32_t now) {
    const int dx = x - g.downX, dy = y - g.downY;
    if (dx * dx + dy * dy > 8 * 8) g.moved = true;
    if (now - g.gT > 0 && now - g.gT < 150) {
        const float dt = (float)(now - g.gT);
        g.gvX = 0.5f * g.gvX + 0.5f * (float)(x - g.gX) / dt;
        g.gvY = 0.5f * g.gvY + 0.5f * (float)(y - g.gY) / dt;
    } else if (now - g.gT >= 150) {
        g.gvX = g.gvY = 0;
    }
    if (now != g.gT) { g.gX = x; g.gY = y; g.gT = now; }
    g.fingerX = x; g.fingerY = y;
}
// What letting go means: 0 a poke (a still tap), 1 a fling at (vx, vy),
// 2 a drop (let go slowly), 3 nothing. On the boards with buttons there is
// no finger to measure, so a hold let go tosses him up and to one side.
static uint8_t gripLetGo(const Grip& g, uint32_t now, float& vx, float& vy) {
    if (!g.moved && now - g.grabAt < 450) return 0;
#if defined(SQW_SMALL)
    if (!g.moved) { vx = random(0, 2) ? 0.5f : -0.5f; vy = -1.0f; return 1; }
#endif
    if (!g.moved) return 3;
    if (now - g.gT < 120 && g.gvX * g.gvX + g.gvY * g.gvY > CLIP_MIN_V * CLIP_MIN_V) { vx = g.gvX; vy = g.gvY; return 1; }
    vx = vy = 0;
    return 2;
}
static Grip        s_cG;
static uint32_t    s_cThLast = 0, s_cThAt = 0, s_cSqAt = 0;
static float       s_cSqK = 0;
static uint8_t     s_cBounces = 0;
static bool        s_aValid = false, s_sqWasThrown = false;
static int         s_aCx = 0, s_aHalfW = 0, s_aBot = 0;

// The toaster's half of each of these lives in pet_toaster.inc; the
// functions keep C1iPPY's names because main.cpp learned them first, and
// what they mean is "the pet that can be picked up".
static bool toasterOn() { return Squachy::petChoice() == Squachy::PetId::TOASTER && Squachy::toasterUnlocked(); }
static void toasterNoteCatch(uint8_t type);
static bool toasterHit(int x, int y);
static bool toasterCenter(int& x, int& y);
static void toasterPoke(uint32_t now);
static void toasterGrab(int x, int y, uint32_t now);
static void toasterDrag(int x, int y, uint32_t now);
static void toasterRelease(uint32_t now);
// The ball and chain, in pet_jail.inc: while it is out, a touch on the pet
// is a touch on the ball.
static void noteThrow(uint32_t now);
static bool jailShowing(uint32_t now);
// The ball is out either as the jailer or as the pet somebody chose.
static bool ballOut(uint32_t now) { return jailShowing(now) || Squachy::ballPetOn(); }
static bool ballHit(int x, int y);
static void ballPoke(uint32_t now);
static bool s_jGrab = false;
extern int16_t s_jX, s_jY, s_jR;

void noteCatch(uint8_t type) {
    if (toasterOn()) { toasterNoteCatch(type); return; }
    if (Squachy::petChoice() != Squachy::PetId::CLIPPY) return;
    s_cPending = (uint8_t)(type + 1);
}

// How long the balloon has actually been on screen, not how long ago the
// line was said: a line said mid-throw or mid-hop, or while another screen
// was up, still gets its full time once it can be seen.
static uint32_t s_cShownMs = 0, s_cLastNow = 0;
static bool     s_cFlung = false;           // this flight began with a throw, not a drop
static bool clipTalking() { return s_cLine && s_cShownMs < CLIP_TALK_MS; }

static void clipSay(const char* line, uint32_t now, bool caught) {
    s_cShownMs = 0;
    s_cLine = line; s_cSaidAt = now ? now : 1; s_cCaught = caught; s_cAnswered = caught;
}

bool clippyHit(int x, int y) {
    if (ballOut(millis())) return ballHit(x, y);
    if (toasterOn()) return toasterHit(x, y);
    if (Squachy::petChoice() != Squachy::PetId::CLIPPY || s_cX < -50) return false;
    return x >= s_cX - 4 && x <= s_cX + 30 && y >= s_cY - 4 && y <= s_cY + CLIP_H;
}

void clippyPoke(uint32_t now) {
    if (toasterOn()) { toasterPoke(now); return; }
    // A run of pokes walks the four lines; a pause starts it over.
    if (now - s_cPokeAt > 8000) s_cPoke = 0;
    s_cPokeAt = now;
    clipSay(CLIP_POKE[s_cPoke % 4], now, false);
    s_cAnswered = true;           // Squachy keeps out of this one
    s_cPoke++;
}

bool clippyCenter(int& x, int& y) {
    if (ballOut(millis())) { if (s_jX < -50) return false; x = s_jX; y = s_jY - s_jR; return true; }
    if (toasterOn()) return toasterCenter(x, y);
    if (Squachy::petChoice() != Squachy::PetId::CLIPPY || s_cX < -50) return false;
    x = s_cX + 11; y = s_cY + 24;
    return true;
}

void clippyGrab(int x, int y, uint32_t now) {
    if (ballOut(now)) { s_jGrab = true; return; }
    if (toasterOn()) { toasterGrab(x, y, now); return; }
    if (Squachy::petChoice() != Squachy::PetId::CLIPPY || s_cX < -50) return;
    s_cTh = ClipThrow::HELD;
    s_cfX = (float)s_cX; s_cfY = (float)s_cY;
    gripStart(s_cG, x, y, s_cX, s_cY, now);
    s_cHopAt = 0;
}

void clippyDrag(int x, int y, uint32_t now) {
    if (s_jGrab) return;
    if (toasterOn()) { toasterDrag(x, y, now); return; }
    if (s_cTh != ClipThrow::HELD) return;
    gripMove(s_cG, x, y, now);
}

static void clipLaunch(float vx, float vy, uint32_t now) {
    if (vx > CLIP_MAX_V) vx = CLIP_MAX_V;
    if (vx < -CLIP_MAX_V) vx = -CLIP_MAX_V;
    if (vy > CLIP_MAX_V) vy = CLIP_MAX_V;
    if (vy < -CLIP_MAX_V) vy = -CLIP_MAX_V;
    s_cvX = vx; s_cvY = vy;
    s_cTh = ClipThrow::AIR;
    s_cThAt = s_cThLast = now;
    s_cBounces = 0;
    s_cSqAt = 0;
}

void clippyRelease(uint32_t now) {
    if (s_jGrab) { s_jGrab = false; ballPoke(now); return; }
    if (toasterOn()) { toasterRelease(now); return; }
    if (s_cTh != ClipThrow::HELD) return;
    float vx, vy;
    switch (gripLetGo(s_cG, now, vx, vy)) {
        case 0:  s_cTh = ClipThrow::NONE; clippyPoke(now); break;
        case 1:  clipLaunch(vx, vy, now); s_cFlung = true; noteThrow(now); break;
        case 2:  clipLaunch(0, 0, now); s_cFlung = false; break;    // just let go: he falls from there
        default: s_cTh = ClipThrow::NONE; break;
    }
}

// ---- the wire ----------------------------------------------------------------
static uint8_t s_cGrid[CLIP_H][CLIP_W];     // 0 nothing, 1 outline, 2 wire, 3 highlight
static float   s_cSq = 1.0f;                // squash: 1 none, under 1 flattened

static inline void clipMap(float& x, float& y) {
    if (s_cSq == 1.0f) return;
    x = 11.0f + (x - 11.0f) / sqrtf(s_cSq);
    y = 42.0f - (42.0f - y) * s_cSq;
}
static void clipStamp(float x, float y, int r, uint8_t code) {
    clipMap(x, y);
    const int cx = (int)lroundf(x), cy = (int)lroundf(y);
    for (int dy = -r; dy <= r; dy++)
        for (int dx = -r; dx <= r; dx++) {
            if (dx * dx + dy * dy > r * r + r) continue;
            const int gx = cx + dx, gy = cy + dy;
            if (gx < 0 || gy < 0 || gx >= CLIP_W || gy >= CLIP_H) continue;
            if (s_cGrid[gy][gx] < code || code == 3) s_cGrid[gy][gx] = code;
        }
}
// The wire as one path: inner leg, small turn, up, top turn, the long leg,
// the big bottom turn, and the leg bent out into a pick with a hook on it,
// stamped on points roughly two thirds of a pixel apart. pass 0 lays the
// outline, 1 the wire, 2 the glints on every ninth point. One function told
// which pass rather than a template per pass: three copies of this cost
// 1.2 KB of flash for the same arithmetic.
static void clipPath(int wig, bool tipUp, uint8_t pass) {
    int i = 0;
    auto visit = [pass](float x, float y, int idx) {
        if (pass == 0) clipStamp(x, y, 2, 1);
        else if (pass == 1) clipStamp(x, y, 1, 2);
        else if (idx % 9 < 2) clipStamp(x - 0.6f, y - 0.6f, 0, 3);
    };
    auto line = [&](float x0, float y0, float x1, float y1) {
        const int n = (int)ceilf(hypotf(x1 - x0, y1 - y0) * 1.6f) + 1;
        for (int k = 0; k <= n; k++) visit(x0 + (x1 - x0) * k / n, y0 + (y1 - y0) * k / n, i++);
    };
    auto arc = [&](float cx, float cy, float r, float a0, float a1) {
        const int n = (int)ceilf(fabsf(a1 - a0) * r * 1.6f) + 6;
        for (int k = 0; k <= n; k++) { const float a = a0 + (a1 - a0) * k / n; visit(cx + cosf(a) * r, cy + sinf(a) * r, i++); }
    };
    const float P = 3.14159265f;
    line(7.5f, 15, 7.5f, 31); arc(10.5f, 31, 3, P, 0); line(13.5f, 31, 13.5f, 8);
    arc(8.75f, 8, 4.75f, 0, -P); line(4, 8, 4, 35); arc(10.5f, 35, 6.5f, P, 0);
    const float tx = 23.0f + wig, ty = tipUp ? 5.0f : 9.0f;
    line(17, 35, tx, ty);
    line(tx, ty, tx + 2, ty - 3);              // the hook
}

static void clipDraw(TFT_eSPI& t, int x, int y, uint32_t now, ClipFace face) {
    static const uint16_t WIRE = 0, OUT = 1, HI = 2;   // indices into col[]
    const uint16_t col[3] = { (uint16_t)0xAD98, (uint16_t)0x2125, (uint16_t)0xDF3E };
    int wig = (int)lroundf(sinf((float)now / 180.0f) * 1.2f);
    if (face == ClipFace::IDLE) wig = ((now % 2400) < 600) ? (int)lroundf(sinf((float)now / 60.0f)) : 0;   // raking pins
    if (face == ClipFace::SMUG) wig = (int)lroundf(sinf((float)now / 220.0f) * 2.0f);
    const bool surprised = face == ClipFace::SURPRISED;

    memset(s_cGrid, 0, sizeof s_cGrid);
    for (uint8_t pass = 0; pass < 3; pass++) clipPath(wig, surprised, pass);

    // A shadow to stand on, then the wire a run at a time.
    Theme::dimRegion(t, x + 1, y + 42, 22, 2, 128);
    for (int gy = 0; gy < CLIP_H; gy++) {
        int gx = 0;
        while (gx < CLIP_W) {
            const uint8_t c = s_cGrid[gy][gx];
            int e = gx + 1;
            while (e < CLIP_W && s_cGrid[gy][e] == c) e++;
            if (c) t.drawFastHLine(x + gx, y + gy, e - gx, col[c == 1 ? OUT : c == 2 ? WIRE : HI]);
            gx = e;
        }
    }

    // Eyes, on the two legs.
    const int R = 3;
    int look = surprised || face == ClipFace::TALK ? 0 : -1;    // toward Squachy
    int lid = 3;
    if (face == ClipFace::TALK) lid = 2;
    if (surprised) lid = 0;
    if (face == ClipFace::SMUG) lid = 4;
    if (face == ClipFace::IDLE && (now % 4700) < 130) lid = 2 * R + 1;   // blink
    const float ex[2] = { 4, 14 };
    for (int e = 0; e < 2; e++) {
        float fx = ex[e], fy = 17; clipMap(fx, fy);
        const int cx = x + (int)lroundf(fx), cy = y + (int)lroundf(fy);
        t.fillCircle(cx, cy, R + 1, Theme::BLACK);
        t.fillCircle(cx, cy, R, Theme::WHITE);
        const int pr = surprised ? 1 : 2;
        if (lid < 2 * R) t.fillRect(cx - 1 + look, cy, pr, pr, Theme::BLACK);
        if (lid > 0) {
            const int lh = lid < 2 * R + 1 ? lid : 2 * R + 1;
            t.fillRect(cx - R, cy - R, 2 * R + 1, lh, col[WIRE]);
            t.drawFastHLine(cx - R, cy - R + lh - 1, 2 * R + 1, Theme::BLACK);
        }
    }
    // Eyebrows: one flat, one raised. They bob while he talks.
    int lUp = 0, rUp = 0;
    if (face == ClipFace::TALK) { const int b = (int)((now / 220) % 3); lUp = b == 1; rUp = b == 2 ? 2 : 1; }
    if (surprised) { lUp = 3; rUp = 2; }
    if (face == ClipFace::SMUG) rUp = 2;
    float lx = 0, ly = 10, rx = 11, ry = 8; clipMap(lx, ly); clipMap(rx, ry);
    t.fillRect(x + (int)lx, y + (int)ly - lUp, 7, 2, Theme::BLACK);
    t.fillRect(x + (int)rx, y + (int)ry - rUp, 3, 2, Theme::BLACK);
    t.fillRect(x + (int)rx + 3, y + (int)ry - 1 - rUp, 4, 2, Theme::BLACK);
}

// His balloon: the help-balloon cream, a hard black edge and a tail to him.
// Above him, wrapped at 24 characters in three lines -- or, on a screen too
// short for that (the StickS3), BESIDE him at whatever width the side has,
// in up to five lines, so it never sits on Squachy's face.
static void petBalloon(TFT_eSPI& t, const char* s, int boxX, int boxY, int boxW, int screenW, int screenH) {
    const bool side = screenH < 200;
    int maxC = 24;
    bool right = true;
    if (side) {
        const int roomR = screenW - (boxX + boxW + 4) - 4, roomL = boxX - 6;
        right = roomR >= roomL;
        maxC = ((right ? roomR : roomL) - 10) / 6;
        if (maxC > 24) maxC = 24;
        if (maxC < 14) { maxC = 24; right = true; }     // no room either side: above after all
    }
    const uint8_t maxN = side ? 5 : 3;
    char lines[5][25];
    uint8_t n = 0;
    const char* p = s;
    while (*p && n < maxN) {
        while (*p == ' ') p++;
        size_t len = strlen(p);
        size_t take = len;
        if ((int)len > maxC) { take = (size_t)maxC; while (take > 0 && p[take] != ' ') take--; if (!take) take = (size_t)maxC; }
        memcpy(lines[n], p, take); lines[n][take] = '\0';
        n++; p += take;
    }
    if (!n) return;
    t.setTextSize(1);
    int tw = 0;
    for (uint8_t i = 0; i < n; i++) { const int w = t.textWidth(lines[i]); if (w > tw) tw = w; }
    const int bw = tw + 9, bh = n * 9 + 6;
    const uint16_t paper = (uint16_t)0xFFDA;
    if (side && maxC < 24 + 1 && (screenW - (boxX + boxW + 4) - 4 >= bw || boxX - 6 >= bw)) {
        int bx = right ? boxX + boxW + 4 : boxX - 4 - bw;
        int by = boxY + 6 - bh / 2;
        if (by < 16) by = 16;
        t.fillRect(bx, by, bw, bh, Theme::BLACK);
        t.fillRect(bx + 1, by + 1, bw - 2, bh - 2, paper);
        // A tail sideways, back to him.
        const int ty = by + bh - 9;
        for (int i = 0; i < 4; i++) {
            const int tx = right ? bx - i : bx + bw - 1 + i;
            t.drawFastVLine(tx, ty + i, 6 - 2 * i > 0 ? 6 - 2 * i : 1, paper);
            t.drawPixel(tx, ty + i - 1, Theme::BLACK);
        }
        t.setTextColor(Theme::BLACK, paper);
        for (uint8_t i = 0; i < n; i++) { t.setCursor(bx + 5, by + 3 + i * 9); t.print(lines[i]); }
        return;
    }
    const int anchorX = boxX + boxW / 2, bottomY = boxY - 4;
    int bx = anchorX - bw / 2;
    if (bx > screenW - bw - 2) bx = screenW - bw - 2;
    if (bx < 2) bx = 2;
    int by = bottomY - bh;
    if (by < 16) by = 16;
    t.fillRect(bx, by, bw, bh, Theme::BLACK);
    t.fillRect(bx + 1, by + 1, bw - 2, bh - 2, paper);
    int tx = anchorX;
    if (tx < bx + 5) tx = bx + 5;
    if (tx > bx + bw - 6) tx = bx + bw - 6;
    for (int i = 0; i < 4; i++) {
        t.drawFastHLine(tx - 2 + i, by + bh - 1 + i, 4 - i > 0 ? 4 - i : 1, paper);
        t.drawPixel(tx - 3 + i, by + bh - 1 + i, Theme::BLACK);
        t.drawPixel(tx + 2, by + bh - 1 + i, Theme::BLACK);
    }
    t.setTextColor(Theme::BLACK, paper);
    for (uint8_t i = 0; i < n; i++) { t.setCursor(bx + 5, by + 3 + i * 9); t.print(lines[i]); }
}

static void clipBalloon(TFT_eSPI& t, const char* s, int clipX, int clipY, int screenW, int screenH) {
    petBalloon(t, s, clipX, clipY, 22, screenW, screenH);
}

static void clippyTick(TFT_eSPI& t, uint32_t now, int screenW, int floorY) {
    int cx, halfW, top, bot;
    if (!Squachy::lastFootprint(cx, halfW, top, bot)) { s_cX = -100; return; }
    (void)top;
    bot = floorY;     // the pets' own line, not Squachy's feet
    const bool sqAway = Squachy::isHeld() || Squachy::thrown();
    if (sqAway) {
        if (!s_aValid) { s_cX = -100; return; }
        cx = s_aCx; halfW = s_aHalfW; bot = s_aBot;
    } else {
        s_aCx = cx; s_aHalfW = halfW; s_aBot = bot; s_aValid = true;
    }
    // Squachy going past in the air is worth a remark.
    const bool sqThrown = Squachy::thrown();
    if (sqThrown && !s_sqWasThrown && s_cTh == ClipThrow::NONE && !clipTalking() && random(0, 3) != 0) {
        clipSay(CLIP_SQTHROWN[random(0, 3)], now, false);
        s_cAnswered = true;
        s_cSmugAt = now;
    }
    s_sqWasThrown = sqThrown;
    uint32_t seenMs = (s_cLastNow && now - s_cLastNow < 100000u) ? now - s_cLastNow : 0;
    if (seenMs > 100) seenMs = 100;
    s_cLastNow = now;
    // Two spots beside him, the right-hand side when it has room.
    int a = cx + halfW + 2, b = a + 30;
    if (a + 30 > screenW - 2) { a = cx - halfW - 30; b = a - 30; }
    if (b < 2 || b + 30 > screenW - 2) b = a;

    if (const char* intro = Squachy::takeClippyIntro()) {
        clipSay(intro, now, false);
        s_cAnswered = true;
        Theme::showToast("C1iPPY", "your new pet. Settings > Pet", Theme::CYAN, 3500);
        s_cNextTip = now + 60000;
    }
    if (!s_cNextTip) s_cNextTip = now + 6000;
    if (!s_cNextHop) s_cNextHop = now + 30000 + (uint32_t)random(0, 20000);
    if (s_cPending && !clipTalking()) {
        const char* l = clipCatchLine((uint8_t)(s_cPending - 1));
        s_cPending = 0;
        if (l) { clipSay(l, now, true); s_cNextTip = now + 60000; }
    }
    if ((int32_t)(now - s_cNextTip) >= 0 && !clipTalking()) {
        const char* l = CLIP_TIPS[s_cTip++ % CLIP_TIPS_N];
        if (Squachy::visiting() && random(0, 2) == 0) l = CLIP_SQUAD[random(0, 3)];
        else if (Clock::night() && random(0, 3) == 0) l = CLIP_NIGHT[random(0, 3)];
        clipSay(l, now, false);
        s_cNextTip = now + 60000 + (uint32_t)random(0, 30000);
    }
    const bool talking = clipTalking();
    // Squachy gets a word in after a tip, now and then, and C1iPPY is smug
    // about it.
    if (s_cLine && !talking && !s_cAnswered) {
        s_cAnswered = true;
        if (random(0, 2) == 0) { Squachy::visitSay(CLIP_BACK[random(0, 6)]); s_cSmugAt = now; }
    }
    // A hop to the other spot, not while he is mid-sentence.
    if (!talking && (int32_t)(now - s_cNextHop) >= 0 && a != b) {
        s_cFromX = s_cSpotB ? (int16_t)b : (int16_t)a;
        s_cSpotB = !s_cSpotB;
        s_cHopAt = now;
        s_cNextHop = now + 30000 + (uint32_t)random(0, 20000);
    }
    const int to = s_cSpotB ? b : a;
    // ---- picked up, thrown, or getting over it ----
    if (s_cTh != ClipThrow::NONE) {
        const float floorY = (float)(bot - 44);
        const float leftX = 0.0f, rightX = (float)(screenW - 26), ceilY = 16.0f;
        ClipFace tf = ClipFace::SURPRISED;
        s_cSq = 1.0f;
        if (s_cTh == ClipThrow::HELD) {
            s_cfX = (float)(s_cG.fingerX - s_cG.offX);
            s_cfY = (float)(s_cG.fingerY - s_cG.offY);
            if (s_cfX < leftX) s_cfX = leftX;
            if (s_cfX > rightX) s_cfX = rightX;
            if (s_cfY < ceilY) s_cfY = ceilY;
            if (s_cfY > floorY) s_cfY = floorY;
            s_cSq = s_cG.moved ? 1.06f : 1.0f;      // dangling
            if (!s_cG.moved) tf = ClipFace::IDLE;
            // A finger that has stopped reporting has gone (see Squachy's
            // carry): let him drop rather than hang there.
            if (now - s_cG.gT > 600 && s_cG.moved) clipLaunch(0, 0, now);
        } else if (s_cTh == ClipThrow::AIR) {
            uint32_t dt = now - s_cThLast;
            if (dt > 60) dt = 60;
            s_cThLast = now;
            while (dt && s_cTh == ClipThrow::AIR) {
                const float st = (float)(dt > 8 ? 8 : dt);
                dt -= (dt > 8 ? 8 : dt);
                s_cvY += CLIP_G * st;
                s_cfX += s_cvX * st;
                s_cfY += s_cvY * st;
                if (s_cfX < leftX && s_cvX < 0)  { s_cfX = leftX;  s_cvX = -s_cvX * 0.6f; }
                if (s_cfX > rightX && s_cvX > 0) { s_cfX = rightX; s_cvX = -s_cvX * 0.6f; }
                if (s_cfY < ceilY && s_cvY < 0)  { s_cfY = ceilY;  s_cvY = -s_cvY * 0.4f; }
                if (s_cfY >= floorY && s_cvY > 0) {
                    // He is wire: springier than Squachy, so one more bounce.
                    s_cfY = floorY;
                    s_cSqAt = now;
                    s_cSqK = s_cvY > 0.9f ? 1.0f : s_cvY / 0.9f;
                    if (s_cBounces == 0 && s_cFlung) Squachy::petLandedAt((int)s_cfX + 11);
                    if (s_cvY > 0.2f && s_cBounces < 5) {
                        s_cvY = -s_cvY * 0.5f;
                        s_cvX *= 0.75f;
                        s_cBounces++;
                    } else {
                        s_cvY = 0;
                        s_cTh = ClipThrow::SIT;
                        s_cThAt = now;
                        // Said once he is down, where it can be read for
                        // as long as any other line.
                        if (s_cFlung) {
                            const char* streak = clipStreakLine(now);
                            clipSay(streak ? streak : (s_cBounces >= 2 ? CLIP_LANDED[random(0, CLIP_LANDED_N)] : CLIP_THROWN[random(0, CLIP_THROWN_N)]), now, false);
                            s_cAnswered = true;
                            s_cFlung = false;
                        }
                    }
                }
            }
            if (s_cTh == ClipThrow::AIR && now - s_cThAt > 5000) { s_cfY = floorY; s_cTh = ClipThrow::SIT; s_cThAt = now; }
        } else if (s_cTh == ClipThrow::SIT) {
            tf = (now - s_cThAt < 500) ? ClipFace::SURPRISED : ClipFace::SMUG;
            if (now - s_cThAt > 900) {
                // Home, by the usual hop.
                s_cTh = ClipThrow::NONE;
                s_cFromX = (int16_t)lroundf(s_cfX);
                s_cHopAt = now;
            }
        }
        if (s_cTh != ClipThrow::NONE) {
            if (s_cSqAt && now - s_cSqAt < CLIP_LAND_MS + 60)
                s_cSq = 1.0f - 0.25f * s_cSqK * (1.0f - (float)(now - s_cSqAt) / (float)(CLIP_LAND_MS + 60));
            s_cX = (int16_t)lroundf(s_cfX);
            s_cY = (int16_t)lroundf(s_cfY);
            clipDraw(t, s_cX, s_cY, now, tf);
            if (clipTalking() && s_cTh == ClipThrow::SIT) {
                clipBalloon(t, s_cLine, s_cX, s_cY, screenW, t.height());
                s_cShownMs += seenMs;
            }
            return;
        }
    }
    float x = (float)to, hop = 0.0f;
    s_cSq = 1.0f;
    ClipFace face = ClipFace::IDLE;
    const uint32_t since = now - s_cHopAt;
    if (s_cHopAt && since < CLIP_HOP_MS) {
        const float k = (float)since / (float)CLIP_HOP_MS;
        x = (float)s_cFromX + (float)(to - s_cFromX) * k;
        hop = -sinf(k * 3.14159265f) * 24.0f;
        s_cSq = 1.0f + 0.08f * sinf(k * 3.14159265f);
        face = ClipFace::SURPRISED;
    } else {
        if (s_cHopAt && since < CLIP_HOP_MS + CLIP_LAND_MS) s_cSq = 0.82f + 0.18f * (float)(since - CLIP_HOP_MS) / (float)CLIP_LAND_MS;
        if (talking) face = (s_cCaught && now - s_cSaidAt < 900) ? ClipFace::SURPRISED : ClipFace::TALK;
        else if (s_cSmugAt && now - s_cSmugAt < CLIP_SMUG_MS) face = ClipFace::SMUG;
    }
    s_cX = (int16_t)lroundf(x);
    s_cY = (int16_t)(bot - 44 + (int)lroundf(hop));
    clipDraw(t, s_cX, s_cY, now, face);
    if (talking && !(s_cHopAt && since < CLIP_HOP_MS)) {
        clipBalloon(t, s_cLine, s_cX, s_cY, screenW, t.height());
        s_cShownMs += seenMs;
    }
}

#include "pet_toaster.inc"
#include "pet_jail.inc"

void reset() {
    toasterReset();
    jailReset();
    s_phase  = Phase::AWAY;
    s_x      = -100.0f;
    s_nextAt = 0;
    s_yPhase = YPhase::AWAY;
    s_yX     = -100.0f;
    s_yNextAt = 0;
    s_yNap   = false;
    s_yAnswered = false;
    s_yFlinchAt = 0;
    s_yLine  = nullptr;
    s_cX     = -100;
    s_cHopAt = 0;
    s_cTh    = ClipThrow::NONE;
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

// One visit: in from the left, a beat standing beside Squachy, and out the
// other side. Everything hangs off his footprint the way the lil guy's does.
static void yetiTick(TFT_eSPI& t, uint32_t now, int screenW, int cx, int halfW, int bot) {
    const int baseY = bot;                       // his feet, on Squachy's floor
    switch (s_yPhase) {
    case YPhase::AWAY: {
        if (!s_yNextAt) s_yNextAt = now + 9000u;
        if (now < s_yNextAt) return;
        // What kind of visit this is gets decided before he sets off, so the
        // walk-on already knows where it is going.
        const uint8_t pick = (uint8_t)random(0, 100);
        s_yNap   = (pick < 22);                       // roughly one in five
        s_yQuip  = (uint8_t)random(0, YETI_QUIPS_N);
        // A third of the time, if he has something to say about where he is,
        // he says that instead of a line from the general set.
        const char* place = placeLine((uint8_t)random(0, 3));
        s_yLine  = (place && random(0, 3) == 0) ? place : YETI_QUIPS[s_yQuip];
        s_yAnswered = false;
        s_yFlinchAt = 0;
        s_yX     = -(float)Theme::YETI_W;
        s_yPhase = YPhase::IN;
        s_yAt    = now;
        return;
    }
    case YPhase::IN: {
        const float target = (float)(cx - halfW - Theme::YETI_W / 2 - 8);
        s_yX += YETI_PXMS * (float)(now - s_yAt);
        s_yAt = now;
        if (s_yX >= target) {
            s_yX = target;
            s_yPhase = s_yNap ? YPhase::NAP : YPhase::TALK;
            s_yAt = now;
            if (s_yNap) s_yLine = "YETI NAP.";
        }
        break;
    }
    case YPhase::TALK:
        if (now - s_yAt >= YETI_TALK_MS) { s_yPhase = YPhase::OUT; s_yAt = now; }
        break;
    case YPhase::NAP:
        if (now - s_yAt >= YETI_NAP_MS) { s_yPhase = YPhase::OUT; s_yAt = now; }
        break;
    case YPhase::OUT:
        s_yX += YETI_PXMS * (float)(now - s_yAt);
        s_yAt = now;
        if (s_yX > (float)screenW + Theme::YETI_W) {
            s_yPhase  = YPhase::AWAY;
            // Same rarity the lil guy keeps to: the behaviour is the joke,
            // and turning up every ten seconds is how a joke stops being one.
            s_yNextAt = now + 45000u + (uint32_t)random(0, 45000);
        }
        break;
    }
    if (s_yPhase == YPhase::AWAY) return;

    // ---- the host gets a turn --------------------------------------------
    // A beat after the yeti's line lands, Squachy answers it -- or just
    // cracks up, which is funnier about a third of the time and costs a line
    // nobody has to write. He uses his OWN bubble, so the two are plainly
    // different voices rather than one caption box changing hands.
    const bool standing = (s_yPhase == YPhase::TALK || s_yPhase == YPhase::NAP);
    if (standing && !s_yAnswered && now - s_yAt >= YETI_ANSWER_MS) {
        s_yAnswered = true;
        const uint8_t roll = (uint8_t)random(0, 100);
        if (roll < 30) {
            Squachy::visitLaugh(now);
        } else {
            Squachy::visitSay(s_yNap ? NAP_REPLY[random(0, 3)]
                                     : YETI_REPLY[random(0, YETI_REPLY_N)]);
            // And it makes him jump. Not at you -- at the small one who
            // just started talking next to him.
            if (!s_yNap) s_yFlinchAt = now;
        }
    }

    Theme::YetiPose pose = Theme::YetiPose::WALK;
    if (s_yPhase == YPhase::NAP)                             pose = Theme::YetiPose::NAP;
    else if (s_yFlinchAt && now - s_yFlinchAt < YETI_FLINCH_MS) pose = Theme::YetiPose::FLINCH;
    else if (s_yPhase == YPhase::TALK)                       pose = Theme::YetiPose::STAND;
    Theme::drawYeti(t, (int)s_yX, baseY, now, pose);

    // His bubble is up for the first stretch of a nap too -- he announces it
    // and then goes quiet, which is the whole joke.
    const bool talking = (s_yPhase == YPhase::TALK) ||
                         (s_yPhase == YPhase::NAP && now - s_yAt < 1800u);
    if (talking && s_yLine) {
        t.setTextSize(1);
        const int bw = t.textWidth(s_yLine) + 8;
        int bx = (int)s_yX + Theme::YETI_W / 2 - bw / 2;
        int by = baseY - Theme::YETI_H - 16;
        if (by < 22) by = 22;
        bubble(t, bx, by, screenW, s_yLine);
    }
}


void tick(TFT_eSPI& t, uint32_t now, int screenW, int bandTop, int bandBottom) {
    // Serving time: the pet is in custody, and the ball is out instead. Or
    // he IS the pet, by choice, which is its own kind of sentence.
    if (ballOut(now)) {
        s_phase = Phase::AWAY; s_yPhase = YPhase::AWAY; s_cX = -100; s_tX = -100;
        jailTick(t, now, screenW);
        return;
    }
    s_jX = -100;
    const Squachy::PetId which = Squachy::petChoice();
    if (which == Squachy::PetId::CLIPPY) {
        s_phase = Phase::AWAY; s_yPhase = YPhase::AWAY; s_tX = -100;
        if (Squachy::clippyUnlocked()) clippyTick(t, now, screenW, bandBottom);
        return;
    }
    s_cX = -100;
    if (which == Squachy::PetId::TOASTER) {
        s_phase = Phase::AWAY; s_yPhase = YPhase::AWAY;
        if (Squachy::toasterUnlocked()) toasterTick(t, now, screenW, bandBottom);
        return;
    }
    s_tX = -100;
    if (!Squachy::petUnlocked() || which == Squachy::PetId::OFF) {
        s_phase  = Phase::AWAY;
        s_yPhase = YPhase::AWAY;
        return;
    }
    if (which == Squachy::PetId::YETI) {
        int ycx, yhalfW, ytop, ybot;
        if (!Squachy::lastFootprint(ycx, yhalfW, ytop, ybot)) return;
        if (Squachy::isHeld()) { s_yPhase = YPhase::AWAY; return; }
        (void)ytop; (void)ybot;
        yetiTick(t, now, screenW, ycx, yhalfW, bandBottom);
        return;
    }
    s_yPhase = YPhase::AWAY;

    // Where Squachy actually is THIS frame. Everything below hangs off
    // this rather than off constants, so bob, squash and his idle amble
    // all come along for free.
    int cx, halfW, top, bot;
    if (!Squachy::lastFootprint(cx, halfW, top, bot)) return;
    bot = bandBottom;     // the pets' own line, not Squachy's feet

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
