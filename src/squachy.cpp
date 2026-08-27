// SquachWatch-CYD — Squachy implementation
#include "squachy.h"
#include "theme.h"
#include <Arduino.h>

namespace Squachy {

enum class Mood : uint8_t { IDLE, WAVE, SHOCKED, BOUNCE };

// ---- Line banks (string literals live in flash, not RAM) ----
static const char* IDLE_LINES[] = {
    "Stay squachy out there.",
    "Been in these walls for years.",
    "Don't be a skid. Learn the craft.",
    "This WiFi is giving me ideas.",
    "I'm everywhere and nowhere.",
    "They built cameras. I built better hiding spots.",
    "Big feet, bigger opsec.",
    "Too quiet. I love it.",
    "Snacks fuel good opsec. Pack extra.",
    "This screen's my new hideout.",
    "Bigfoot sightings up 40% lately.",
    "Lockpicking: problem solving, but metal.",
    "The best hack teaches someone.",
    "I contain multitudes and RF signals.",
    "Every good cryptid needs a hobby.",
    "Scanning is my cardio.",
    "Cryptid by night, operator by day.",
    "Nobody suspects the sasquatch.",
};

static const char* ENCOURAGE_LINES[] = {
    "Nothing detected? Boring. Go walk.",
    "Take me outside, I get restless.",
    "Every parking lot's got a story.",
    "Go check the block for Flock cams.",
    "A quiet scan needs new scenery.",
    "Adventure's out there. So are ALPRs.",
    "Get up. Go touch grass. Bring me.",
    "Field work builds character.",
};

static const char* BORED_LINES[] = {
    "...anybody there?",
    "I could use a walk. Just saying.",
    "Standing by. Very patiently.",
    "Send help. Or snacks.",
};

static const char* BOOT_LINES[] = {
    "SquachWatch online. Let's find something.",
    "Booted. Don't just stare at your phone.",
};

static const char* LOG_OPEN_LINES[] = {
    "Snooping the log? Bold. Respect.",
    "This is where the receipts live.",
};

static const char* LOG_CLEAR_LINES[] = {
    "Log wiped. Fresh start, cryptid style.",
    "Evidence? Never heard of her.",
};

static const char* ROTATE_LINES[] = {
    "Whoa, easy on the spins.",
    "Now THAT'S a plot twist.",
    "I get dizzy but I never complain.",
};

struct DetLines { const char* a; const char* b; };
// Indexed by DetectionType (UNKNOWN..CAMERA), matches state.h ordering.
static const DetLines DET_LINES[] = {
    { "Something's out there.",              "Unknown signal. Stay sharp." },      // UNKNOWN
    { "Flock spotted. Big Brother waves.",   "ALPR camera. You're cataloged." },   // FLOCK
    { "Axon gear nearby. Mind your manners.","Body cam up. Smile back." },         // AXON
    { "Ray-Bans that snitch. Wild times.",   "Someone's glasses are recording." }, // META
    { "Card skimmer! Don't swipe there.",    "Rude little Bluetooth device." },    // SKIMMER
    { "Gunshot sensor pinged. Stay sharp.",  "Raven detected. Eyes open." },       // RAVEN
    { "AirTag nearby. Hope it's yours.",     "Something's tracking something." },  // AIRTAG
    { "Eyes in the sky. Literally.",         "Drone up. Wave if ready." },         // DRONE
    { "Plate reader spotted. Classic.",      "ALPR sees you. Smile." },            // ALPR
    { "Camera detected. Smile, legend.",     "Someone's watching. Look good." },   // CAMERA
};
static const uint8_t DET_LINES_N = sizeof(DET_LINES) / sizeof(DET_LINES[0]);

// ---- Runtime state ----
static Mood     mood            = Mood::IDLE;
static uint32_t moodUntil       = 0;
static const char* bubbleText   = nullptr;
static uint32_t bubbleUntil     = 0;
static uint32_t nextIdleAt      = 4000;
static uint32_t lastInteraction = 0;

static const char* pick(const char* const* arr, int n) {
    return arr[random(0, n)];
}

static void say(const char* line, uint32_t ms) {
    bubbleText  = line;
    bubbleUntil = millis() + ms;
}

// Every bubble stays up at least this long, no matter which line fires.
static const uint32_t MIN_BUBBLE_MS = 4000;

void trigger(Event evt, DetectionType dt) {
    uint32_t now = millis();
    lastInteraction = now;
    switch (evt) {
        case Event::DETECTION: {
            mood = Mood::SHOCKED;
            moodUntil = now + 1400;
            uint8_t idx = (uint8_t)dt;
            if (idx >= DET_LINES_N) idx = 0;
            say(random(0, 2) ? DET_LINES[idx].a : DET_LINES[idx].b, 4500);
            break;
        }
        case Event::LOG_OPENED:
            say(pick(LOG_OPEN_LINES, 2), MIN_BUBBLE_MS);
            break;
        case Event::LOG_CLEARED:
            say(pick(LOG_CLEAR_LINES, 2), MIN_BUBBLE_MS);
            break;
        case Event::ROTATED:
            say(pick(ROTATE_LINES, 3), MIN_BUBBLE_MS);
            break;
        case Event::BOOTED:
            say(pick(BOOT_LINES, 2), MIN_BUBBLE_MS);
            break;
    }
    nextIdleAt = now + 15000 + random(0, 15000);
}

// ---- Drawing ----
// Tracks the previous frame's bubble footprint so we can erase exactly
// that rectangle (and nothing more) when the bubble changes or goes
// away — the rest of the row stays untouched, so matrix rain shows
// through whenever Squachy isn't actively saying something.
static int  lastBubbleX = 0, lastBubbleW = 0;
static bool hadBubble   = false;

static void drawBubble(TFT_eSPI& t, int cx, int topY, const char* text) {
    t.setTextSize(1);
    t.setTextWrap(false);
    int tw = t.textWidth(text);
    int bw = tw + 10;
    int bh = 14;
    int bx = cx - bw / 2;
    t.fillRoundRect(bx, topY, bw, bh, 3, Theme::BG);
    t.drawRoundRect(bx, topY, bw, bh, 3, Theme::VAPOR_PINK);
    t.setTextColor(Theme::WHITE, Theme::BG);
    t.setCursor(bx + 5, topY + 3);
    t.print(text);
    lastBubbleX = bx;
    lastBubbleW = bw;
}

// Squachy's base design is ~68px tall (crest to shadow) at scale 1.0.
static const int BASE_HEIGHT = 68;

// Draws Squachy at an already-animated anchor (hy = head-top Y for this
// exact frame). Bob is computed once in tick() so it can also drive the
// dirty-rect clear that runs before this is called.
static void drawBody(TFT_eSPI& t, int cx, int hy, int headTopY, uint32_t now, Mood m, float scale) {
    auto S = [scale](int v) { return (int)(v * scale); };
    int cx2 = cx;

    using namespace Theme;

    // Shadow (fixed, doesn't bob)
    t.fillEllipse(cx2, headTopY + S(62), S(18), S(4), blend(BG, FUR_DARK, 70));

    // Legs + big bigfoot feet
    t.fillRect(cx2 - S(10), hy + S(40), S(8), S(10), FUR_MAIN);
    t.fillRect(cx2 + S(2),  hy + S(40), S(8), S(10), FUR_MAIN);
    t.fillRoundRect(cx2 - S(13), hy + S(49), S(12), S(6), 2, FUR_LIGHT);
    t.fillRoundRect(cx2 + S(1),  hy + S(49), S(12), S(6), 2, FUR_LIGHT);

    // Body — broad, stocky torso instead of a slim rounded rect.
    t.fillRoundRect(cx2 - S(15), hy + S(23), S(30), S(18), S(5), FUR_MAIN);
    t.fillRect(cx2 - S(15), hy + S(23), S(5), S(18), FUR_LIGHT);
    t.fillRect(cx2 + S(10), hy + S(23), S(5), S(18), FUR_LIGHT);

    // Arms — long, ape-like, hanging past the waist. Depend on mood.
    if (m == Mood::WAVE) {
        float wa = -1.0f + sinf((float)(now % 400) / 400.0f * 6.2831853f) * 0.5f;
        float ex = cx2 + S(13) + cosf(wa) * (18.0f * scale);
        float ey = hy + S(28) + sinf(wa) * (18.0f * scale);
        t.drawWideLine(cx2 + S(11), hy + S(28), ex, ey, S(7), FUR_LIGHT);
        t.fillRoundRect(cx2 - S(18), hy + S(22), S(8), S(22), S(3), FUR_LIGHT);
    } else if (m == Mood::SHOCKED) {
        t.drawWideLine(cx2 - S(11), hy + S(26), cx2 - S(23), hy + S(10), S(7), FUR_LIGHT);
        t.drawWideLine(cx2 + S(11), hy + S(26), cx2 + S(23), hy + S(10), S(7), FUR_LIGHT);
    } else {
        t.fillRoundRect(cx2 - S(18), hy + S(22), S(8), S(22), S(3), FUR_LIGHT);
        t.fillRoundRect(cx2 + S(10), hy + S(22), S(8), S(22), S(3), FUR_LIGHT);
    }

    // Head — broader jaw than before, brow ridge over the eyes.
    t.fillRoundRect(cx2 - S(15), hy, S(30), S(24), S(7), FUR_LIGHT);
    t.fillRoundRect(cx2 - S(12), hy + S(2), S(24), S(19), S(5), FUR_MAIN);
    t.fillRoundRect(cx2 - S(9),  hy + S(7), S(18), S(11), S(4), SKIN_TAN);

    // Sagittal crest (the pronounced skull peak real bigfoot sightings
    // always mention) plus a couple of smaller shaggy fringe tufts.
    t.fillTriangle(cx2 - S(6), hy + S(2), cx2, hy - S(14), cx2 + S(6), hy + S(2), FUR_LIGHT);
    t.fillTriangle(cx2 - S(13), hy + S(3), cx2 - S(9), hy - S(4), cx2 - S(5), hy + S(3), FUR_LIGHT);
    t.fillTriangle(cx2 + S(5),  hy + S(3), cx2 + S(9), hy - S(4), cx2 + S(13),hy + S(3), FUR_LIGHT);

    // Ears — small and tucked close, like a real sasquatch rather than
    // a cartoon animal's.
    t.fillCircle(cx2 - S(15), hy + S(13), S(3), FUR_MAIN);
    t.fillCircle(cx2 + S(15), hy + S(13), S(3), FUR_MAIN);
    t.fillCircle(cx2 - S(15), hy + S(13), S(1), SKIN_DARK);
    t.fillCircle(cx2 + S(15), hy + S(13), S(1), SKIN_DARK);

    // Blush
    t.fillCircle(cx2 - S(8), hy + S(15), S(2), VAPOR_PINK);
    t.fillCircle(cx2 + S(8), hy + S(15), S(2), VAPOR_PINK);

    // Eyes / sunglasses + mouth
    if (m == Mood::SHOCKED) {
        t.fillEllipse(cx2 - S(5), hy + S(9), S(3), S(4), WHITE);
        t.fillEllipse(cx2 + S(5), hy + S(9), S(3), S(4), WHITE);
        t.fillCircle(cx2 - S(5), hy + S(9), S(1), BLACK);
        t.fillCircle(cx2 + S(5), hy + S(9), S(1), BLACK);
        t.fillEllipse(cx2, hy + S(18), S(4), S(5), BLACK);
    } else {
        bool blink = ((now / 2200) % 40) < 3;
        uint16_t lens = blink ? BLACK : blend(BG, CYAN, 60);
        t.fillRoundRect(cx2 - S(12), hy + S(6), S(10), S(7), 2, BLACK);
        t.fillRoundRect(cx2 + S(2),  hy + S(6), S(10), S(7), 2, BLACK);
        t.fillRect(cx2 - S(2), hy + S(8), S(4), S(2), BLACK);
        t.fillRoundRect(cx2 - S(11), hy + S(7), S(8), S(5), 1, lens);
        t.fillRoundRect(cx2 + S(3),  hy + S(7), S(8), S(5), 1, lens);

        // A glint sweeps across each lens (skipped mid-blink) so the
        // shades read as reflective glass instead of a flat fill.
        if (!blink) {
            float sweep = (float)(now % 2400) / 2400.0f;
            int gx = (int)(sweep * 6.0f);
            t.drawFastVLine(cx2 - S(11) + S(1 + gx), hy + S(7), S(4), WHITE);
            t.drawFastVLine(cx2 + S(3)  + S(1 + gx), hy + S(7), S(4), WHITE);
        }

        // Mouth: resting smile most of the time, or an open/close
        // "talking" flap while a speech bubble is actually up.
        bool talking = bubbleText && now < bubbleUntil;
        if (talking && ((now / 160) % 2) == 0) {
            t.fillRoundRect(cx2 - S(8), hy + S(15), S(16), S(9), S(3), BLACK);
            t.fillRect(cx2 - S(6), hy + S(16), S(12), S(2), WHITE);
            t.fillEllipse(cx2, hy + S(21), S(5), S(3), PINK);
        } else {
            t.fillRoundRect(cx2 - S(8), hy + S(16), S(16), S(7), S(3), BLACK);
            t.fillRect(cx2 - S(6), hy + S(17), S(12), S(2), WHITE);
            t.fillRect(cx2 - S(6), hy + S(19), S(12), S(3), PINK);
        }
    }
}

// Tight bounding box (unscaled, relative to hy) for whatever drawBody()
// actually paints in a given mood — used to clear only his real
// footprint between frames instead of a generic worst-case box. Arms
// are the only part that changes the horizontal extent by mood; the
// vertical extent (hair tuft peak to feet) is the same for all of them.
static void bodyBounds(Mood m, int& xMin, int& xMax, int& yMin, int& yMax) {
    yMin = -18; yMax = 58; // -18 covers the sagittal crest peak
    switch (m) {
        case Mood::WAVE:    xMin = -21; xMax = 32; break;
        case Mood::SHOCKED: xMin = -30; xMax = 30; break;
        default:            xMin = -21; xMax = 21; break; // long resting arms are the widest feature
    }
}

void tick(TFT_eSPI& t, int cx, int topY, int availHeight, uint32_t now) {
    // Expire a triggered mood back to idle
    if (mood != Mood::IDLE && now > moodUntil) mood = Mood::IDLE;

    // Random idle fun: bounce/wave + a quip, only when nothing else
    // triggered a reaction recently.
    if (mood == Mood::IDLE && now >= nextIdleAt) {
        bool longIdle = (now - lastInteraction) > 90000;
        if (longIdle && random(0, 3) == 0) {
            say(pick(BORED_LINES, 4), MIN_BUBBLE_MS);
        } else if (random(0, 4) == 0) {
            say(pick(ENCOURAGE_LINES, 8), MIN_BUBBLE_MS);
        } else {
            say(pick(IDLE_LINES, 18), MIN_BUBBLE_MS);
        }
        mood = random(0, 2) ? Mood::WAVE : Mood::BOUNCE;
        moodUntil = now + 1200;
        nextIdleAt = now + 12000 + random(0, 18000);
    }

    // Maximize Squachy's size to whatever vertical room the caller says
    // is free (title bar to status line), after reserving a row for the
    // speech bubble. Clamped to keep his proportions from getting
    // blocky-huge or unreadably tiny on extreme screen sizes.
    const int bubbleRowH = 16;
    int charAvail = availHeight - bubbleRowH;
    if (charAvail < 40) charAvail = 40;
    float scale = (float)charAvail / (float)BASE_HEIGHT;
    if (scale < 1.0f) scale = 1.0f;
    if (scale > 2.6f) scale = 2.6f;

    int headTopY = topY + bubbleRowH;

    // Idle bob runs noticeably quicker than a resting breathing rate —
    // he should read as lively even when nothing's happening. Bob
    // amplitude scales with him so it stays proportional when he's big.
    float bobAmt   = ((mood == Mood::BOUNCE) ? 9.0f : 3.0f) * scale;
    float bobSpeed = (mood == Mood::BOUNCE) ? 220.0f : 1100.0f;
    float bob = sinf((float)(now % (uint32_t)bobSpeed) / bobSpeed * 6.2831853f) * bobAmt;
    if (mood == Mood::BOUNCE) bob = -fabsf(bob); // hop upward only
    int hy = headTopY + (int)bob;

    // He's drawn directly over the matrix rain with fully opaque shapes
    // (no background box) so the rain shows through around him — but
    // that means his own previous-frame pixels only get cleaned up
    // where this frame's shapes happen to land on top of them. Bob
    // motion shifts him a few px between frames, so without an explicit
    // clear, the sliver each bob step doesn't re-cover is left behind
    // as a smear. Erase exactly his last-frame footprint (not a fixed
    // box sized for his whole motion range) right before redrawing —
    // that's just a thin trailing edge, not a static black rectangle.
    static int lastHy = -30000;
    static float lastScale = 1.0f;
    static Mood lastMood = Mood::IDLE;
    if (lastHy != -30000) {
        int xMin, xMax, yMin, yMax;
        bodyBounds(lastMood, xMin, xMax, yMin, yMax);
        int bx = cx + (int)(xMin * lastScale);
        int bw = (int)((xMax - xMin) * lastScale);
        int by = lastHy + (int)(yMin * lastScale);
        int bh = (int)((yMax - yMin) * lastScale);
        t.fillRect(bx, by, bw, bh, Theme::BG);
    }
    drawBody(t, cx, hy, headTopY, now, mood, scale);
    lastHy = hy;
    lastScale = scale;
    lastMood = mood;

    // The bubble does need clearing (its width changes with the text),
    // but only its own footprint — erase the previous frame's exact
    // rectangle, not a fixed-size strip across the whole row. Covers
    // both "bubble went away" and "bubble changed to a shorter one".
    bool showBubble = bubbleText && now < bubbleUntil;
    if (hadBubble) {
        t.fillRect(lastBubbleX, topY, lastBubbleW, 14, Theme::BG);
    }
    if (showBubble) {
        drawBubble(t, cx, topY, bubbleText);
    }
    hadBubble = showBubble;
}

} // namespace Squachy
