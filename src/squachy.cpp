// SquachWatch-CYD — Squachy implementation
#include "squachy.h"
#include "theme.h"
#include "signatures.h"
#include <Arduino.h>
#include <Preferences.h>

namespace Squachy {

enum class Mood : uint8_t { IDLE, WAVE, SHOCKED, BOUNCE, SLEEPY };

// Which reaction pose a SHOCKED mood strikes — varies by what triggered
// it so a detection actually reads differently depending on the type,
// instead of every alert getting the same generic startle.
enum class ReactPose : uint8_t { STARTLED, HANDS_UP, COVER_FACE, POINT_SHADES, DISGUST, LOOK_UP, LOOK_AROUND };

static ReactPose reactPoseFor(DetectionType t) {
    switch (t) {
        case DetectionType::AXON:    return ReactPose::HANDS_UP;    // "don't shoot" — it's law enforcement gear
        case DetectionType::FLOCK:
        case DetectionType::ALPR:
        case DetectionType::CAMERA:  return ReactPose::COVER_FACE;  // something's taking his picture
        case DetectionType::META:    return ReactPose::POINT_SHADES;// smart glasses — he points at his own shades
        case DetectionType::SKIMMER: return ReactPose::DISGUST;     // a skimmer is just gross
        case DetectionType::DRONE:   return ReactPose::LOOK_UP;     // eyes in the sky
        case DetectionType::AIRTAG:
        case DetectionType::SAMSUNG_TAG:
        case DetectionType::GOOGLE_TAG: return ReactPose::LOOK_AROUND; // something's tracking him
        default:                     return ReactPose::STARTLED;   // UNKNOWN, RAVEN
    }
}

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
    "Locks keep out the polite. I'm not polite.",
    "The best hack teaches someone.",
    "I contain multitudes and RF signals.",
    "Every good cryptid needs a hobby.",
    "This counts as cardio. Fight me.",
    "Cryptid by night, operator by day.",
    "Nobody suspects the Sasquach.",
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

// Tap-to-pet reactions — a minority get the milestone treatment below
// instead (see PET_MILESTONES).
static const char* PET_LINES[] = {
    "Ooh, right there.",
    "Personal space? Never heard of it.",
    "Petting a cryptid. Bold move.",
    "This is why they never get good photos of me.",
    "Okay, ONE more. Don't tell the others.",
    "You'd pet Bigfoot too. Don't lie.",
    "Cryptid, not a house pet. But okay.",
    "Ten out of ten, would be spotted again.",
};

// A yawn/nap moment for when nothing's happened in a long while — a
// visual state, not just another line bank (see the SLEEPY mood in
// drawBody).
static const char* SLEEPY_LINES[] = {
    "*yawn* ...still here.",
    "Cryptid power-nap. Don't tell anyone.",
    "Resting my eyes. Not my watch.",
    "Zzz... wake me if something's actually out there.",
};

// A few over-the-top lines for the rare full "party mode" flourish
// (see s_legendary below) — bigger occasion than the plain shimmer.
static const char* PARTY_LINES[] = {
    "PARTY MODE. You're welcome.",
    "Whoa. Did you just see that?",
    "This is a disco now. No refunds.",
    "Cryptid rave. Don't tell anyone.",
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
    { "Samsung tag pinged. Somebody's tagged.","Galaxy SmartTag nearby. Hm." },     // SAMSUNG_TAG
    { "Google's tracking network says hi.",  "Find My Device? Found by me." },     // GOOGLE_TAG
};
static const uint8_t DET_LINES_N = sizeof(DET_LINES) / sizeof(DET_LINES[0]);

// Lifetime-detection-count thresholds Squachy calls out by name. Bigger
// than any of these and he just keeps quiet about the exact number.
static const uint32_t MILESTONES[] = { 10, 25, 50, 100, 250, 500, 1000, 2500, 5000 };
static const uint8_t  MILESTONES_N = sizeof(MILESTONES) / sizeof(MILESTONES[0]);

// Same idea for lifetime pet count — a much lower bar than detections,
// since petting is its own little game rather than the main point.
static const uint32_t PET_MILESTONES[] = { 1, 10, 25, 50, 100, 250, 500 };
static const uint8_t  PET_MILESTONES_N = sizeof(PET_MILESTONES) / sizeof(PET_MILESTONES[0]);

// ---- Runtime state ----
static Mood          mood            = Mood::IDLE;
static uint32_t      moodUntil       = 0;
static const char*   bubbleText      = nullptr;
static uint32_t      bubbleUntil     = 0;
static uint32_t      nextIdleAt      = 4000;
static uint32_t      lastInteraction = 0;
static DetectionType s_reactType     = DetectionType::UNKNOWN;
static uint32_t      s_lastMilestone = 0;
static bool          s_milestoneInit = false;
static char          s_milestoneBuf[48];
static char          s_detBuf[56];

// A very rare idle flourish — his fur shimmers through the vaporwave
// palette for a few seconds, plus (see tick()/drawPartyFx) a full
// rainbow wash and confetti across his whole region. Purely cosmetic,
// no gameplay meaning.
static bool     s_legendary      = false;
static uint32_t s_legendaryUntil = 0;

// Tap-to-pet: a lifetime count persisted across reboots (its own NVS
// namespace, loaded lazily on first trigger() call rather than a
// dedicated init() — there wasn't one before and every event already
// funnels through trigger()), plus a little floating-heart flourish
// while the "petted" reaction is showing.
static Preferences s_petPrefs;
static bool        s_petPrefsLoaded = false;
static uint32_t    s_petCount       = 0;
static uint32_t    s_petFxStart     = 0;
static uint32_t    s_petFxUntil     = 0;

// Confetti for the rare "party mode" flourish (see s_legendary) —
// seeded once when it triggers, then just falls and recycles for the
// duration of the effect.
static const uint8_t CONFETTI_N = 12;
static float   s_cfx[CONFETTI_N], s_cfy[CONFETTI_N], s_cfvy[CONFETTI_N];
static uint8_t s_cfcol[CONFETTI_N];

// Where he last actually drew himself — updated at the end of every
// tick()/drawWaving() call, read by hitTest() so a tap only counts if
// it lands where he's currently standing (he moves/scales with the
// screen, this isn't a fixed region).
static int   s_lastCx = -10000, s_lastHeadTopY = 0;
static float s_lastScale = 1.0f;

// After this long with no interaction at all (not even idle quips
// count — this tracks real engagement), he dozes off instead of
// standing around wide awake forever.
static const uint32_t SLEEPY_AFTER_MS = 180000;

static const char* pick(const char* const* arr, int n) {
    return arr[random(0, n)];
}

static void say(const char* line, uint32_t ms) {
    bubbleText  = line;
    bubbleUntil = millis() + ms;
}

// Every bubble stays up at least this long, no matter which line fires.
static const uint32_t MIN_BUBBLE_MS = 4000;

void trigger(Event evt, DetectionType dt, uint32_t lifetimeTotal) {
    uint32_t now = millis();
    lastInteraction = now;
    switch (evt) {
        case Event::DETECTION: {
            mood = Mood::SHOCKED;
            moodUntil = now + 1400;
            s_reactType = dt;

            // First call this boot: don't re-announce milestones the
            // lifetime counter already passed in a previous session.
            if (!s_milestoneInit) {
                s_milestoneInit = true;
                for (uint8_t i = 0; i < MILESTONES_N; i++) {
                    if (lifetimeTotal >= MILESTONES[i]) s_lastMilestone = MILESTONES[i];
                }
            }
            uint32_t hit = 0;
            for (uint8_t i = 0; i < MILESTONES_N; i++) {
                if (lifetimeTotal >= MILESTONES[i] && MILESTONES[i] > s_lastMilestone) {
                    hit = MILESTONES[i];
                }
            }

            if (hit > 0) {
                s_lastMilestone = hit;
                snprintf(s_milestoneBuf, sizeof(s_milestoneBuf),
                         "Detection #%lu! Milestone.", (unsigned long)hit);
                say(s_milestoneBuf, 5500);
            } else {
                uint8_t idx = (uint8_t)dt;
                if (idx >= DET_LINES_N) idx = 0;
                const char* base = random(0, 2) ? DET_LINES[idx].a : DET_LINES[idx].b;
                // High confidence hits get the line straight — no need
                // to hedge on something we're actually sure about. Med
                // /Low get an honest number tacked on so a shakier
                // match doesn't read as equally certain.
                Confidence conf = confidenceFor(dt);
                if (conf == Confidence::HIGH_CONF) {
                    say(base, 4500);
                } else {
                    snprintf(s_detBuf, sizeof(s_detBuf), "%s (~%u%%)",
                             base, confidencePercent(conf));
                    say(s_detBuf, 5500);
                }
            }
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
            if (!s_petPrefsLoaded) {
                s_petPrefs.begin("squachy", false);
                s_petCount = s_petPrefs.getUInt("pets", 0);
                s_petPrefsLoaded = true;
            }
            say(pick(BOOT_LINES, 2), MIN_BUBBLE_MS);
            break;
        case Event::PETTED: {
            mood = Mood::BOUNCE;
            moodUntil = now + 1200;
            s_petFxStart = now;
            s_petFxUntil = now + 1900;
            s_petCount++;
            s_petPrefs.putUInt("pets", s_petCount);

            uint32_t hit = 0;
            for (uint8_t i = 0; i < PET_MILESTONES_N; i++) {
                if (s_petCount == PET_MILESTONES[i]) hit = PET_MILESTONES[i];
            }
            if (hit > 0) {
                snprintf(s_milestoneBuf, sizeof(s_milestoneBuf),
                         "Pet #%lu! We're basically friends now.", (unsigned long)hit);
                say(s_milestoneBuf, 5500);
            } else {
                say(pick(PET_LINES, 8), MIN_BUBBLE_MS);
            }
            break;
        }
    }
    nextIdleAt = now + 15000 + random(0, 15000);
}

bool hitTest(int x, int y) {
    // Generous fixed bounding box (not a pixel-perfect silhouette
    // test) sized off his last known position/scale — good enough for
    // a fingertip, and matches how forgiving every other tap target in
    // this UI already is.
    if (s_lastCx < -5000) return false;
    int halfW = (int)(24 * s_lastScale);
    int top   = s_lastHeadTopY - (int)(20 * s_lastScale);
    int bot   = s_lastHeadTopY + (int)(62 * s_lastScale);
    return x >= s_lastCx - halfW && x <= s_lastCx + halfW && y >= top && y <= bot;
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
    // Keep the whole bubble on-screen even for an unusually long line
    // (e.g. a detection quip with a confidence suffix on a narrow
    // portrait screen) instead of letting it run off either edge.
    int screenW = t.width();
    if (bx + bw > screenW - 2) bx = screenW - 2 - bw;
    if (bx < 2) bx = 2;
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

    // Once every so often (see tick()'s idle branch), his fur shimmers
    // through the vaporwave palette for a few seconds instead of the
    // usual brown — a rare, purely-cosmetic flourish.
    uint16_t furMain = FUR_MAIN, furLight = FUR_LIGHT;
    if (s_legendary && now < s_legendaryUntil) {
        float ph = (float)(now % 900) / 900.0f;
        furMain  = blend(CYAN, VAPOR_PINK, (uint16_t)(ph * 256.0f));
        furLight = blend(VAPOR_PINK, VAPOR_PURPLE, (uint16_t)(ph * 256.0f));
    }

    // Shadow (fixed, doesn't bob)
    t.fillEllipse(cx2, headTopY + S(62), S(18), S(4), blend(BG, FUR_DARK, 70));

    // Legs + big bigfoot feet
    t.fillRect(cx2 - S(10), hy + S(40), S(8), S(10), furMain);
    t.fillRect(cx2 + S(2),  hy + S(40), S(8), S(10), furMain);
    t.fillRoundRect(cx2 - S(13), hy + S(49), S(12), S(6), 2, furLight);
    t.fillRoundRect(cx2 + S(1),  hy + S(49), S(12), S(6), 2, furLight);

    // Body — broad, stocky torso instead of a slim rounded rect.
    t.fillRoundRect(cx2 - S(15), hy + S(23), S(30), S(18), S(5), furMain);
    t.fillRect(cx2 - S(15), hy + S(23), S(5), S(18), furLight);
    t.fillRect(cx2 + S(10), hy + S(23), S(5), S(18), furLight);

    // Arms — long, ape-like, hanging past the waist. Depend on mood; a
    // SHOCKED reaction further varies pose by what triggered it.
    if (m == Mood::WAVE) {
        float wa = -1.0f + sinf((float)(now % 400) / 400.0f * 6.2831853f) * 0.5f;
        float ex = cx2 + S(13) + cosf(wa) * (18.0f * scale);
        float ey = hy + S(28) + sinf(wa) * (18.0f * scale);
        t.drawWideLine(cx2 + S(11), hy + S(28), ex, ey, S(7), furLight);
        t.fillRoundRect(cx2 - S(18), hy + S(22), S(8), S(22), S(3), furLight);
    } else if (m == Mood::SHOCKED) {
        switch (reactPoseFor(s_reactType)) {
            case ReactPose::HANDS_UP:
                t.drawWideLine(cx2 - S(11), hy + S(26), cx2 - S(14), hy - S(10), S(7), furLight);
                t.drawWideLine(cx2 + S(11), hy + S(26), cx2 + S(14), hy - S(10), S(7), furLight);
                break;
            case ReactPose::COVER_FACE:
                // The crossing lines land on the face — drawn later,
                // after the head/eyes, so they show up in front of it.
                break;
            case ReactPose::POINT_SHADES:
            case ReactPose::DISGUST:
                // Resting arm now; the pointing/covering arm is drawn
                // after the head for the same in-front-of-face reason.
                t.fillRoundRect(cx2 - S(18), hy + S(22), S(8), S(22), S(3), furLight);
                break;
            case ReactPose::LOOK_UP:
            case ReactPose::LOOK_AROUND:
            case ReactPose::STARTLED:
            default:
                t.drawWideLine(cx2 - S(11), hy + S(26), cx2 - S(23), hy + S(10), S(7), furLight);
                t.drawWideLine(cx2 + S(11), hy + S(26), cx2 + S(23), hy + S(10), S(7), furLight);
                break;
        }
    } else {
        t.fillRoundRect(cx2 - S(18), hy + S(22), S(8), S(22), S(3), furLight);
        t.fillRoundRect(cx2 + S(10), hy + S(22), S(8), S(22), S(3), furLight);
    }

    // Head — broader jaw than before, brow ridge over the eyes.
    t.fillRoundRect(cx2 - S(15), hy, S(30), S(24), S(7), furLight);
    t.fillRoundRect(cx2 - S(12), hy + S(2), S(24), S(19), S(5), furMain);
    t.fillRoundRect(cx2 - S(9),  hy + S(7), S(18), S(11), S(4), SKIN_TAN);

    // Sagittal crest (the pronounced skull peak real bigfoot sightings
    // always mention) plus a couple of smaller shaggy fringe tufts.
    t.fillTriangle(cx2 - S(6), hy + S(2), cx2, hy - S(14), cx2 + S(6), hy + S(2), furLight);
    t.fillTriangle(cx2 - S(13), hy + S(3), cx2 - S(9), hy - S(4), cx2 - S(5), hy + S(3), furLight);
    t.fillTriangle(cx2 + S(5),  hy + S(3), cx2 + S(9), hy - S(4), cx2 + S(13),hy + S(3), furLight);

    // Ears — small and tucked close, like a real Sasquach rather than
    // a cartoon animal's.
    t.fillCircle(cx2 - S(15), hy + S(13), S(3), furMain);
    t.fillCircle(cx2 + S(15), hy + S(13), S(3), furMain);
    t.fillCircle(cx2 - S(15), hy + S(13), S(1), SKIN_DARK);
    t.fillCircle(cx2 + S(15), hy + S(13), S(1), SKIN_DARK);

    // Blush
    t.fillCircle(cx2 - S(8), hy + S(15), S(2), VAPOR_PINK);
    t.fillCircle(cx2 + S(8), hy + S(15), S(2), VAPOR_PINK);

    // Eyes / sunglasses + mouth
    if (m == Mood::SHOCKED) {
        ReactPose pose = reactPoseFor(s_reactType);
        int pdx = 0, pdy = 0;
        if (pose == ReactPose::LOOK_UP) {
            pdy = -S(2);
        } else if (pose == ReactPose::LOOK_AROUND) {
            pdx = (int)(sinf((float)(now % 600) / 600.0f * 6.2831853f) * S(2));
        }
        t.fillEllipse(cx2 - S(5), hy + S(9), S(3), S(4), WHITE);
        t.fillEllipse(cx2 + S(5), hy + S(9), S(3), S(4), WHITE);
        t.fillCircle(cx2 - S(5) + pdx, hy + S(9) + pdy, S(1), BLACK);
        t.fillCircle(cx2 + S(5) + pdx, hy + S(9) + pdy, S(1), BLACK);
        t.fillEllipse(cx2, hy + S(18), S(4), S(5), BLACK);

        // The pointing/covering gesture for these reactions lands on
        // the face, so it's drawn last, in front of the head just
        // painted above, instead of underneath it with the other arm.
        if (pose == ReactPose::COVER_FACE) {
            t.drawWideLine(cx2 - S(11), hy + S(26), cx2 + S(7), hy + S(6), S(7), furLight);
            t.drawWideLine(cx2 + S(11), hy + S(26), cx2 - S(7), hy + S(6), S(7), furLight);
        } else if (pose == ReactPose::POINT_SHADES) {
            t.drawWideLine(cx2 + S(11), hy + S(26), cx2 + S(4), hy + S(8), S(7), furLight);
        } else if (pose == ReactPose::DISGUST) {
            t.drawWideLine(cx2 + S(11), hy + S(26), cx2, hy + S(17), S(7), furLight);
        }
    } else if (m == Mood::SLEEPY) {
        // Closed, content eyes — soft downward arcs instead of shades —
        // plus a little "o" mouth and a drifting Z to sell the nap.
        t.drawLine(cx2 - S(9), hy + S(9), cx2 - S(2), hy + S(11), furLight);
        t.drawLine(cx2 + S(2), hy + S(11), cx2 + S(9), hy + S(9), furLight);
        t.fillCircle(cx2, hy + S(18), S(2), BLACK);

        float zPhase = (float)(now % 1600) / 1600.0f;
        int zx = cx2 + S(15) + (int)(zPhase * S(6));
        int zy = hy + S(1) - (int)(zPhase * S(14));
        uint16_t zCol = blend(BG, CYAN, (uint16_t)(220 * (1.0f - zPhase)));
        t.setTextSize(scale > 1.4f ? 2 : 1);
        t.setTextColor(zCol, BG);
        t.setCursor(zx, zy);
        t.print("Z");
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

void drawWaving(TFT_eSPI& t, int cx, int baseY, uint32_t now, float scale, const char* line) {
    // Same idle bob as tick()'s WAVE mood, just without the quip/mood
    // state machine — a self-contained cameo for the boot splash.
    float bobAmt = 6.0f * scale;
    float bob = sinf((float)(now % 900) / 900.0f * 6.2831853f) * bobAmt;
    int headTopY = baseY - (int)(58.0f * scale);
    int hy = headTopY + (int)bob;
    drawBody(t, cx, hy, headTopY, now, Mood::WAVE, scale);
    // Fixed above his (pre-bob) head, same as tick()'s bubble row — it
    // shouldn't bounce along with him. Pulled up further than tick()'s
    // gap (18px) specifically so it sits right under the "TALKING
    // SASQUACH" subtitle above, in the extra room this bigger boot-
    // splash scale leaves between his head and the subtitle.
    if (line) drawBubble(t, cx, headTopY - 34, line);
}

// Small filled heart, used by the tap-to-pet flourish.
static void drawHeart(TFT_eSPI& t, int x, int y, int r, uint16_t col) {
    t.fillCircle(x - r / 2, y, r / 2, col);
    t.fillCircle(x + r / 2, y, r / 2, col);
    t.fillTriangle(x - r, y, x + r, y, x, y + r, col);
}

// A few hearts drift up from his head and fade, staggered so they
// don't all rise in lockstep.
static void drawHeartFx(TFT_eSPI& t, int cx, int headTopY, uint32_t now) {
    static const uint8_t  NH = 5;
    static const int8_t   offsets[NH] = { -14, -7, 0, 7, 14 };
    static const uint16_t phaseMs[NH] = { 0, 150, 300, 450, 600 };
    for (uint8_t i = 0; i < NH; i++) {
        if (now < s_petFxStart + phaseMs[i]) continue;
        uint32_t elapsed = now - s_petFxStart - phaseMs[i];
        if (elapsed > 1200) continue;
        float p = (float)elapsed / 1200.0f;
        int hx = cx + offsets[i];
        int hy2 = headTopY - (int)(p * 34.0f) - 4;
        uint16_t col = Theme::blend(Theme::BG, Theme::PINK, (uint16_t)(255 * (1.0f - p)));
        drawHeart(t, hx, hy2, 3, col);
    }
}

// The rare "party mode" flourish: a rainbow strobe wash across his
// whole allotted region plus falling confetti, drawn before he is so
// he stands out in front of it. Whichever theme is active supplies the
// colors (same runtime Theme:: variables everything else reads), so
// the light show re-tints with the palette instead of a fixed rainbow.
static void drawPartyFx(TFT_eSPI& t, uint32_t now, int topY, int availHeight) {
    using namespace Theme;
    int w = t.width();
    static const uint16_t stops[6] = { RED, AMBER, GREEN, CYAN, VAPOR_PURPLE, PINK };
    for (int yy = topY; yy < topY + availHeight; yy += 4) {
        float huePos = fmodf((float)now / 260.0f + (float)(yy - topY) * 0.05f, 6.0f);
        int i0 = (int)huePos % 6, i1 = (i0 + 1) % 6;
        uint16_t col = blend(stops[i0], stops[i1], (uint16_t)((huePos - (int)huePos) * 255));
        t.drawFastHLine(0, yy, w, blend(BG, col, 110));
    }
    for (uint8_t i = 0; i < CONFETTI_N; i++) {
        s_cfy[i] += s_cfvy[i];
        if (s_cfy[i] > topY + availHeight) s_cfy[i] = (float)topY;
        t.fillRect((int)s_cfx[i], (int)s_cfy[i], 3, 3, stops[s_cfcol[i]]);
    }
}

void tick(TFT_eSPI& t, int cx, int topY, int availHeight, uint32_t now) {
    // Expire a triggered mood back to idle
    if (mood != Mood::IDLE && now > moodUntil) mood = Mood::IDLE;
    if (s_legendary && now >= s_legendaryUntil) s_legendary = false;

    // Random idle fun: bounce/wave + a quip, only when nothing else
    // triggered a reaction recently.
    if (mood == Mood::IDLE && now >= nextIdleAt) {
        uint32_t idleFor = now - lastInteraction;
        bool longIdle  = idleFor > 90000;
        bool verySleepy = idleFor > SLEEPY_AFTER_MS;
        if (verySleepy) {
            // A nap, not just another quip — persists for a while and
            // re-enters itself below rather than popping in and out
            // every idle cycle.
            say(pick(SLEEPY_LINES, 4), 6000);
            mood = Mood::SLEEPY;
            moodUntil = now + 8000;
            nextIdleAt = now + 8000;
        } else if (random(0, 250) == 0) {
            // Rare shimmering flourish — see drawBody's fur-color swap
            // — escalated into a full rainbow-wash-and-confetti moment.
            say(pick(PARTY_LINES, 4), 5000);
            mood = Mood::BOUNCE;
            moodUntil = now + 2000;
            s_legendary = true;
            s_legendaryUntil = now + 5000;
            int w = t.width();
            for (uint8_t i = 0; i < CONFETTI_N; i++) {
                s_cfx[i]   = (float)random(0, w);
                s_cfy[i]   = (float)(topY + random(0, availHeight));
                s_cfvy[i]  = 0.8f + (float)random(0, 100) / 100.0f * 1.4f;
                s_cfcol[i] = (uint8_t)random(0, 6);
            }
            nextIdleAt = now + 12000 + random(0, 18000);
        } else {
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
    float bobAmt   = (mood == Mood::BOUNCE) ? 9.0f : (mood == Mood::SLEEPY ? 1.5f : 3.0f);
    bobAmt *= scale;
    float bobSpeed = (mood == Mood::BOUNCE) ? 220.0f : (mood == Mood::SLEEPY ? 2200.0f : 1100.0f);
    float bob = sinf((float)(now % (uint32_t)bobSpeed) / bobSpeed * 6.2831853f) * bobAmt;
    if (mood == Mood::BOUNCE) bob = -fabsf(bob); // hop upward only
    int hy = headTopY + (int)bob;

    // Party mode draws first — a full wash across his region — so his
    // body and the hearts below land on top of it, not under it.
    if (s_legendary) drawPartyFx(t, now, topY, availHeight);

    // No erase-then-redraw here: ui_clear.cpp's background draw call
    // (matrix rain / starfield / toasters / lava lamp) runs immediately
    // before this every frame and already fully repaints this entire
    // region, including wherever he stood last frame. Erasing his
    // footprint to flat BG on top of that would just punch a static
    // black hole in the animation right behind him — drawing his
    // opaque shapes straight onto the fresh background is enough, and
    // the negative space around his silhouette shows the animation
    // through instead of a box. (Party mode is the one exception —
    // when it's active the wash above already repaints this whole
    // region every frame, same guarantee, just with extra flair.)
    drawBody(t, cx, hy, headTopY, now, mood, scale);
    if (now < s_petFxUntil) drawHeartFx(t, cx, headTopY, now);

    // Remember where/how big he actually was this frame — hitTest()
    // (tap-to-pet) checks against this, not a fixed region, since he
    // moves and rescales with the screen.
    s_lastCx = cx;
    s_lastHeadTopY = headTopY;
    s_lastScale = scale;

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
