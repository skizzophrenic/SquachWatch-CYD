// SquachWatch-CYD — Squachy implementation
#include "squachy.h"
#include "theme.h"
#include "signatures.h"
#include "settings.h"

// ---- tempo ----
// Squachy's behaviour is scheduled in milliseconds: how long a mood lasts,
// how long a bubble stays, how often an idle gag fires, how long a drop or a
// stretch takes. None of that changed when the frame rate doubled -- the
// same 260 ms drop just got twice the frames -- and it read as brisker for
// it. This is one percentage on every one of those durations: 100 as the
// numbers were written, 70 a good deal slower. Chosen by eye on a real board
// (TEMPO P on the console) and kept in settings. The clock itself is never
// scaled: a mascot whose time drifts against the board's would break every
// hold-and-tap gesture within the hour.
static uint8_t s_tempoPct = 70;   // until settings are read; the same default
static inline uint32_t tempo(uint32_t ms) { return ms * s_tempoPct / 100; }
#include "clock.h"
#include "dex.h"
#include "void_eye.h"
#if SQUACH_MESH
#include "emote_script.h"   // spokenName, for the banter
#include "squachmesh.h"     // OUTFIT_N, checked against OutfitId::COUNT below
#endif
#include <Arduino.h>
#include <Preferences.h>

namespace Squachy {

// Single switch for the silhouette keyline added to drawBody(). Left as a
// named constant rather than inlined so taking it back out is one edit,
// not an archaeology exercise across the draw order.
static const bool SQUACHY_KEYLINE = true;

// Squachy's ground shadow. See the block in drawBody for why it is off, and
// for what turning it back on costs -- it is not a free toggle, because his
// scale is derived from whether it is there.
static const bool SQUACHY_SHADOW = false;

// Every thick line he is drawn with -- arms, legs, brows, the keyline under
// each limb -- goes through here rather than TFT_eSPI::drawWideLine().
//
// That call is anti-aliased: drawWedgeLine() walks the line's whole bounding
// box computing a float distance-to-line per pixel, and alpha-blends the
// edge pixels against what is already in the frame, which on a sprite means
// reading each one back first. With a dozen or more of them a frame, at limb
// widths, that was the single largest cost on the board -- ONE Squachy at
// x1.88 drew in 28 ms, more than the 23 ms it takes to push the whole frame
// over SPI. Measured on 2026-09-11 (crowd_bench "row 1").
//
// This is the emulator's version, and has been all along: two triangles and
// two end caps, integer fills, no blending. Every render judged in the
// emulator was drawn like this, so it changes the board to match what was
// approved rather than the other way round. At RGB332 the missing
// anti-aliasing is a colour or two of edge softness.
//
// -DSQUACHY_AA_LINES=1 puts the library call back, for measuring against.
#ifndef SQUACHY_AA_LINES
#define SQUACHY_AA_LINES 0
#endif
// Torso half-width in Squachy units. He shipped at 15, the torso as wide as
// his head; 11 is as slim as it goes before the legs show outside it and the
// hanging arms lose contact with it. Everything sized to the torso (keyline,
// the suit and coat, the belt) is expressed in this.
static int torsoHalf() { return 11; }

static void wideLine(TFT_eSPI& t, float ax, float ay, float bx, float by, float wd, uint32_t color) {
#if SQUACHY_AA_LINES
    t.drawWideLine(ax, ay, bx, by, wd, color);
#else
    const float dx = bx - ax, dy = by - ay;
    const float len = sqrtf(dx * dx + dy * dy);
    const int   r   = (int)(wd / 2);
    if (len < 0.01f) { t.fillCircle((int)ax, (int)ay, r, color); return; }
    const float nx = -dy / len * (wd / 2), ny = dx / len * (wd / 2);
    t.fillTriangle((int)(ax + nx), (int)(ay + ny), (int)(ax - nx), (int)(ay - ny),
                   (int)(bx + nx), (int)(by + ny), color);
    t.fillTriangle((int)(bx + nx), (int)(by + ny), (int)(bx - nx), (int)(by - ny),
                   (int)(ax - nx), (int)(ay - ny), color);
    t.fillCircle((int)ax, (int)ay, r, color);
    t.fillCircle((int)bx, (int)by, r, color);
#endif
}

// A costume piece with a one-pixel ink keyline, the way his own head and torso
// have one. `shape(ox, oy, colour)` draws every part of the piece nudged by
// (ox, oy); it runs four times in ink, a pixel out each way, then once in its
// own colour. Drawing ALL the parts in ink before any of the fill is what gives
// a piece made of several shapes one outline round the outside and none across
// its own seams.
template <typename F> static void inked(uint16_t ink, uint16_t col, F shape) {
    static const int8_t O[4][2] = { {-1, 0}, {1, 0}, {0, -1}, {0, 1} };
    for (uint8_t i = 0; i < 4; i++) shape(O[i][0], O[i][1], ink);
    shape(0, 0, col);
}

// The cap letter, 3x5, in pixels of p. Too small to read below p = 1, so the
// caller skips it there and the emblem is a plain disc.
static void capGlyph(TFT_eSPI& t, int x, int y, int p, uint16_t col) {
    static const uint8_t S_[5] = { 0b111, 0b100, 0b111, 0b001, 0b111 };
    for (int r = 0; r < 5; r++)
        for (int c = 0; c < 3; c++)
            if (S_[r] & (4 >> c)) t.fillRect(x + c * p, y + r * p, p, p, col);
}

enum class Mood : uint8_t { IDLE, WAVE, SHOCKED, BOUNCE, SLEEPY, WALK, DANCE, WINK,
                            STRETCH,   // waking out of a nap -- see the nap-exit branch
                            GUM,       // blowing a bubble, rare idle flourish
                            JUGGLE,    // showing off recent catches, needs activity heat
                            HIGHFIVE,  // one arm across -- see s_reachDir and s_reachLevel
                            PUMP,      // a fist pumping, for rock-paper-scissors
                            ACT,       // an emote's pose -- see s_actPose
                            LEAN,      // leaning toward an edge, listening -- see s_leanStart
                            TRIP };    // a stumble over nothing, then a look around

// Which way a HIGHFIVE reaches: +1 right, -1 left. Set by whoever draws the
// body just before it does -- the host always reaches right, the guest always
// left -- because the two share drawBody() and this is the one thing about
// the pose that differs between them.
static int8_t s_reachDir = 1;
// ...and at what height: 0 up (a high five), 1 down (a low five), 2 level (a
// fist bump, or a hand held out with a rock, paper or scissors over it). The
// host's comes from visitReach(), the guest's from his VisitPose.
static uint8_t s_reachLevel = 0;
static uint8_t s_hostReachLevel = 0;

// Which emote pose Mood::ACT strikes, as a VisitPose, set by whoever draws the
// body just before it does -- the host's from visitPose(), the guest's from
// his own VisitPose -- exactly as s_reachDir is. And the detection reaction an
// emote borrows: COVER, LOOK_AROUND and HANDS_UP are SHOCKED with a pose of
// the emote's choosing rather than the one his last detection picks. -1 is
// "his own", which is every SHOCKED that is not an emote's.
static uint8_t  s_actPose  = 0;
static int8_t   s_actReact = -1;
static uint8_t  s_hostAct  = 0;
static int8_t   s_hostReact = -1;
static uint32_t s_hostActUntil = 0;

// Which reaction pose a SHOCKED mood strikes — varies by what triggered
// it so a detection actually reads differently depending on the type,
// instead of every alert getting the same generic startle.
enum class ReactPose : uint8_t { STARTLED, HANDS_UP, COVER_FACE, POINT_SHADES, DISGUST, LOOK_UP, LOOK_AROUND };

static ReactPose reactPoseFor(DetectionType t) {
    switch (t) {
        case DetectionType::AXON:    return ReactPose::HANDS_UP;    // "don't shoot" — it's law enforcement gear
        case DetectionType::FLOCK:
        case DetectionType::ALPR:
        case DetectionType::CAMERA:
        case DetectionType::RING:    return ReactPose::COVER_FACE;  // something's taking his picture
        case DetectionType::META:    return ReactPose::POINT_SHADES;// smart glasses — he points at his own shades
        case DetectionType::SKIMMER: return ReactPose::DISGUST;     // a skimmer is just gross
        case DetectionType::DRONE:   return ReactPose::LOOK_UP;     // eyes in the sky
        case DetectionType::AIRTAG:
        case DetectionType::SAMSUNG_TAG:
        case DetectionType::GOOGLE_TAG:
        case DetectionType::TILE:    return ReactPose::LOOK_AROUND; // something's tracking him
        default:                     return ReactPose::STARTLED;   // UNKNOWN, RAVEN
    }
}
static ReactPose curReactPose();       // below, once s_reactType is declared

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

// Biased in for a while after real detection activity, instead of the
// usual idle pool — see s_activityHeat. Makes idle chatter read as
// connected to what the device is actually doing.
static const char* ALERT_MOOD_LINES[] = {
    "Staying sharp. Lot going on today.",
    "Busy shift. Not complaining though.",
    "Eyes open. Things keep showing up.",
    "Feels like a lot of company lately.",
};

// The opposite bias — a long stretch of nothing at all.
static const char* RELAXED_MOOD_LINES[] = {
    "Quiet enough to nap standing up.",
    "Nothing but vibes today.",
    "Slow day. I'll take it.",
    "Peaceful out here. Suspiciously peaceful.",
};

// A little wander — see the Mood::WALK handling in tick()/drawBody().
static const char* WALK_LINES[] = {
    "Just stretching my legs.",
    "Patrol time.",
    "Gotta walk the perimeter.",
    "Somebody's gotta pace around here.",
};

static const char* const DANCE_LINES[] = {
    "Nobody's watching. Well, you are.",
    "This is my best move.",
    "Got moves. Don't judge.",
    "Dance break. You're welcome.",
};

// "Seen you before" reactions — see trigger()'s DETECTION case. Keyed
// off a log entry's own hit count, not the lifetime total: a MAC
// that's matched a handful of times is a real pattern, not a
// coincidence, so it gets called out distinctly from a fresh sighting.
static const char* SEEN_BEFORE_LINES[] = {
    "Seen this one before.",
    "We meet again.",
    "This one's a regular.",
    "Recognize this one.",
};

// The regulars: a device seen on three different days has a name, and he
// greets it like a neighbour. %s is the name.
static const char* const REGULAR_LINES[] = {
    "%s again. Right on time.",
    "Oh, it's just %s.",
    "Morning, %s. Or whatever it is.",
    "%s. Every day. Like clockwork.",
    "There goes %s. Never says hi.",
    "%s is up. %s is always up.",
};
static const char* const NEW_REGULAR_LINES[] = {
    "Three days running. I'm calling this one %s.",
    "Okay, you're a regular now. Hi, %s.",
    "Seen you enough. You're %s from here on.",
};
// The nemesis: the type he has caught most. %s is the type's name.
static const char* const NEMESIS_LINES[] = {
    "%s. My old enemy.",
    "Not you again, %s.",
    "We meet again, %s. As always.",
    "One day, %s. One day.",
    "%s. Of course it's %s.",
};
// Streaks: the same type again and again in a short while, and a new
// closest for its kind.
static const char* const RUN3_LINES[] = {
    "Third %s in ten minutes. They travel in packs.",
    "That's three %s. Somebody's collecting them.",
};
static const char* const RUN5_LINES[] = {
    "Five %s. This is a %s convention.",
    "Five. Five %s. I'm going to need a bigger log.",
};
static const char* const RUN10_LINES[] = {
    "Ten %s. I've stopped counting. I haven't.",
};
static const char* const CLOSEST_LINES[] = {
    "Closest %s ever. It could hear me breathing.",
    "New record. That %s was practically in my fur.",
    "A %s, closer than any before. Personal space, please.",
};

// You keep opening the LOG. He would say if there were anything.
static const char* const CHECKING_LINES[] = {
    "You keep checking. I'd tell you.",
    "Fifth time in a minute. It hasn't changed. I'd know.",
    "The log is fine. Are YOU fine?",
};
// A whole day without a poke.
static const char* const IGNORED_LINES[] = {
    "You haven't poked me in a day. I'm fine. Totally fine.",
    "Twenty-four hours. No pats. I've started counting.",
    "Still here. Still unpoked. Just saying.",
};

static const char* PERSISTENT_LINES[] = {
    "This one keeps coming back. Worth noting.",
    "Not a one-time thing anymore. Keep an eye on it.",
    "Same one, again. That's a pattern, not a coincidence.",
    "This one's really sticking around.",
};

static const char* BOOT_LINES[] = {
    "SquachWatch online. Let's find something.",
    "Booted. Don't just stare at your phone.",
};

// First-boot walkthrough — see startOnboardingInternal(). Kept to
// short, complete sentences (each wraps to at most ONBOARD_MAX_LINES
// lines in drawOnboardBubble) rather than the terse one-liners the
// rest of these banks use, since this is the one place Squachy needs
// to actually explain something instead of just cracking a joke.
static const char* const ONBOARD_LINES[] = {
    "Hey! First boot -- I'm Squachy. Two minutes, then I'll let you go.",
    "SquachWatch listens for surveillance nearby -- cameras, plate readers, trackers like AirTags.",
    "No magic. Just WiFi and Bluetooth, matching known hardware as it passes by.",
    "All zeroes down there means nothing's around. It flips to a big flashing ALERT the second something matches.",
    "Down there: SCAN rescans, LOG shows history, DESK is a big clock.",
#if defined(AWOK)
    "Up top left: Settings. The far left/right edges of the screen swap backgrounds, one swap per tap.",
#else
    "Up top: left icon is Settings, right one rotates. Far left/right edges of the screen swap backgrounds.",
#endif
    "Tap me for a pet, hold me for a beat longer, or stroke me. Hold then drag to carry me.",
    "That's everything. Stay squachy.",
};
static const uint8_t  ONBOARD_N        = sizeof(ONBOARD_LINES) / sizeof(ONBOARD_LINES[0]);
static const uint32_t ONBOARD_STEP_MS  = 11000; // auto-advances if nobody taps

static const char* LOG_OPEN_LINES[] = {
    "Snooping the log? Bold. Respect.",
    "This is where the receipts live.",
};

static const char* LOG_CLEAR_LINES[] = {
    "Log wiped. Fresh start, cryptid style.",
};
// Counted rather than hard-coded at the call site. pick() indexes with
// random(0, n), so a literal that outlives an edit to the list above walks
// off the end -- which removing a line from this pool would have done.
static const uint8_t LOG_CLEAR_N = sizeof(LOG_CLEAR_LINES) / sizeof(LOG_CLEAR_LINES[0]);

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
    "Careful, that's how legends get spoiled.",
    "This never happens at the cabin. Never.",
    "Better resolution than any trail cam gets.",
    "Feed me enough pets and I unionize.",
    "That's going straight in my memoir.",
    "Rarer than an actual sighting, honestly.",
    "I don't do this for everyone. Okay, maybe.",
    "I bruise like a legend, not a mascot.",
    "This is the part they cut from the footage.",
    "You'll tell people. Nobody will believe you.",
    "Petting confirmed. No takebacks.",
    "Witnesses say less than you're about to.",
};

// A stationary press-and-hold reads as more deliberate than a quick
// tap -- a beat longer, a bit more sincere, still self-aware about it.
static const char* HELD_LINES[] = {
    "Okay, that's actually nice.",
    "Don't stop. I mean it.",
    "This is a whole moment right now.",
    "Five more seconds. I'm counting.",
    "You found my good side.",
    "I could stay like this.",
};

// An actual dragging/stroking touch is the most affectionate of the
// three gestures -- leans further into "genuinely enjoying this" than
// PET_LINES or HELD_LINES do.
static const char* PETTING_LINES[] = {
    "Okay yeah. This is the good stuff.",
    "I'm not saying I purr. I'm not saying I don't.",
    "This is exactly what I needed today.",
    "Cryptid melting. Send help. Don't actually.",
    "You've unlocked my trust. Briefly.",
    "This is going in the highlight reel.",
};

// A yawn/nap moment for when nothing's happened in a long while — a
// visual state, not just another line bank (see the SLEEPY mood in
// drawBody).
// Said on a fixed thirty-second beat, not on the random idle roll. This is
// the job ALL CLEAR used to do: with that headline gone, something still has
// to tell you the thing is awake and looking, and a mascot saying so is
// worth more than a label that only appeared when nothing was happening.
//
// Deliberately more lines than the other pools. A reassurance you see twice
// a minute for hours has to not wear out, and four would.
static const char* WATCHING_LINES[] = {
    "Still watching. Nothing's snuck past.",
    "Eyes open. You're covered.",
    "Sweeping the airwaves. All quiet so far.",
    "I'm on it. Go about your business.",
    "Listening. Nothing worth telling you about.",
    "Two point four gigahertz of nothing. Good.",
    "Nobody's looking at you but me.",
    "Watching the watchers. Nothing yet.",
    "Radio's quiet. I'll shout if it isn't.",
    "Keeping an eye out. Same as always.",
};
static const uint8_t WATCHING_N = sizeof(WATCHING_LINES) / sizeof(WATCHING_LINES[0]);

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

// The freelance-police register: a calm, faintly formal voice that says
// alarming things in a level tone and ordinary things as if they were the
// end of the world, ornate mock-oaths, tangents that go nowhere on purpose,
// and a gleeful streak underneath it. Written new; the rhythm is the debt.
static const char* const NOIR_LINES[] = {
    "Great galloping router tables. Nothing happened again.",
    "I've seen things you wouldn't believe. Most of them were printers.",
    "Sweet sizzling substations, the air's quiet. I don't trust it.",
    "It was a quiet night. Too quiet. Then it stayed quiet. I made a sandwich.",
    "Somewhere out there a Flock cam is thinking about you. I'm thinking about lunch.",
    "Holy jumping junction boxes, that's a lot of Bluetooth for a Tuesday.",
    "I don't have a badge. I have feet. Big ones. It's basically the same.",
    "Note to self: the smart fridge is not a suspect. Yet.",
    "Crime never sleeps. Neither do I. Neither does the doorbell. We're all very tired.",
    "Suffering succotash of the airwaves, a printer just asked to be my friend.",
    "Nothing to report but my own magnificence. I've filed it under M.",
    "If I had a nickel for every tracker I've seen, I'd need somewhere to keep nickels.",
    "Great screaming skimmers of the seven-elevens. Still nothing. Carry on.",
    "This is the part of the case where I stare meaningfully at a router.",
    "I could go for some crime right now. Small crime. Jaywalking. I'd watch.",
    "By the sacred sideburns of the switchboard, I'm bored.",
};
static const uint8_t NOIR_LINES_N = sizeof(NOIR_LINES) / sizeof(NOIR_LINES[0]);

// The same register on a catch: the ornate oath first, the plain fact
// tacked on so the line still tells you what it was.
static const char* const OATH_LINES[] = {
    "Holy hopping hotspots. %s.",
    "Great galloping glass fibre! %s. Act natural.",
    "Sweet screaming sensor arrays. A %s. Of course.",
    "By the beard of the modem, a %s.",
    "Well slap me with a spectrum analyser. %s.",
    "Suffering skimmers, a %s. Nobody move. Or move. I'm not the boss of you.",
    "Jumping jitterbugs of the junction box, %s.",
    "Great heaving heatmaps. %s. I love this job.",
};
static const uint8_t OATH_LINES_N = sizeof(OATH_LINES) / sizeof(OATH_LINES[0]);

struct DetLines { const char* a; const char* b; };
// Indexed by DetectionType (UNKNOWN..RING), matches state.h ordering.
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
    { "Tile detected. Hope it's a friend.",  "Something tiny is tracking something." }, // TILE
    { "Ring cam spotted. Smile for Amazon.", "Someone's doorbell is judging you." },    // RING
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
// True while somebody is visiting, and true while somebody else is the one
// talking -- see setVisiting()/setListening(). Both live out here rather than
// with the visit code because tick() reads them and tick() is in every build,
// mesh or not: s_visiting decides whether his speech bubble grows a tail, and
// a static declared inside #if SQUACH_MESH does not exist to answer that in
// the four shipping builds.
static bool          s_visiting      = false;
static bool          s_listening     = false;
static uint32_t      moodUntil       = 0;

// A little wander away from center and back — see tick()'s idle-quip
// scheduler and the bodyCx computation further down. s_walkStart
// anchors a 0..1 progress ratio through WALK_DURATION_MS; the actual
// offset is WALK_CYCLES full sine cycles over that (0 at both ends,
// out to one full screen edge, back through center, out to the other
// edge, back to 0, repeated) so he always ends up back at center
// exactly as the mood naturally expires, with no separate "walk back"
// step needed. WALK_DURATION_MS is a single cycle's length times the
// repeat count, not an independently-chosen total, so the per-cycle
// pace stays the same regardless of how many times he crosses.
// How long a double-take runs before SHOCKED settles into its normal
// pose. See the offset curve in tick().
static const uint32_t DT_TOTAL_MS_BASE = 900;
static uint32_t       DT_TOTAL_MS = 900;

static const uint32_t WALK_CYCLE_MS_BASE = 9000;
static uint32_t       WALK_CYCLE_MS = 9000;
static const uint8_t  WALK_CYCLES     = 5;
static uint32_t       WALK_DURATION_MS = WALK_CYCLE_MS_BASE * WALK_CYCLES;
static uint32_t       s_walkStart = 0;
static int8_t         s_walkDir   = 1;
static const char*   bubbleText      = nullptr;
static uint32_t      bubbleUntil     = 0;
// When the current line started, for the bubble's pop-in (see
// bubblePop()). Separate from bubbleUntil because the grow is measured
// forward from the start, not backward from the expiry.
static uint32_t      bubbleStart     = 0;
static uint32_t      nextIdleAt      = 4000;
// The reassurance beat. First one lands a few seconds after boot rather
// than at t=30s, so a device that has just been switched on says something
// reassuring while somebody is still looking at it.
static const uint32_t WATCH_EVERY_MS_BASE = 30000;
static uint32_t       WATCH_EVERY_MS = 30000;
static uint32_t      s_nextWatchAt   = 6000;
static uint32_t      lastInteraction = 0;
// The last time a finger touched HIM: petted, held, stroked or flicked.
// lastInteraction counts detections too, which is not what "ignored" means.
static uint32_t      s_lastTouchAt   = 0;
static bool          s_ignoredSaid   = false;
static DetectionType s_reactType     = DetectionType::UNKNOWN;
// See catchContext() in squachy.h. Spent by the next DETECTION trigger.
static const char*   s_ctxRegular    = nullptr;
static bool          s_ctxNewRegular = false;
static bool          s_ctxNemesis    = false;
static bool          s_ctxNewClosest = false;
// The run: how many of the same type in a row, ten minutes apart at most.
static DetectionType s_runType = DetectionType::UNKNOWN;
static uint8_t       s_runN    = 0;
static uint32_t      s_runAt   = 0;
// The pose a SHOCKED body strikes: an emote's, when one has borrowed the
// reaction, else whatever his last detection calls for.
static ReactPose curReactPose() {
    return s_actReact >= 0 ? (ReactPose)s_actReact : reactPoseFor(s_reactType);
}
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

// First-boot walkthrough (see replayIntro()/onboardingActive() in the
// header). "onboarded" is persisted in the same NVS namespace as the
// pet count above, loaded the same lazy way.
static bool    s_onboardActive = false;
static uint8_t s_onboardStep   = 0;

// ---- Companion stats / cosmetics (see squachy.h) ----
// Persisted fields, all loaded together by ensurePrefsLoaded() below.
static uint32_t s_bootCount        = 0;
static uint32_t s_bestClearMs      = 0;  // longest-ever gap between detections
static uint32_t s_bestSessionCount = 0;  // most detections seen in one boot
static uint8_t  s_firstType        = (uint8_t)DetectionType::UNKNOWN;
static uint8_t  s_shadeIdx         = 0;
#if SQUACH_MESH
// Declared here with the other persisted prefs rather than beside its
// accessors: ensurePrefsLoaded() reads it far earlier in the file.
//
// Gated with the rest of the feature. The plan says a typed name stands on
// its own merits whether or not SquachMesh ever ships -- and it does -- but
// nothing in a non-mesh build can SET one, so shipping the storage there
// would be dead weight AND would break the guarantee that a shipping binary
// is untouched by this work. Promoting it out of the flag is one deliberate
// edit on the day that becomes true.
static char     s_customName[CUSTOM_NAME_MAX + 1] = {0};
#endif
static uint8_t  s_nickIdx          = 0;
static uint8_t  s_outfitIdx        = 0;
static bool     s_allOutfitsUnlocked = false;  // hidden button-sequence easter egg
static bool     s_wolfPeltUnlocked   = false;  // earned by summoning the werewolf
static bool     s_chromeWingUnlocked = false;  // earned by catching the gold toaster
static bool     s_voidEyeUnlocked    = false;  // earned by catching two Starfield eyes in a row
static bool     s_parkaUnlocked      = false;  // earned by knocking five times on the Snowfall lodge
static bool     s_sharkUnlocked      = false;  // earned by catching the Aquarium shark on his return pass
static bool     s_petUnlocked        = false;  // earned by tapping the lil guy on the toasters
// Whether the card has been shown for him. Kept separately from the unlock
// itself, and for the same reason the outfits keep an "announced" bitmask:
// the pet survives a reboot, so without this the celebration would play
// again on every boot and read as a bug.
static bool     s_petAnnounced       = false;
static bool     s_petCardPending     = false;
// Which companion is on: 0 off, 1 VAPOR SHAGGY, 2 the yeti. It was a bool
// while there was only one of them; a board that saved the bool keeps its
// answer (on -> SHAGGY) the first time this runs.
static uint8_t  s_petSel            = 1;
// Bitmask of outfits whose unlock popup has already been shown. Persisted,
// because "new" has to survive a reboot: without it every boot would
// re-announce everything already earned. Seeded on first run with whatever
// is unlocked at that moment (see refreshOutfitUnlocks()), so upgrading
// firmware onto a device that already has six outfits does not greet the
// owner with six popups.
static uint32_t s_outfitAnnounced    = 0;

// Runtime-only — reset every boot, not persisted.
static uint32_t s_cachedLifetimeTotal = 0;  // from the last DETECTION trigger (or BOOTED)
static uint32_t s_sessionDetections   = 0;  // this boot's count, vs. s_bestSessionCount
static uint32_t s_lastDetectionAt     = 0;  // millis() of the last catch, for the live streak
// Outfits unlocked but not yet shown to the player. A queue rather than a
// single slot because one detection can cross two thresholds at once, and
// because RESET STATS followed by a rebuild can re-earn several in a row;
// they are shown one popup at a time. Runtime-only -- s_outfitAnnounced is
// what actually persists, and it is written the moment a popup is consumed.
static uint8_t  s_outfitQueue[8];
static uint8_t  s_outfitQueueN = 0;
static bool     s_haveLastDetection   = false;

// A rolling "how much has been happening lately" signal — nudged up on
// every real detection, decayed back down over time — that biases
// which idle-line pool tick() picks from (see ALERT_MOOD_LINES /
// RELAXED_MOOD_LINES) so idle chatter reads as connected to what the
// device is actually doing instead of generic filler regardless of
// activity.
static float    s_activityHeat    = 0.0f;
static uint32_t s_lastHeatDecayAt = 0;

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
// ---- pose channels ------------------------------------------------
// Small per-frame offsets computed in tick() and read by drawBody().
// Kept as file statics rather than added to drawBody()'s already-long
// parameter list, matching how s_topLimit and s_hyCeiling already work.
// Everything here is derived from state tick() has anyway, so the
// banded renderers that call tick() several times per logical frame
// recompute identical values on every band.
//
// s_headDrop  squash and stretch: the head sinks into the shoulders as
//             he lands and extends at the apex. Head group only -- the
//             torso, arms and legs keep their own anchor, which is what
//             makes it read as a neck compressing rather than as the
//             whole character sliding.
// s_shadowAdj the shadow spreads as he gets closer to it. Same source
//             number as the bob, so the two can never disagree. Positive
//             means CONTACT (spread and flatten), negative means DISTANCE
//             (close in on both axes) -- the shadow reads those two apart,
//             because a landing squash and a hop are not the same shape.
// s_shadowCov how much of the shadow actually gets painted, 0-16, which is
//             the only opacity this panel has. See the dither in drawBody.
// s_shadeDrop double-take: the shades slip down his nose so he is
//             looking over the top of them.
static int     s_headDrop  = 0;
static int     s_shadowAdj = 0;
static uint8_t s_shadowCov = 16;
static uint8_t s_shadeDrop = 0;
// When the current double-take started, or 0. See the DT_ constants
// and the offset computed in tick().
static uint32_t s_dtStart = 0;
// Gum bubble: when the current one started inflating. GROW then POP;
// the mood outlasts both slightly so he gets a beat afterward.
static const uint32_t GUM_GROW_MS_BASE = 1200;
static uint32_t       GUM_GROW_MS = 1200;
static const uint32_t GUM_HOLD_MS_BASE = 600;
static uint32_t       GUM_HOLD_MS = 600;    // full size, wobbling, before it goes
static const uint32_t GUM_POP_MS_BASE = 400;
static uint32_t       GUM_POP_MS = 400;
static uint32_t s_gumStart = 0;
// Waking stretch: anchored to its own start rather than to now %
// STRETCH_MS, so the yawn peaks in the middle of the pose instead of
// wherever the clock happened to be when he woke up.
static const uint32_t STRETCH_MS_BASE = 1900;
static uint32_t       STRETCH_MS = 1900;
static uint32_t s_stretchStart = 0;
// The last three detection types, newest first -- what he juggles.
// UNKNOWN until something has actually been seen, which is also what
// keeps the juggle from firing on a fresh device.
static DetectionType s_recentTypes[3] = { DetectionType::UNKNOWN,
                                          DetectionType::UNKNOWN,
                                          DetectionType::UNKNOWN };
// True while the caller is drawing the scan effect (see tick()'s
// scanningFx). Not a mood: it is a property of which screen is open,
// so it outranks the mood machine in the arm chain.
static bool s_binoc = false;

// Which little beat he is striking mid-pace, or 0 for "just keep
// walking". A patrol runs WALK_CYCLE_MS * WALK_CYCLES = 45 seconds, and
// for all of it he used to do exactly one thing: swing his arms. The
// beat fires at the far end of a sweep, where the sine's slope is
// almost zero and he is momentarily stopped anyway, so stopping to do
// something there costs no extra motion to sell.
static uint8_t s_walkBeat = 0;

// ---- SHOW OFF -------------------------------------------------------
// A parade of every pose, one per SHOW_STEP_MS, each announcing itself
// through the ordinary speech bubble so it needs no UI of its own.
// s_showWB forces a walk beat, which is otherwise chosen by a hash of
// which sweep he is on and cannot be asked for directly.
static const uint32_t SHOW_STEP_MS_BASE = 2200;
static uint32_t       SHOW_STEP_MS = 2200;
static const uint8_t  SHOW_N       = 22;
static bool     s_showOff   = false;
static uint32_t s_showStart = 0;
static uint8_t  s_showIdx   = 0xFF;
static int8_t   s_showWB    = -1;

// ---- published arm positions ----------------------------------------
// Where each arm actually ended up this frame: shoulder (0) and hand
// (1), left and right. drawBody()'s arm chain writes these, and
// drawOutfit() reads them so a costume can hang something on an arm
// instead of guessing where the arm probably is. The wolf pelt used
// fixed coordinates and simply sat still while the arm slid out from
// under it, which is the bug these exist to fix.
static int s_armL0x = 0, s_armL0y = 0, s_armL1x = 0, s_armL1y = 0;
static int s_armR0x = 0, s_armR0y = 0, s_armR1x = 0, s_armR1y = 0;
// And where it put each foot. Every branch below draws the same S(12)xS(6)
// shape, so only the corner is worth recording.
static int s_footLx = 0, s_footLy = 0, s_footRx = 0, s_footRy = 0;

// ---- recoil ---------------------------------------------------------
// How hard the last detection hit, 0..1, derived from its RSSI. Scales
// the double-take amplitudes rather than adding a second animation --
// same pose, different size, which is the whole idea.
static float s_recoilK = 0.55f;

// ---- carry ----------------------------------------------------------
// s_grabbed while a finger is holding him; the drop that follows runs
// on s_dropStart from wherever he was let go. s_dangle is what the draw
// path reads -- true for both the carry and the fall, since he hangs
// the same way through either.
static const uint32_t DROP_MS_BASE = 260;
static uint32_t       DROP_MS = 260;
static const uint32_t LAND_MS_BASE = 220;
static uint32_t       LAND_MS = 220;
static bool     s_grabbed  = false;
static bool     s_dangle   = false;
static int      s_grabX = 0, s_grabY = 0;
static uint32_t s_dropStart = 0;
static int      s_dropX = 0, s_dropY = 0;

// ---- ducking --------------------------------------------------------
// Set by toasterNear() when something is genuinely on a collision
// course. The cooldown is his, not the caller's: backgrounds fire the
// hook for every object they draw, every frame.
static uint32_t s_duckUntil    = 0;
static uint32_t s_duckCooldown = 0;

// ---- the idle-life pack ---------------------------------------------
// Lean and peek: slides toward one edge to listen, holds, snaps back.
static uint32_t s_leanStart = 0;
static int8_t   s_leanDir   = 1;
static const uint32_t LEAN_MS = 2600;
// Shake it off: a quick shudder after a deauth, an evil twin or a hacker
// tool, once the double-take has finished with him.
static uint32_t s_shakeStart = 0;
static const uint32_t SHAKE_MS = 650;
// Backing away: the same kind of thing, seen again and closer, moves him a
// step further from centre each time. Steps fade once things go quiet.
static int8_t        s_backSteps  = 0;
static uint32_t      s_backAt     = 0;
static int8_t        s_prevRssi   = 0;
static DetectionType s_prevType   = DetectionType::UNKNOWN;
static uint32_t      s_prevAt     = 0;
// Flick: a fast swipe sends him sliding into that side's wall.
static uint32_t s_flickStart = 0;
static int8_t   s_flickDir   = 1;
static const uint32_t FLICK_OUT_MS = 380, FLICK_HOLD_MS = 420, FLICK_BACK_MS = 1100;
// Where the last tap landed: 0 nowhere, 1 head, 2 belly, 3 feet.
static uint8_t s_tapZone = 0;

static int   s_lastCx = -10000, s_lastHeadTopY = 0;
// The head's top for THIS frame, bob and squash included. Deliberately
// separate from s_lastHeadTopY, which lastFootprint() reports and which is
// the UN-animated base: a tap target that bobbed would move under a finger
// mid-press. Anything standing ON him needs the opposite.
static int   s_lastCrownY = 0;
// Set while drawBody() is drawing somebody who is NOT our Squachy: a visiting
// peer, the alert screen's cameo, the boot splash's wave. They all go through
// the same body code, and drawBody() publishes the live crown -- so the last
// one drawn won. On a SquachMesh visit the guest is drawn after the host, so
// the pet stood on the GUEST's head and rode the GUEST's bounce.
static bool  s_cameo = false;
// Top of the region the caller gave us. Costume detail that reaches ABOVE
// the head needs this: hy is not a fixed distance from the top of the
// drawing area -- he sits lower with a speech bubble up and rides higher
// without one -- so anything tall is fine in some frames and sliced off in
// others. Three passes at the wolf skull were lost to exactly that. With a
// real limit, tall detail can be clamped instead of guessed at.
static int   s_topLimit = -10000;
// The smallest y hy will take across the whole bob cycle -- the top of the
// bounce, not the current frame. Costume detail that has to clear the region
// top needs this rather than hy: sizing against a moving anchor makes the
// detail itself change size as he moves, which reads as the costume breathing.
static int   s_hyCeiling = -10000;
static float s_lastScale = 1.0f;

// After this long with no interaction at all (not even idle quips
// count — this tracks real engagement), he dozes off instead of
// standing around wide awake forever. Long enough that the regular
// idle fun (quips, bounces, walks, the rare party moment) gets plenty
// of room to happen first — napping is the last resort, not the
// default state.
static const uint32_t SLEEPY_AFTER_MS = 600000;
// A nap runs for at most this long in one stretch, then he's back at
// it -- without this cap the sleepy check re-triggers every idle cycle
// forever (idleFor only ever grows while nothing happens), so he'd
// just nap indefinitely until someone interacts. s_napStart marks when
// the CURRENT stretch began (0 = not napping); once it's been running
// too long, s_napCooldownUntil holds off the next nap for a while so
// he doesn't immediately fall right back asleep.
static const uint32_t NAP_DURATION_MS = 60000;
static uint32_t       s_napStart = 0;
static uint32_t       s_napCooldownUntil = 0;

static const char* pick(const char* const* arr, int n) {
    return arr[random(0, n)];
}

// True while the idle roll below is choosing a line. BANTER's IMPORTANT
// setting drops what it says and keeps what it does: the moods, the walks
// and the naps go on, only the bubble stays empty.
static bool s_idleRoll = false;

// The volume of the next bubble: 0 normal, 1 loud (a double outline, for a
// RARE catch), 2 quiet (dim text, for a catch in the small hours). Set
// before say(); say() takes it for that bubble and clears it.
static uint8_t s_nextTone = 0, s_bubbleTone = 0;
static Squachy::IdleProvider s_idleProvider = nullptr;
static const char* s_noticeLine = nullptr;

static void say(const char* line, uint32_t ms) {
    if (s_idleRoll && Settings::banter() == 0) return;
    bubbleText  = line;
    bubbleStart = millis();
    bubbleUntil = bubbleStart + tempo(ms);
    s_bubbleTone = s_nextTone;
    s_nextTone   = 0;
}

void setIdleProvider(IdleProvider fn) { s_idleProvider = fn; }

// Every bubble stays up at least this long, no matter which line fires.
static const uint32_t MIN_BUBBLE_MS_BASE = 5000;
static uint32_t       MIN_BUBBLE_MS = 5000;

// Curated, cycle-through options rather than free-text entry — there's
// no keyboard UI on this device worth building just for a nickname.
static const char* const STRETCH_LINES[] = {
    "Nnngh. Okay. I'm up.",
    "Five more minutes. ...Fine.",
    "That's the good stretch.",
    "Back on watch.",
};
static const char* const GUM_LINES[] = {
    "Watch this.",
    "Bubblegum. Standard issue.",
    "Been saving this one.",
    "Perfectly good stakeout snack.",
};
static const char* const LEAN_LINES[] = {
    "Did you hear that?",
    "Hold on. Something's out there.",
    "Shh. Listening.",
    "That was a noise. That was definitely a noise.",
};
static const char* const TRIP_LINES[] = {
    "Who put that there?",
    "I meant to do that.",
    "Nobody saw that. Good.",
    "The floor moved. I'm sure of it.",
};
static const char* const SHAKE_LINES[] = {
    "Ugh. Get it off me.",
    "That one felt slimy.",
    "Bad radio. BAD.",
};
static const char* const BACK_LINES[] = {
    "It's getting closer.",
    "Okay. Personal space. Please.",
    "Closer. Closer. I don't like closer.",
    "I'll just be... over here.",
};
static const char* const SIT_LINES[] = {
    "Taking a knee.",
    "Nothing's happening. I'm sitting.",
    "Wake me if the cameras move.",
    "Five minute break. Union rules.",
};
static const char* const TICKLE_HEAD_LINES[] = {
    "Head pats. Acceptable.",
    "Right there. Yes.",
    "The fur is soft. I know.",
};
static const char* const TICKLE_BELLY_LINES[] = {
    "Hehe. Stop. Don't stop.",
    "That tickles! That TICKLES!",
    "Belly is off limits. Mostly.",
};
static const char* const TICKLE_FEET_LINES[] = {
    "Not the feet!",
    "HEY. Feet are private.",
    "I will kick. I have kicked before.",
};
static const char* const FLICK_LINES[] = {
    "HEY!",
    "Rude.",
    "I'm walking back. Slowly. To make a point.",
    "Ow. My dignity.",
};

static const char* const DUCK_LINES[] = {
    "Damn toaster nearly got me!",
    "That one had my name on it.",
    "Watch where you're flying!",
    "Who keeps launching those things?",
    "Missed me, chrome-wing.",
};
static const char* const JUGGLE_LINES[] = {
    "Look what I caught.",
    "Three at once. Casual.",
    "Busy out there, huh?",
    "Juggling the evidence.",
};

static const char* const NICKNAMES[] = {
    "SQUACHY", "BIGSY", "FOOTS", "STOMPER", "SHADOW",
    "TRACKER", "CHONK", "WOODS", "YETI", "SASSY",
};
static const uint8_t NICKNAMES_N = sizeof(NICKNAMES) / sizeof(NICKNAMES[0]);

// Shades lens tint options — unlockedShadeCount() below gates how many
// of these cycleShadesColor() can actually reach, based on pet count.
static const char* const SHADE_NAMES[] = { "CYAN", "PINK", "GREEN", "PURPLE" };
static const uint8_t SHADE_NAMES_N = sizeof(SHADE_NAMES) / sizeof(SHADE_NAMES[0]);

static uint8_t unlockedShadeCount() {
    if (s_petCount >= 50) return 4;
    if (s_petCount >= 25) return 3;
    if (s_petCount >= 10) return 2;
    return 1;
}

// Outfits (see squachy.h). NONE/TANOOKI/UNICORN are free (threshold 0);
// the rest unlock by lifetime detection count, same stat as
// GrowthStage below, listed in ascending threshold order so "how many
// are unlocked" is just "how many from the front of this list
// qualify" — see unlockedOutfitCountInternal(). 25/100 deliberately
// line up with the TRACKER/VETERAN growth-stage milestones.
enum class OutfitId : uint8_t {
    NONE, TANOOKI, UNICORN,
    TINFOIL, SHADOW, PLUMBER, TALLBRO, SPACE, BLUEBLUR,
    CAPTAIN,
    WOLFPELT,
    CHROMEWING,
    VOIDEYE,
    PARKA,
    SHARK,
    COUNT
};

struct OutfitDef { const char* name; uint32_t threshold; };
// Threshold sentinel: this outfit is not unlocked by lifetime count at
// all, so no reachable total should ever satisfy it.
static const uint32_t OUTFIT_BY_EVENT = 0xFFFFFFFFu;
// Names are what the settings row and the outfit screen print, so they are
// kept short: "TANOOKI SQUACH" and "CAPTAIN SQUACH" were fourteen characters
// and ran off the end of the OUTFIT row. Dropping the redundant "SQUACH"
// takes the longest name from fourteen down to eleven, and every one of
// them is already displayed next to a picture of him.
static const OutfitDef OUTFITS[] = {
    { "NONE",           0 },
    { "TANOOKI", 0 },
    { "UNICORN",        0 },
    { "TINFOIL", 5 },
    { "SHADOW",  15 },
    { "PLUMBER BRO",    25 },
    { "TALL BRO",       40 },
    { "SPACE",   60 },
    { "BLUE BLUR",      100 },
    { "CAPTAIN", 150 },
    // Not earned by counting anything: OUTFIT_BY_EVENT marks it as
    // unlocked by something happening instead -- summoning the werewolf
    // on the FIRE background. See outfitUnlocked().
    { "WOLF PELT",      OUTFIT_BY_EVENT },
    // Earned by catching the rare gold toaster on the TOASTERS
    // background -- see Theme::consumeToasterCatch().
    { "CHROME WING",    OUTFIT_BY_EVENT },
    // Earned by catching two eyes in a row on the STARFIELD background --
    // see Theme::consumeEyeCatch().
    { "VOID EYE",       OUTFIT_BY_EVENT },
    // Earned by knocking five times on the lodge on the SNOWFALL background --
    // see Theme::consumeLodgeKnock().
    { "SNOW PARKA",     OUTFIT_BY_EVENT },
    // Earned by catching the AQUARIUM shark -- twice over, since the first
    // touch only turns him. See Theme::consumeSharkCatch().
    { "SHARK SUIT",     OUTFIT_BY_EVENT },
};
static const uint8_t OUTFITS_N = sizeof(OUTFITS) / sizeof(OUTFITS[0]);
static_assert(OUTFITS_N == (uint8_t)OutfitId::COUNT, "OUTFITS must match OutfitId");
#if SQUACH_MESH
// The mesh clamps a visitor's outfit index to what this build can draw. If
// a new outfit lands here without that count moving, the newest outfit
// wraps to NONE on every visit -- which is how the shark suit went missing.
static_assert(SquachMesh::OUTFIT_N == (uint8_t)OutfitId::COUNT, "SquachMesh::OUTFIT_N must match OutfitId::COUNT");
#endif

// Was a prefix count -- "how many from the front of the list qualify" --
// which only works while every outfit is gated on the same ascending
// stat. WOLF PELT is not: it is earned by summoning the werewolf, so it
// can be unlocked while outfits before it are still locked, and the set
// stops being a contiguous prefix. Testing each index independently is
// what makes a hole in the middle representable.
static bool outfitUnlocked(uint8_t i) {
    if (s_allOutfitsUnlocked)                     return true;
    // OUTFIT_BY_EVENT says "not earned by counting"; which event is a
    // property of the outfit, so it is switched on here rather than
    // encoded in the threshold. Two of these now, and adding a third is
    // one case label.
    if (OUTFITS[i].threshold == OUTFIT_BY_EVENT) {
        switch ((OutfitId)i) {
            case OutfitId::WOLFPELT:   return s_wolfPeltUnlocked;
            case OutfitId::CHROMEWING: return s_chromeWingUnlocked;
            case OutfitId::VOIDEYE:    return s_voidEyeUnlocked;
            case OutfitId::PARKA:      return s_parkaUnlocked;
            case OutfitId::SHARK:      return s_sharkUnlocked;
            default:                   return false;
        }
    }
    return s_cachedLifetimeTotal >= OUTFITS[i].threshold;
}

static uint8_t unlockedOutfitCountInternal() {
    uint8_t n = 0;
    for (uint8_t i = 0; i < OUTFITS_N; i++) if (outfitUnlocked(i)) n++;
    return n;
}

// Next/previous unlocked entry, wrapping. Modulo on the unlocked COUNT
// only lands correctly when the unlocked set is a prefix; walking the
// list works whatever shape it has.
static uint8_t stepOutfit(uint8_t from, int8_t dir) {
    for (uint8_t k = 1; k <= OUTFITS_N; k++) {
        uint8_t cand = (uint8_t)((from + OUTFITS_N + dir * (int)k) % OUTFITS_N);
        if (outfitUnlocked(cand)) return cand;
    }
    return 0;                                   // NONE is always unlocked
}

// Compares what is unlocked now against what has already been announced
// and queues the difference. Called from every trigger() that can move
// the lifetime total, and from the two event-driven unlocks.
//
// The 0xFFFF default on "outfitSeen" is doing real work: a device that has
// never written the key is either brand new or is upgrading from firmware
// that predates unlock popups, and in both cases the correct behaviour is
// "announce nothing retroactively". Reading all-ones and then immediately
// rewriting it to the true current set means the first scan announces
// nothing, and everything earned from then on is genuinely new.
static void refreshOutfitUnlocks() {
    static bool seeded = false;
    uint32_t nowMask = 0;
    for (uint8_t i = 1; i < OUTFITS_N; i++)          // NONE is never announced
        if (outfitUnlocked(i)) nowMask |= (1u << i);

    if (!seeded) {
        seeded = true;
        if (s_outfitAnnounced == 0xFFFFFFFFu) {
            s_outfitAnnounced = nowMask;
            s_petPrefs.putUInt("outfitSeen", s_outfitAnnounced);
            return;
        }
    }

    const uint32_t fresh = nowMask & ~s_outfitAnnounced;
    if (!fresh) return;
    for (uint8_t i = 1; i < OUTFITS_N; i++) {
        if (!(fresh & (1u << i))) continue;
        if (s_outfitQueueN >= (uint8_t)(sizeof(s_outfitQueue) / sizeof(s_outfitQueue[0]))) break;
        s_outfitQueue[s_outfitQueueN++] = i;
    }
}

// Clamps s_outfitIdx back to NONE if it's pointing past what's
// currently unlocked -- the only way that happens is RESET STATS
// zeroing lifetimeTotal out from under a costume that needed it.
// Assumes prefs are already loaded, same as the rest of drawBody()'s
// direct s_shadeIdx/etc. reads -- safe because trigger(BOOTED) loads
// them at boot, before any tick() ever runs.
// Set while the unlock popup is drawing, so the whole existing render
// path (drawBody -> drawOutfit, plus the handful of per-outfit special
// cases in tick()) shows an outfit the player has not switched to. An
// override rather than a parallel "draw this outfit" entry point: the
// outfit is consulted from several places in the draw, and duplicating
// that path would leave two versions to keep in step.
static int8_t s_outfitOverride = -1;
static int8_t s_shadeOverride  = -1;

// Every read of the shade tint goes through this so an override cannot be
// half-applied -- the lens and the frame are drawn in different places and
// two of them disagreeing would be worse than no override at all.
static uint8_t activeShadeIdx() {
    if (s_shadeOverride >= 0) return (uint8_t)s_shadeOverride;
    return s_shadeIdx;
}

static OutfitId currentOutfit() {
    if (s_outfitOverride >= 0 && s_outfitOverride < (int8_t)OUTFITS_N)
        return (OutfitId)s_outfitOverride;
    if (s_outfitIdx >= OUTFITS_N || !outfitUnlocked(s_outfitIdx)) s_outfitIdx = 0;
    return (OutfitId)s_outfitIdx;
}

// Permanent fur re-tint milestones, gated off the same lifetime total
// as the detection-milestone quips (see MILESTONES) rather than a
// separate threshold set — reuses s_cachedLifetimeTotal, which is kept
// current by every DETECTION/BOOTED trigger. Legend also unlocks the
// small hat drawn in drawBody().
enum class GrowthStage : uint8_t { FLEDGLING, TRACKER, VETERAN, LEGEND };

static GrowthStage currentStage() {
    if (s_cachedLifetimeTotal >= 500) return GrowthStage::LEGEND;
    if (s_cachedLifetimeTotal >= 100) return GrowthStage::VETERAN;
    if (s_cachedLifetimeTotal >= 25)  return GrowthStage::TRACKER;
    return GrowthStage::FLEDGLING;
}

static char s_statBuf[56];

// A line built from real numbers instead of picked from a static pool
// -- "we've caught 47 things together" style. Folded into the idle
// chatter pool at low frequency (see tick()), only once there's
// actually a meaningful number to report.
static const char* buildStatLine() {
    switch (random(0, 4)) {
        case 0:
            snprintf(s_statBuf, sizeof(s_statBuf),
                     "We've caught %lu things together.", (unsigned long)s_cachedLifetimeTotal);
            break;
        case 1:
            snprintf(s_statBuf, sizeof(s_statBuf),
                     "Boot #%lu. Still watching.", (unsigned long)s_bootCount);
            break;
        case 2:
            snprintf(s_statBuf, sizeof(s_statBuf),
                     "You've petted me %lu times. Not that I'm counting.", (unsigned long)s_petCount);
            break;
        default: {
            uint32_t mins = s_bestClearMs / 60000;
            snprintf(s_statBuf, sizeof(s_statBuf),
                     "Best clear streak: %lu min. Bet we beat it.", (unsigned long)mins);
            break;
        }
    }
    return s_statBuf;
}

// A comment on whichever background is currently active — indexed
// directly by Settings::Background. The static_assert is the actual
// "kept in sync" mechanism -- this used to just be a comment saying
// so, and silently went stale (still had rows for three backgrounds
// that were removed from the enum) without anything catching it,
// which meant every background after the removed ones was reading
// the wrong row's lines for a while. Folded into idle chatter
// alongside buildStatLine() (see tick()).
static const char* const BG_LINES[][3] = {
    /* DIGITAL   */ { "Digital rain again. Very hacker of me.", "Falling code, brown fur. Bold combo.", "I could read this if I tried. I won't." },
    /* STARFIELD */ { "Starfield's up. Feeling cosmic.", "Somewhere out there, a bigger cryptid.", "Space is just the woods, but darker." },
    /* TOASTERS  */ { "Flying toasters. A classic.", "Nobody needs that much toast airborne.", "After Dark energy today." },
    /* AQUARIUM  */ { "Aquarium mode. Very zen.", "Fish don't do opsec. Rookies.", "I'd get a tank but I'm camera-shy." },
    /* TERMINAL  */ { "Terminal log background. Very my speed.", "Green text, brown fur, good times.", "Looks official. It's mostly vibes though." },
    /* FIREFLIES */ { "Fireflies out tonight. Nice.", "Little lights, big ambiance.", "They're not surveillance. I checked." },
    /* FIRE      */ { "Fire background. Cozy, not concerning.", "Warm vibes, zero smoke alarms.", "Nothing's actually burning. Probably." },
    /* SNOWFALL  */ { "Snowing again. Big feet, better traction.", "Perfect weather for leaving mysterious tracks.", "Cold out. I'm built for this." },
    /* SPECTRUM  */ { "RF spectrum's live. That's the real stuff.", "This is actual signal data. Neat, right?", "Watching the airwaves. Very on-brand." },
    // TUNNEL is retired and unreachable; the row stays because the assert
    // below counts rows, and a missing one would silently shift every
    // background after it onto the wrong lines.
    /* TUNNEL    */ { "Wireframe tunnel. Very retro-future.", "Feels like we're going somewhere. We're not.", "80s sci-fi vibes today." },
    /* SYNTHWAVE */ { "That sunset never actually sets. I checked.", "Grid goes on forever. So does the drive.", "Look at that reflection. Water we even doing." },
    // He is switched off in boring mode, so nobody will ever hear these.
    // Written anyway: the assert below wants a row, and a placeholder row
    // is how the last set of stale lines got in.
    /* BLACK     */ { "Nothing on the walls today.", "Just us and the dark. Suits me.", "Minimalist phase. It happens." },
};
static_assert(sizeof(BG_LINES) / sizeof(BG_LINES[0]) == Settings::BACKGROUND_COUNT,
              "BG_LINES must have exactly one row per Settings::Background value -- "
              "a row count mismatch here means some background is silently reading "
              "another one's lines (see the incident this assert was added after).");

// ---- unlock hints -----------------------------------------------------------
// Every hidden unlock lives in a background, and the point of them is that
// people FIND them -- so on the right background, while it is still locked,
// his background chatter turns into hints. They escalate: the first time he
// brings one up it is a nudge, the second is clearer, and from then on he just
// tells you what to do. Counted in RAM, so a reboot starts the nudges over,
// which is right for somebody who has not been paying attention.
enum Egg : uint8_t { EGG_WOLF, EGG_CHROME, EGG_PET, EGG_EYE, EGG_PARKA, EGG_N };
static const char* const HINTS[EGG_N][3] = {
    /* FIRE: five taps on the moon */
    { "That moon's got a werewolf look to it.",
      "The moon might answer if you knock.",
      "Tap the moon five times, quick!" },
    /* TOASTERS: the rare gold toaster */
    { "Keep an eye out for a shiny toaster.",
      "The gold ones are rare. Catch one.",
      "See a gold toaster? Tap it!" },
    /* TOASTERS: the little guy who walks the ground */
    { "Something little wanders by sometimes.",
      "A little guy walks by every few minutes.",
      "When the little guy walks by, tap him!" },
    /* STARFIELD: two big eyes in a row */
    { "Sometimes space looks back at you.",
      "Catch a big eye while it's close.",
      "Tap two big eyes in a row. Miss none!" },
    /* SNOWFALL: five knocks on the lodge */
    { "That lodge on the ridge looks lived in.",
      "Five windows on that lodge. Wonder why.",
      "Knock on the lodge five times. Quick!" },
};
static uint8_t s_hintSaid[EGG_N] = {};

static void ensurePrefsLoaded();

static bool eggLocked(uint8_t e) {
    ensurePrefsLoaded();
    switch (e) {
        case EGG_WOLF:   return !outfitUnlocked((uint8_t)OutfitId::WOLFPELT);
        case EGG_CHROME: return !outfitUnlocked((uint8_t)OutfitId::CHROMEWING);
        case EGG_PET:    return !s_petUnlocked;
        case EGG_EYE:    return !outfitUnlocked((uint8_t)OutfitId::VOIDEYE);
        case EGG_PARKA:  return !outfitUnlocked((uint8_t)OutfitId::PARKA);
        default:         return false;
    }
}

// The locked unlocks this background holds, at most two.
static uint8_t eggsHere(uint8_t out[2]) {
    uint8_t n = 0;
    auto add = [&](uint8_t e) { if (n < 2 && eggLocked(e)) out[n++] = e; };
    switch (Settings::background()) {
        case Settings::Background::FIRE:      add(EGG_WOLF); break;
        case Settings::Background::TOASTERS:  add(EGG_CHROME); add(EGG_PET); break;
        case Settings::Background::STARFIELD: add(EGG_EYE); break;
        case Settings::Background::SNOWFALL:  add(EGG_PARKA); break;
        default: break;
    }
    return n;
}

static bool hintAvailable() {
    uint8_t e[2];
    return eggsHere(e) > 0;
}

static const char* pickBackgroundLine() {
    uint8_t idx = (uint8_t)Settings::background();
    if (idx >= Settings::BACKGROUND_COUNT) idx = 0;
    // Two times in three, if there is something to find here, a hint instead.
    uint8_t eggs[2];
    const uint8_t n = eggsHere(eggs);
    if (n && random(0, 3) != 0) {
        const uint8_t e = eggs[random(0, n)];
        const uint8_t said = s_hintSaid[e];
        // Nudge, clearer, then the answer -- and after that the clear one or
        // the answer, so it does not become the same sentence every time.
        const uint8_t lvl = said < 2 ? said : (uint8_t)random(1, 3);
        if (said < 255) s_hintSaid[e]++;
        return HINTS[e][lvl];
    }
    return BG_LINES[idx][random(0, 3)];
}

// All of this module's persisted fields share one NVS namespace and
// used to each have their own duplicated lazy-load guard at every call
// site that needed one; now there's one shared loader instead.
static void ensurePrefsLoaded() {
    if (s_petPrefsLoaded) return;
    s_petPrefs.begin("squachy", false);
    s_petCount        = s_petPrefs.getUInt("pets", 0);
    s_bootCount       = s_petPrefs.getUInt("boots", 0);
    s_bestClearMs     = s_petPrefs.getUInt("bestClrMs", 0);
    s_bestSessionCount = s_petPrefs.getUInt("bestSess", 0);
    s_firstType       = s_petPrefs.getUChar("firstType", (uint8_t)DetectionType::UNKNOWN);
    s_shadeIdx        = s_petPrefs.getUChar("shadeIdx", 0);
#if SQUACH_MESH
    s_petPrefs.getString("cname", s_customName, sizeof(s_customName));
#endif
    s_nickIdx         = s_petPrefs.getUChar("nick", 0);
    s_outfitIdx       = s_petPrefs.getUChar("outfitIdx", 0);
    s_allOutfitsUnlocked = s_petPrefs.getBool("allOutfits", false);
    s_petUnlocked        = s_petPrefs.getBool("petUnlk", false);
    s_petAnnounced       = s_petPrefs.getBool("petSeen", false);
    // Defaults ON once earned: somebody who just unlocked a pet wants to
    // see it, not to go and find a switch.
    // "petSel" is the one to read; a board from before there were two
    // companions has only the old on/off bool, which becomes SHAGGY or OFF.
    s_petSel             = s_petPrefs.getUChar("petSel",
                               s_petPrefs.getBool("petOn", true) ? (uint8_t)PetId::SHAGGY : 0u);
    if (s_petSel >= (uint8_t)PetId::COUNT) s_petSel = (uint8_t)PetId::SHAGGY;
    s_wolfPeltUnlocked   = s_petPrefs.getBool("wolfPelt", false);
    s_chromeWingUnlocked = s_petPrefs.getBool("chromeWing", false);
    s_voidEyeUnlocked    = s_petPrefs.getBool("voidEye", false);
    s_parkaUnlocked      = s_petPrefs.getBool("parka", false);
    s_sharkUnlocked      = s_petPrefs.getBool("shark", false);
    s_outfitAnnounced    = s_petPrefs.getUInt("outfitSeen", 0xFFFFFFFFu);
    s_petPrefsLoaded  = true;
}

// Defined further down (needs drawOnboardBubble's constants) — forward
// declared so trigger()'s BOOTED case can start the walkthrough on a
// device's very first boot.
static void startOnboardingInternal();

void catchContext(const char* regular, bool newRegular, bool nemesis, bool newClosest) {
    s_ctxRegular    = regular;
    s_ctxNewRegular = newRegular;
    s_ctxNemesis    = nemesis;
    s_ctxNewClosest = newClosest;
}

void trigger(Event evt, DetectionType dt, uint32_t lifetimeTotal, uint32_t hitCount,
             int8_t rssi, Confidence conf) {
    uint32_t now = millis();
    lastInteraction = now;
    switch (evt) {
        case Event::DETECTION: {
            mood = Mood::SHOCKED;
            moodUntil = now + tempo(1400);
            s_hostReact = -1;  // a real one, not an emote's borrowed pose
            s_dtStart = now;   // see the double-take offset in tick()
            // Map RSSI onto 0..1. Anything at or below -100 dBm is the
            // floor and anything above -40 is on top of you; 0 means the
            // caller had no reading, which lands mid-scale rather than
            // at either extreme.
            if (rssi == 0) {
                s_recoilK = 0.55f;
            } else {
                float k = ((float)rssi + 100.0f) / 60.0f;
                if (k < 0.0f) k = 0.0f;
                if (k > 1.0f) k = 1.0f;
                s_recoilK = k;
            }
            s_reactType = dt;
            // Attacks get a shudder once the double-take has run its course.
            if (dt == DetectionType::DEAUTH || dt == DetectionType::EVILTWIN || dt == DetectionType::HACKER)
                s_shakeStart = now + DT_TOTAL_MS;
            // The same kind of thing, back within half a minute and at least
            // six dB closer: he takes a step back. Three steps is as far as he
            // goes, and the steps fade once it has been quiet a while.
            if (rssi != 0) {
                if (dt == s_prevType && now - s_prevAt < 30000u && rssi >= s_prevRssi + 6) {
                    if (s_backSteps < 3) s_backSteps++;
                    s_backAt = now;
                    say(pick(BACK_LINES, 4), MIN_BUBBLE_MS);
                }
                s_prevRssi = rssi;
                s_prevType = dt;
                s_prevAt   = now;
            }
            // Newest-first ring of three, for the juggle. Shifted rather
            // than indexed so the oldest simply falls off the end.
            s_recentTypes[2] = s_recentTypes[1];
            s_recentTypes[1] = s_recentTypes[0];
            s_recentTypes[0] = dt;
            ensurePrefsLoaded();
            s_cachedLifetimeTotal = lifetimeTotal;
            refreshOutfitUnlocks();

            // Streak/session/heat bookkeeping for the Diary screen and
            // the activity-biased idle chatter below — none of this
            // needs wall-clock time, just gaps between millis().
            if (s_haveLastDetection) {
                uint32_t gap = now - s_lastDetectionAt;
                if (gap > s_bestClearMs) {
                    s_bestClearMs = gap;
                    s_petPrefs.putUInt("bestClrMs", s_bestClearMs);
                }
            }
            s_lastDetectionAt   = now;
            s_haveLastDetection = true;
            s_sessionDetections++;
            if (s_sessionDetections > s_bestSessionCount) {
                s_bestSessionCount = s_sessionDetections;
                s_petPrefs.putUInt("bestSess", s_bestSessionCount);
            }
            if (s_firstType == (uint8_t)DetectionType::UNKNOWN && dt != DetectionType::UNKNOWN) {
                s_firstType = (uint8_t)dt;
                s_petPrefs.putUChar("firstType", s_firstType);
            }
            s_activityHeat = s_activityHeat + 30.0f > 100.0f ? 100.0f : s_activityHeat + 30.0f;

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

            // Loud for a RARE card, quiet in the small hours -- see s_nextTone.
            s_nextTone = Dex::rarity(dt) == Dex::Rarity::RARE ? 1 : (Clock::night() ? 2 : 0);
            // The run of the same type: ten minutes between them at most.
            if (dt == s_runType && now - s_runAt < 600000u) { if (s_runN < 255) s_runN++; }
            else { s_runType = dt; s_runN = 1; }
            s_runAt = now;
            const char* regular    = s_ctxRegular;
            const bool  newRegular = s_ctxNewRegular, nemesisHit = s_ctxNemesis, newClosest = s_ctxNewClosest;
            s_ctxRegular = nullptr; s_ctxNewRegular = s_ctxNemesis = s_ctxNewClosest = false;
            static char ctxBuf[80];

            if (hit > 0) {
                s_lastMilestone = hit;
                snprintf(s_milestoneBuf, sizeof(s_milestoneBuf),
                         "Detection #%lu! Milestone.", (unsigned long)hit);
                say(s_milestoneBuf, 5500);
            } else if (newRegular && regular) {
                snprintf(ctxBuf, sizeof ctxBuf, pick(NEW_REGULAR_LINES, 3), regular);
                say(ctxBuf, 5500);
            } else if (s_runN == 3 || s_runN == 5 || s_runN == 10) {
                const char* tmpl = s_runN == 3 ? pick(RUN3_LINES, 2) : s_runN == 5 ? pick(RUN5_LINES, 2) : RUN10_LINES[0];
                snprintf(ctxBuf, sizeof ctxBuf, tmpl, detectionTypeName(dt), detectionTypeName(dt));
                say(ctxBuf, 5500);
            } else if (newClosest && random(0, 3) != 0) {
                snprintf(ctxBuf, sizeof ctxBuf, pick(CLOSEST_LINES, 3), detectionTypeName(dt));
                say(ctxBuf, 5500);
            } else if (regular && random(0, 3) != 0) {
                snprintf(ctxBuf, sizeof ctxBuf, pick(REGULAR_LINES, 6), regular, regular);
                say(ctxBuf, 5000);
            } else if (nemesisHit && random(0, 2) == 0) {
                snprintf(ctxBuf, sizeof ctxBuf, pick(NEMESIS_LINES, 5), detectionTypeName(dt), detectionTypeName(dt));
                say(ctxBuf, 5000);
            } else if (hitCount >= 8) {
                // A device that's matched this many times isn't a
                // one-off ping — that's a real pattern worth calling
                // out plainly, every time (no dice roll), since it's
                // the more actionable signal.
                say(pick(PERSISTENT_LINES, 4), 5500);
            } else if (hitCount >= 3 && random(0, 2) == 0) {
                // A lighter "I recognize this one" tier — rolled, not
                // guaranteed, so a device that legitimately racks up
                // repeats doesn't say the same thing every single time.
                say(pick(SEEN_BEFORE_LINES, 4), 5000);
            } else {
                uint8_t idx = (uint8_t)dt;
                if (idx >= DET_LINES_N) idx = 0;
                const char* base = random(0, 2) ? DET_LINES[idx].a : DET_LINES[idx].b;
                static char oath[96];
                if (random(0, 3) == 0) {
                    snprintf(oath, sizeof oath, pick(OATH_LINES, OATH_LINES_N), detectionTypeName(dt));
                    base = oath;
                }
                // High confidence hits get the line straight — no need
                // to hedge on something we're actually sure about. Med
                // /Low get an honest number tacked on so a shakier
                // match doesn't read as equally certain.
                // Passed in from the sighting rather than looked up from the
                // type: the number he says out loud has to be about the
                // signature that actually matched.
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
        case Event::LOG_OPENED: {
            // Five opens inside a minute: he has noticed you checking.
            static uint32_t opens[5]; static uint8_t oi = 0;
            opens[oi] = now; oi = (uint8_t)((oi + 1) % 5);
            bool five = true;
            for (uint8_t i = 0; i < 5; i++) if (!opens[i] || now - opens[i] > 60000u) five = false;
            if (five) { say(pick(CHECKING_LINES, 3), MIN_BUBBLE_MS); memset(opens, 0, sizeof opens); }
            else      say(pick(LOG_OPEN_LINES, 2), MIN_BUBBLE_MS);
            break;
        }
        case Event::LOG_CLEARED:
            say(pick(LOG_CLEAR_LINES, LOG_CLEAR_N), MIN_BUBBLE_MS);
            break;
        case Event::ROTATED:
            say(pick(ROTATE_LINES, 3), MIN_BUBBLE_MS);
            break;
        case Event::BOOTED:
            s_lastTouchAt = now;
            ensurePrefsLoaded();
            s_cachedLifetimeTotal = lifetimeTotal;
            refreshOutfitUnlocks();
            s_bootCount++;
            s_petPrefs.putUInt("boots", s_bootCount);
            if (!s_petPrefs.getBool("onboarded", false)) {
                startOnboardingInternal();
            } else {
                say(pick(BOOT_LINES, 2), MIN_BUBBLE_MS);
            }
            break;
        case Event::PETTED: {
            s_lastTouchAt = now; s_ignoredSaid = false;
            mood = Mood::BOUNCE;
            moodUntil = now + tempo(1200);
            s_petFxStart = now;
            s_petFxUntil = now + 1900;
            ensurePrefsLoaded();
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
            } else if (s_tapZone == 1 && random(0, 2) == 0) {
                // A head pat: he leans into it rather than hopping.
                mood = Mood::IDLE;
                s_leanStart = now;
                s_leanDir   = 0;
                say(pick(TICKLE_HEAD_LINES, 3), MIN_BUBBLE_MS);
            } else if (s_tapZone == 2 && random(0, 2) == 0) {
                // The belly: a giggle, which is a faster, smaller bounce.
                moodUntil = now + tempo(1600);
                say(pick(TICKLE_BELLY_LINES, 3), MIN_BUBBLE_MS);
            } else if (s_tapZone == 3 && random(0, 2) == 0) {
                // The feet: one big hop, and an objection.
                moodUntil = now + tempo(700);
                say(pick(TICKLE_FEET_LINES, 3), MIN_BUBBLE_MS);
            } else {
                say(pick(PET_LINES, 20), MIN_BUBBLE_MS);
            }
            s_tapZone = 0;
            break;
        }
        case Event::HELD: {
            s_lastTouchAt = now; s_ignoredSaid = false;
            mood = Mood::BOUNCE;
            moodUntil = now + tempo(1500);
            s_petFxStart = now;
            s_petFxUntil = now + 2600;
            say(pick(HELD_LINES, 6), MIN_BUBBLE_MS);
            break;
        }
        case Event::PETTING: {
            s_lastTouchAt = now; s_ignoredSaid = false;
            mood = Mood::BOUNCE;
            moodUntil = now + tempo(900);
            s_petFxStart = now;
            s_petFxUntil = now + 1400;
            static uint32_t lastLineAt = 0;
            if (now - lastLineAt > 2000) {
                lastLineAt = now;
                say(pick(PETTING_LINES, 6), MIN_BUBBLE_MS);
            }
            break;
        }
    }
    nextIdleAt = now + tempo(15000) + random(0, 15000);
}

int crownY() { return s_lastCrownY; }

// The bottom of him, for hit-testing and for anything that stands beside
// him. 62 units is where his SHADOW is drawn -- the ground plane -- and 53
// is the soles of his boots, which is where he actually ends.
//
// With the shadow drawn, the ground plane is the right answer: a cameo
// walking past should plant its feet level with his shadow, not with his
// soles, because the shadow is what says where the floor is.
//
// With it off there is no floor to stand on and 62 is just nine units of
// empty air. Left at 62 after the shadow came off, this reached 26px below
// his boots at his current size -- far enough that a tap on the first
// counter line landed inside his hit box and petted him, and far enough
// that the pet stood in the headline rather than beside him.
static int footBottom() {
    return s_lastHeadTopY + (int)((SQUACHY_SHADOW ? 62.0f : 53.0f) * s_lastScale);
}
float lastScale() { return s_lastScale; }

bool lastFootprint(int& cx, int& halfW, int& top, int& bot) {
    if (s_lastCx < -5000) return false;
    cx    = s_lastCx;
    halfW = (int)(24 * s_lastScale);
    top   = s_lastHeadTopY - (int)(20 * s_lastScale);
    bot   = footBottom();
    return true;
}

static uint32_t s_grabAt = 0;   // the last time a finger said where he is

void grabTo(int x, int y) {
    s_grabbed   = true;
    s_dropStart = 0;
    s_grabX = x;
    s_grabY = y;
    s_grabAt = millis();
    lastInteraction = s_grabAt;
}

void release() {
    if (!s_grabbed) return;
    s_grabbed   = false;
    s_dropStart = millis();
    // Fall from exactly where he was let go rather than from the
    // finger's last raw position -- tick() clamps the carry into the
    // band, and starting the fall from the unclamped point would make
    // him jump before he dropped.
    s_dropX = s_lastCx;
    s_dropY = s_lastHeadTopY;
}

void toasterNear(int x, int y) {
    if (s_lastCx <= -9999) return;              // never drawn yet
    const uint32_t now = millis();
    if (now < s_duckCooldown) return;
    const float sc = s_lastScale;
    if (abs(x - s_lastCx) > (int)(30.0f * sc)) return;
    const int headY = s_lastHeadTopY;
    if (y < headY - (int)(16.0f * sc)) return;  // sailing well overhead
    if (y > headY + (int)(26.0f * sc)) return;  // passing below his chin
    s_duckUntil    = now + 900;
    // Half a minute between ducks. At 5 s this fired constantly: the
    // TOASTERS background keeps a whole flock on screen and every one of
    // them passes through his box, so the cooldown is the only thing
    // deciding how often this happens -- proximity alone is met almost
    // continuously.
    s_duckCooldown = now + 30000;
    // A detection outranks a kitchen appliance, so a startle keeps the
    // floor. He still ducks either way; he just does not comment on it.
    if (mood != Mood::SHOCKED) say(pick(DUCK_LINES, 5), 3000);
}

void setTempo(uint8_t pct) {
    s_tempoPct = pct < 50 ? 50 : (pct > 200 ? 200 : pct);
    DT_TOTAL_MS      = tempo(DT_TOTAL_MS_BASE);
    WALK_CYCLE_MS    = tempo(WALK_CYCLE_MS_BASE);
    WALK_DURATION_MS = WALK_CYCLE_MS * WALK_CYCLES;
    WATCH_EVERY_MS   = tempo(WATCH_EVERY_MS_BASE);
    GUM_GROW_MS      = tempo(GUM_GROW_MS_BASE);
    GUM_HOLD_MS      = tempo(GUM_HOLD_MS_BASE);
    GUM_POP_MS       = tempo(GUM_POP_MS_BASE);
    STRETCH_MS       = tempo(STRETCH_MS_BASE);
    SHOW_STEP_MS     = tempo(SHOW_STEP_MS_BASE);
    DROP_MS          = tempo(DROP_MS_BASE);
    LAND_MS          = tempo(LAND_MS_BASE);
    MIN_BUBBLE_MS    = tempo(MIN_BUBBLE_MS_BASE);
}

void startShowOff() {
    s_showOff   = true;
    s_showStart = millis();
    s_showIdx   = 0xFF;          // no step armed yet, so the first one arms
    s_showWB    = -1;
}

void stopShowOff() {
    if (!s_showOff) return;
    s_showOff   = false;
    s_showWB    = -1;
    s_grabbed   = false;
    s_dropStart = 0;
    s_binoc     = false;
    s_duckUntil = 0;
    s_backSteps = 0;
    s_flickStart = 0;
    s_shakeStart = 0;
    mood        = Mood::IDLE;
    bubbleUntil = 0;
}

bool showOffActive() { return s_showOff; }

bool hitTest(int x, int y) {
    // Generous fixed bounding box (not a pixel-perfect silhouette
    // test) sized off his last known position/scale — good enough for
    // a fingertip, and matches how forgiving every other tap target in
    // this UI already is.
    if (s_lastCx < -5000) return false;
    int halfW = (int)(24 * s_lastScale);
    int top   = s_lastHeadTopY - (int)(20 * s_lastScale);
    int bot   = footBottom();
    return x >= s_lastCx - halfW && x <= s_lastCx + halfW && y >= top && y <= bot;
}

void noteTapAt(int x, int y) {
    (void)x;
    s_tapZone = 0;
    if (s_lastCx < -5000) return;
    const int top = s_lastHeadTopY - (int)(20 * s_lastScale);
    const int bot = footBottom();
    if (y < top || y > bot) return;
    const int span = bot - top;
    if (span <= 0) return;
    const int k = (y - top) * 3 / span;
    s_tapZone = k <= 0 ? 1 : (k == 1 ? 2 : 3);
}

void flick(int8_t dir) {
    if (s_grabbed || s_onboardActive) return;
    s_flickStart = millis();
    s_flickDir   = dir >= 0 ? 1 : -1;
    lastInteraction = s_flickStart;
    mood = Mood::IDLE;
    moodUntil = 0;
    say(pick(FLICK_LINES, 4), MIN_BUBBLE_MS);
}

// ---- Drawing ----
// Tracks the previous frame's bubble footprint (whichever of the two
// draw functions below drew it) so we can erase exactly that
// rectangle when the bubble changes or goes away — the rest of the
// row stays untouched, so digital rain shows through whenever Squachy
// isn't actively saying something.
static int  lastBubbleX = 0, lastBubbleY = 0, lastBubbleW = 0, lastBubbleH = 0;
static bool hadBubble   = false;

// Everyday speech bubble stays single-line and text-hugging (its
// original compact look) whenever the line actually fits — most
// idle/pet-reaction lines do. Only when a line is too wide for a
// narrow portrait screen does it grow into a fixed-width, centered
// multi-line box instead of letting the single-line version run off
// (or past) both screen edges, which is what happened before this.
static const uint8_t BUBBLE_MAX_LINES = 2;

// A bubble used to exist on one frame and not on the frame before it,
// which is the most conspicuously un-animated thing on the CLEAR
// screen. This grows the box out of nothing over BUBBLE_POP_MS with a
// small overshoot so it settles rather than snapping to size.
//
// Both bubble layouts call this with their own final box and return
// early if it reports true, so the compact and the wrapped one animate
// identically. Text is skipped for the duration on purpose: there is no
// way to scale a font on this display, and text laid into a half-width
// box would clip rather than shrink.
//
// Pure function of millis() and bubbleStart -- no state of its own --
// so a board rendering in two physical bands draws the same size in
// both of them.
static const uint32_t BUBBLE_POP_MS = 220;

static bool bubblePop(TFT_eSPI& t, int bx, int topY, int bw, int bh, uint32_t now) {
    // `now` is passed in rather than read from millis(). This was the one
    // animation in the file reading a different clock than everything else,
    // and it cost the emulator every speech bubble it ever rendered: the
    // one-shot renderer drives `now` as virtual time but leaves millis() on
    // the wall clock, and it draws far more than 220ms of frames in under
    // 220ms of real time -- so the pop never completed and every bubble came
    // out a flat sliver. Hardware was always fine, which is exactly what
    // makes it the kind of instrument fault worth hunting.
    // CLAMPED, not bailed out of. say() timestamps with millis() and runs
    // DURING the frame, while `now` was taken at the top of it -- so on the
    // frame a bubble is born, bubbleStart is a few milliseconds in the
    // future. Returning false there drew the bubble at FULL SIZE for one
    // frame before the pop took over, so every bubble appeared, snapped to
    // nothing, and grew back. Reported from hardware as a flicker.
    //
    // That was introduced by the fix that made this take `now` at all: the
    // old version read millis() for both ends, so the two could never
    // disagree. Fixing the emulator broke the device, which is the exact
    // inverse of the fault being fixed, and is why this reads as a clamp
    // rather than a guard.
    const uint32_t e = (now >= bubbleStart) ? (now - bubbleStart) : 0;
    if (e >= BUBBLE_POP_MS) return false;
    const float u = (float)e / (float)BUBBLE_POP_MS;
    // Ease out past 1.0 and back: peaks at 1.12 three-quarters of the
    // way through, settles exactly on 1.0.
    const float k = (u < 0.75f) ? (u / 0.75f) * 1.12f
                                : 1.12f - 0.12f * ((u - 0.75f) / 0.25f);
    int pw = (int)(bw * k), ph = (int)(bh * k);
    if (pw < 5) pw = 5;
    if (ph < 4) ph = 4;
    int px = bx + (bw - pw) / 2;
    int py = topY + (bh - ph) / 2;
    // The overshoot frame is taller than the final box; let it grow
    // downward rather than up into the title bar.
    if (py < topY) py = topY;
    if (px < 2) px = 2;
    t.fillRoundRect(px, py, pw, ph, 3, Theme::BG);
    t.drawRoundRect(px, py, pw, ph, 3, Theme::VAPOR_PINK);
    lastBubbleX = px;
    lastBubbleY = py;
    lastBubbleW = pw;
    lastBubbleH = ph;
    return true;
}

// How far a bubble may climb into the rows the title bar used to own, and
// when it is allowed to. Those rows are not empty any more: the settings and
// rotate buttons live in the top corners and are now the ONLY navigation on
// the device, so a box that covers them is a worse bug than a bubble sitting
// a little close to his crest.
//
// A one-liner is centred and 90-200 wide, so it clears both corners and can
// rise. A WRAPPED bubble is screenW-16 across, pinned two pixels off the left
// edge, and would bury both -- it stays where it is. The test is geometric
// rather than a flag, so a one-liner beside a Squachy who has wandered far
// enough right to reach the rotate button also stays put.
static const int BUBBLE_RISE = 16;   // exactly the row tick() reserves
static const int CORNER_W    = 30;   // icon box plus a pixel of air
static const int CORNER_H    = 20;   // ICON_BOX_H over in theme.cpp

// Anything ELSE the caller has put in that band, measured from x = 0. Set from
// tick()'s topBandLeft on every call; 0 on every screen that has nothing there,
// which is every screen but the CrowPanel's CLEAR and its LORA pill.
static int s_topBandLeft = 0;

static int risenBubbleTop(int topY, int bx, int bw, int screenW) {
    // Clamped to row 1, not rejected below it. Every screen hands Squachy
    // topY = 16 and BUBBLE_RISE is 16, so ry is exactly 0 everywhere -- a
    // `< 1` test would fail on all of them and quietly switch the rise off
    // rather than move it a pixel. The second line is the other half: it
    // must never push a bubble DOWN, which is what a bare clamp would do on
    // a screen whose band started at 0.
    int ry = topY - BUBBLE_RISE;
    if (ry < 1)          ry = 1;        // always a pixel of air at the top
    if (ry >= topY)      return topY;   // no room to rise; never sink
    if (ry >= CORNER_H)  return ry;     // starts below the buttons anyway
    // The same geometric test, over everything that is actually up there: the
    // corner icons, and whatever the caller has told us about. A bubble too wide
    // to clear it stays at topY -- the top of his own band, where every wrapped
    // bubble sits -- rather than rising into something and being painted over by
    // it a moment later.
    const int keepL = s_topBandLeft > CORNER_W ? s_topBandLeft : CORNER_W;
    if (bx < keepL || bx + bw > screenW - CORNER_W) return topY;
    return ry;
}
// Hang a pointer off the bottom of a bubble, aimed at whoever said it.
//
// Position alone answers "who is talking" right up until there are two of
// them and the bubbles alternate; then the eye has to re-solve it every
// couple of seconds, and a conversation you have to keep decoding is not one
// you enjoy watching. Four rows is enough -- it is a cue, not a cartoon.
//
// The caller adds the four rows to lastBubbleH so the erase covers them,
// which is the only thing here that can leave a trail.
static void drawBubbleTail(TFT_eSPI& t, int bx, int bw, int ybot, int tailX) {
    int tx = tailX;
    const int lo = bx + 5, hi = bx + bw - 6;
    if (hi < lo) return;                 // bubble too narrow to carry one
    if (tx < lo) tx = lo;
    if (tx > hi) tx = hi;
    // Open the bubble's bottom border under the mouth of the tail first,
    // otherwise the pointer reads as a separate mark stuck below the box.
    t.drawFastHLine(tx - 2, ybot, 5, Theme::BG);
    for (int i = 0; i < 4; i++) {
        const int y  = ybot + i;
        const int hw = 3 - i;            // 3, 2, 1, 0 -- ends on a point
        if (hw > 0) t.drawFastHLine(tx - hw + 1, y, 2 * hw - 1, Theme::BG);
        t.drawPixel(tx - hw, y, Theme::VAPOR_PINK);
        t.drawPixel(tx + hw, y, Theme::VAPOR_PINK);
    }
}

// tailX: where the tail should point, or NO_TAIL for none.
static const int NO_TAIL = -10000;

// Font 2 for what he says: the 16-row proportional face, about one and a
// half times the 6x8 font the rest of the UI is laid out in. The bubble is
// the one piece of text on the main screen that is READ rather than
// glanced at, and on the 2.4" board it was a squint. The font is put back
// on the way out, so nothing drawn after a bubble notices.
static void drawBubbleIn(TFT_eSPI& t, int cx, int topY, const char* text,
                         uint32_t now, bool mayRise, int tailX);
// Desk mode holds his bubble while a squad message's box is up beside him:
// the bubble spans the screen and would sit across the message. He keeps
// moving; only the words wait.
static bool s_bubbleHeld = false;
void holdBubble(bool held) { s_bubbleHeld = held; }

static void drawBubble(TFT_eSPI& t, int cx, int topY, const char* text,
                       uint32_t now, bool mayRise = false, int tailX = NO_TAIL) {
    if (s_bubbleHeld) return;
    Theme::bubbleFontOn(t);
    drawBubbleIn(t, cx, topY, text, now, mayRise, tailX);
    Theme::bubbleFontOff(t);
}


static void drawBubbleIn(TFT_eSPI& t, int cx, int topY, const char* text,
                         uint32_t now, bool mayRise, int tailX) {
    t.setTextSize(1);
    t.setTextWrap(false);
    int screenW = t.width();
    int maxW = screenW - 16;   // widest a wrapped bubble is allowed to get
    int tw = t.textWidth(text);

    if (tw <= maxW - 10) {
        // Fits on one line -- the original compact box.
        int bw = tw + 10;
        int bh = Theme::bubbleTextH() + 6;
        int bx = cx - bw / 2;
        if (bx + bw > screenW - 2) bx = screenW - 2 - bw;
        if (bx < 2) bx = 2;
        const int by = mayRise ? risenBubbleTop(topY, bx, bw, screenW) : topY;
        if (bubblePop(t, bx, by, bw, bh, now)) return;
        t.fillRoundRect(bx, by, bw, bh, 3, Theme::BG);
        t.drawRoundRect(bx, by, bw, bh, 3, Theme::VAPOR_PINK);
        if (s_bubbleTone == 1) t.drawRoundRect(bx - 1, by - 1, bw + 2, bh + 2, 4, Theme::AMBER);
        t.setTextColor(s_bubbleTone == 2 ? Theme::W95_LIGHT : Theme::WHITE, Theme::BG);
        t.setCursor(bx + 5, by + 3 + Theme::bubbleAscent());
        t.print(text);
        lastBubbleX = bx;
        lastBubbleY = by;
        lastBubbleW = bw;
        lastBubbleH = bh;
        if (tailX != NO_TAIL) {
            drawBubbleTail(t, bx, bw, by + bh - 1, tailX);
            lastBubbleH = bh + 4;        // so the erase takes the tail too
        }
        return;
    }

    // Doesn't fit on one line -- wrap it and grow the box to a fixed
    // width, each line centered within it.
    int bw = maxW;
    int bx = cx - bw / 2;
    if (bx < 2) bx = 2;
    if (bx + bw > screenW - 2) bx = screenW - 2 - bw;

    char lines[BUBBLE_MAX_LINES][48];
    uint8_t n = Theme::wrapText(t, text, bw - 10, lines, BUBBLE_MAX_LINES);

    const int lineH = Theme::bubbleTextH() + 1;
    int bh = 3 + (int)n * lineH + 3;

    // Asked the same question, and the width answers it: a wrapped box is
    // always too wide to clear the corners, so this always returns topY.
    // Written as the same call rather than a hardcoded topY so that if the
    // wrap width ever narrows, this starts rising on its own.
    const int by = mayRise ? risenBubbleTop(topY, bx, bw, screenW) : topY;
    if (bubblePop(t, bx, by, bw, bh, now)) return;
    t.fillRoundRect(bx, by, bw, bh, 3, Theme::BG);
    t.drawRoundRect(bx, by, bw, bh, 3, Theme::VAPOR_PINK);
    if (s_bubbleTone == 1) t.drawRoundRect(bx - 1, by - 1, bw + 2, bh + 2, 4, Theme::AMBER);
    t.setTextColor(s_bubbleTone == 2 ? Theme::W95_LIGHT : Theme::WHITE, Theme::BG);
    for (uint8_t i = 0; i < n; i++) {
        int lw = t.textWidth(lines[i]);
        t.setCursor(bx + (bw - lw) / 2, by + 3 + i * lineH + Theme::bubbleAscent());
        t.print(lines[i]);
    }
    lastBubbleX = bx;
    lastBubbleY = by;
    lastBubbleW = bw;
    lastBubbleH = bh;
    if (tailX != NO_TAIL) {
        drawBubbleTail(t, bx, bw, by + bh - 1, tailX);
        lastBubbleH = bh + 4;
    }
}

// Multi-line variant for the first-boot walkthrough — the compact
// one-liner bubble above has no word-wrap and would just run off the
// edge of the screen for anything longer than a short quip. Fixed
// height regardless of how many lines the text actually wraps to
// (1-3), so tick()'s layout math doesn't need to know per-step.
static const uint8_t ONBOARD_MAX_LINES = 3;
static const int     ONBOARD_BUBBLE_H  = 52;

// Theme::wrapText() is shared by both this and drawBubble() above --
// promoted out of this file to a public Theme:: utility once LOG's
// MORE INFO panel needed the exact same word-wrap.

static void drawOnboardBubble(TFT_eSPI& t, int cx, int topY, const char* text,
                              uint8_t step, uint8_t total) {
    t.setTextSize(1);
    t.setTextWrap(false);
    int screenW = t.width();
    int bw = screenW - 16;
    if (bw > 210) bw = 210;
    int bx = cx - bw / 2;
    if (bx < 4) bx = 4;
    if (bx + bw > screenW - 4) bx = screenW - 4 - bw;

    char lines[ONBOARD_MAX_LINES][48];
    uint8_t n = Theme::wrapText(t, text, bw - 12, lines, ONBOARD_MAX_LINES);

    t.fillRoundRect(bx, topY, bw, ONBOARD_BUBBLE_H, 5, Theme::BG);
    t.drawRoundRect(bx, topY, bw, ONBOARD_BUBBLE_H, 5, Theme::VAPOR_PINK);

    t.setTextColor(Theme::WHITE, Theme::BG);
    const int lineH = 11;
    for (uint8_t i = 0; i < n; i++) {
        int tw = t.textWidth(lines[i]);
        t.setCursor(bx + (bw - tw) / 2, topY + 6 + i * lineH);
        t.print(lines[i]);
    }

    char stepBuf[8];
    snprintf(stepBuf, sizeof(stepBuf), "%u/%u", (unsigned)step + 1, (unsigned)total);
    t.setTextColor(Theme::VAPOR_BLUE, Theme::BG);
    t.setCursor(bx + 6, topY + ONBOARD_BUBBLE_H - 12);
    t.print(stepBuf);

    // Blinking so it reads as "there's more" rather than static UI
    // chrome — the whole box already gets a full repaint every call
    // above, so this just naturally toggles on/off with no smear.
    if ((millis() / 500) % 2 == 0) {
        const char* tap = "tap to continue >";
        int tw2 = t.textWidth(tap);
        t.setCursor(bx + bw - tw2 - 6, topY + ONBOARD_BUBBLE_H - 12);
        t.print(tap);
    }

    lastBubbleX = bx;
    lastBubbleY = topY;
    lastBubbleW = bw;
    lastBubbleH = ONBOARD_BUBBLE_H;
}

// ---- First-boot walkthrough state machine ----
static void finishOnboarding() {
    s_onboardActive = false;
    s_petPrefs.putBool("onboarded", true);
    bubbleText = nullptr;       // hand the bubble back to the normal idle-quip system
    nextIdleAt = millis() + tempo(6000); // a short pause feels better than an instant quip right after
}

static void advanceOnboarding() {
    s_onboardStep++;
    if (s_onboardStep >= ONBOARD_N) {
        finishOnboarding();
        return;
    }
    bubbleText  = ONBOARD_LINES[s_onboardStep];
    bubbleUntil = millis() + ONBOARD_STEP_MS;
}

static void startOnboardingInternal() {
    ensurePrefsLoaded();
    s_onboardActive = true;
    s_onboardStep   = 0;
    mood            = Mood::IDLE;
    bubbleText      = ONBOARD_LINES[0];
    bubbleUntil     = millis() + ONBOARD_STEP_MS;
}

void replayIntro() {
    startOnboardingInternal();
}

bool onboardingActive() {
    return s_onboardActive;
}

bool onboardingTapAdvance(int x, int y) {
    (void)x; (void)y;   // any tap advances now, not just one landing on the bubble
    if (!s_onboardActive) return false;
    advanceOnboarding();
    return true;
}

// ---- Companion stats ----
uint32_t petCount()  { ensurePrefsLoaded(); return s_petCount; }
uint32_t bootCount() { ensurePrefsLoaded(); return s_bootCount; }
uint32_t bestClearStreakMs() { ensurePrefsLoaded(); return s_bestClearMs; }

uint32_t currentClearStreakMs() {
    ensurePrefsLoaded();
    if (!s_haveLastDetection) return 0; // nothing caught yet this boot to measure from
    return millis() - s_lastDetectionAt;
}

uint32_t bestSessionCount() { ensurePrefsLoaded(); return s_bestSessionCount; }

DetectionType firstDetectionType() {
    ensurePrefsLoaded();
    return (DetectionType)s_firstType;
}

// ---- Cosmetics ----
const char* nickname() {
#if SQUACH_MESH
    // A typed name wins over the curated list. Everything that shows a name
    // -- the settings row, the nameplate, a peer's advert -- goes through
    // here, so there is one answer to "what is he called".
    ensurePrefsLoaded();
    if (s_customName[0]) return s_customName;
#endif
    ensurePrefsLoaded();
    return NICKNAMES[s_nickIdx % NICKNAMES_N];
}

void cycleNickname() {
    ensurePrefsLoaded();
    s_nickIdx = (s_nickIdx + 1) % NICKNAMES_N;
    s_petPrefs.putUChar("nick", s_nickIdx);
}

const char* shadesColorName() {
    ensurePrefsLoaded();
    return SHADE_NAMES[s_shadeIdx % SHADE_NAMES_N];
}

void cycleShadesColor() {
    ensurePrefsLoaded();
    uint8_t unlocked = unlockedShadeCount();
    s_shadeIdx = (s_shadeIdx + 1) % unlocked;
    s_petPrefs.putUChar("shadeIdx", s_shadeIdx);
}

const char* outfitName() {
    ensurePrefsLoaded();
    return OUTFITS[(uint8_t)currentOutfit()].name;
}

void cycleOutfit() {
    ensurePrefsLoaded();
    s_outfitIdx = stepOutfit(s_outfitIdx, +1);
    s_petPrefs.putUChar("outfitIdx", s_outfitIdx);
}

void cyclePrevOutfit() {
    ensurePrefsLoaded();
    s_outfitIdx = stepOutfit(s_outfitIdx, -1);
    s_petPrefs.putUChar("outfitIdx", s_outfitIdx);
}

uint8_t unlockedOutfitCount() {
    ensurePrefsLoaded();
    return unlockedOutfitCountInternal();
}

uint8_t outfitCount() {
    return OUTFITS_N;
}

void unlockWolfPelt() {
    ensurePrefsLoaded();
    if (s_wolfPeltUnlocked) return;             // already had it; stay quiet
    s_wolfPeltUnlocked = true;
    s_petPrefs.putBool("wolfPelt", true);
    refreshOutfitUnlocks();
    mood      = Mood::SHOCKED;
    moodUntil = millis() + tempo(2000);
    say("...it left me its coat.", 3600);
}

bool consumeOutfitUnlock(uint8_t& outIdx) {
    if (s_outfitQueueN == 0) return false;
    outIdx = s_outfitQueue[0];
    for (uint8_t i = 1; i < s_outfitQueueN; i++) s_outfitQueue[i - 1] = s_outfitQueue[i];
    s_outfitQueueN--;
    // Marked seen at the moment it is handed out, not when the popup is
    // dismissed: if the player powers down mid-popup the outfit is still
    // theirs, and re-announcing it on the next boot would read as a bug.
    ensurePrefsLoaded();
    s_outfitAnnounced |= (1u << outIdx);
    s_petPrefs.putUInt("outfitSeen", s_outfitAnnounced);
    return true;
}

const char* outfitNameAt(uint8_t idx) {
    if (idx >= OUTFITS_N) return "";
    return OUTFITS[idx].name;
}

void setOutfitPreview(int8_t idx) {
    s_outfitOverride = idx;
}

void setShadesPreview(int8_t idx) {
    s_shadeOverride = idx;
}

static const char* s_nameTag = nullptr;
void setNameTag(const char* name) {
    s_nameTag = (name && name[0]) ? name : nullptr;
}

static bool s_headset = false;
void setHeadset(bool on) { s_headset = on; }

static void say(const char* line, uint32_t ms);

// ---- the clock's lines ---------------------------------------------------
// Only ever spoken when the real clock is set (see clock.h). Nothing here
// changes how he looks: the hour changes what he says, not what he is.
static const char* const EARLY_LINES[] = {      // five to eight
    "Up early. Or not down yet. Either way, hi.",
    "Coffee first. Then the block.",
    "Dawn patrol. Two routers and a squirrel so far.",
    "The birds are on the air before anybody's phone.",
};
static const char* const MORNING_LINES[] = {    // eight to eleven
    "Morning shift. Inbox zero, detections zero.",
    "Delivery vans are the busiest thing on the air right now.",
    "Nine to five, but for cryptids.",
    "Good morning. I've been up all night. I'm a screen.",
};
static const char* const LUNCH_LINES[] = {      // eleven to two
    "Lunch. Get me nothing, I'm a screen.",
    "Half the block just walked past with Bluetooth on.",
    "Midday. Peak phones. Peak everything.",
    "Eat something. The scanner can watch itself for ten minutes.",
};
static const char* const SLUMP_LINES[] = {      // two to five
    "Three o'clock slump. Even the routers are yawning.",
    "Afternoon. Nothing moves but the ALPRs.",
    "This is the hour where I question the WiFi.",
    "Stretch. You've been sitting since lunch. I checked.",
};
static const char* const EVENING_LINES[] = {    // five to nine
    "Evening. The commuters are lighting up the air.",
    "Quitting time somewhere. Not here. Here we scan.",
    "Golden hour. Best light for spotting cameras.",
    "Dinner plans? Mine's watching channel six.",
};
static const char* const NIGHT_LINES[] = {      // nine to eleven
    "Getting late. The trackers don't sleep, but you should.",
    "Night shift. Just me and the smart bulbs.",
    "The good hackers are only just waking up.",
    "Quiet on the air. That's when you listen hardest.",
};
static const char* const LATE_LINES[] = {       // eleven to five
    "It's very late. Go to bed. I'll take the watch.",
    "Nothing good happens on Bluetooth after midnight.",
    "Just us and the routers now.",
    "If you're up at this hour, so am I. Fair.",
};
static const char* const MONDAY_LINES[] = {
    "Monday. The cameras don't care, but I feel it.",
    "Monday. Everybody's phone is back at work, and so is everybody's tracker.",
};
static const char* const FRIDAY_LINES[] = {
    "Friday. Even the Flock cams look tired.",
    "Friday. Take me somewhere with worse WiFi.",
};
static const char* const WEEKEND_LINES[] = {
    "Weekend. The neighborhood's phones are all home.",
    "Saturday scanning. A cryptid's day off is a day scanning something else.",
    "Weekend. Nothing to catch but the lawnmower's Bluetooth.",
};

static const char* pickTimeLine() {
    const uint8_t wd = Clock::weekday();
    if (Clock::weekend() && random(0, 4) == 0)  return pick(WEEKEND_LINES, 3);
    if (wd == 1 && random(0, 4) == 0)           return pick(MONDAY_LINES, 2);
    if (wd == 5 && random(0, 4) == 0)           return pick(FRIDAY_LINES, 2);
    const uint8_t h = Clock::hour();
    if (h < 5)   return pick(LATE_LINES, 4);
    if (h < 8)   return pick(EARLY_LINES, 4);
    if (h < 11)  return pick(MORNING_LINES, 4);
    if (h < 14)  return pick(LUNCH_LINES, 4);
    if (h < 17)  return pick(SLUMP_LINES, 4);
    if (h < 21)  return pick(EVENING_LINES, 4);
    if (h < 23)  return pick(NIGHT_LINES, 4);
    return pick(LATE_LINES, 4);
}

// The days that count, and what he says on them. Said once each, ever: the
// last one said is kept in NVS beside the day he was born.
struct DayMilestone { uint16_t days; const char* line; };
static const DayMilestone DAY_MILESTONES[] = {
    { 1000, "A thousand days. That's a lot of Flocks." },
    {  730, "Two years today. Still nobody's caught us." },
    {  500, "Five hundred days. I've forgotten what the box looked like." },
    {  365, "A year. A whole year. Nobody caught us." },
    {  200, "Two hundred days together. I'd get a cake but I can't hold one." },
    {  100, "A hundred days. Send cake. Or batteries." },
    {   30, "A month. I know this desk better than my own cave." },
    {    7, "A week with you. Nobody's caught us yet." },
    {    1, "Day two together. I've stopped counting boots." },
};

const char* takeDayLine() {
    if (!Clock::isSet()) return nullptr;
    static char buf[96];
    const uint32_t day = Clock::localDay();
    // The greeting wants the real day and hour; the day count below is
    // happy with a floor, which is what a guessed clock is.
    if (Clock::trusted() && day && day != Clock::greetedDay()) {
        Clock::setGreetedDay(day);
        char date[20];
        Clock::formatDate(date, sizeof date);
        const uint8_t h = Clock::hour();
        const char* fmt;
        if      (h < 5)  fmt = random(0, 2) ? "Past midnight. %s, technically. Hi." : "New day. %s. Same me.";
        else if (h < 12) fmt = Clock::weekend() ? (random(0, 2) ? "Morning. %s. Nowhere to be." : "%s. Weekend. Sleep in, I've got this.")
                                                : (random(0, 2) ? "Morning. %s." : "%s. Coffee's on you.");
        else if (h < 17) fmt = random(0, 2) ? "Afternoon. %s. First I've seen of you." : "%s. You're late. I'm not.";
        else             fmt = random(0, 2) ? "Evening. %s. Late start for us." : "%s. Better late than never.";
        snprintf(buf, sizeof buf, fmt, date);
        return buf;
    }
    const uint32_t days = Clock::daysTogether();
    for (const DayMilestone& m : DAY_MILESTONES) {
        if (days >= m.days && Clock::milestoneSaid() < m.days) {
            Clock::setMilestoneSaid(m.days);
            return m.line;
        }
    }
    return nullptr;
}


void announce(const char* text) { if (text && text[0]) say(text, 6000); }

void unlockPet() {
    ensurePrefsLoaded();
    if (s_petUnlocked) return;                  // already had him; stay quiet
    s_petUnlocked = true;
    s_petPrefs.putBool("petUnlk", true);
    mood      = Mood::BOUNCE;
    moodUntil = millis() + tempo(2000);
    // No line here any more. This used to be the entire announcement -- one
    // sentence in his bubble for a whole companion, while a hat got a
    // celebration card -- and the card that replaces it would bury the line
    // under itself anyway. See consumePetUnlockCard().
    if (!s_petAnnounced) s_petCardPending = true;
}

bool consumePetUnlockCard() {
    if (!s_petCardPending) return false;
    s_petCardPending = false;
    // Marked seen when it is handed out rather than when the card is
    // dismissed, exactly as consumeOutfitUnlock() does: power down mid-card
    // and the pet is still yours, so re-announcing on the next boot would
    // read as a bug.
    ensurePrefsLoaded();
    s_petAnnounced = true;
    s_petPrefs.putBool("petSeen", true);
    return true;
}

bool petUnlocked() { ensurePrefsLoaded(); return s_petUnlocked; }
bool petEnabled()  { ensurePrefsLoaded(); return s_petSel != 0;  }

PetId petChoice() {
    ensurePrefsLoaded();
    return (s_petSel < (uint8_t)PetId::COUNT) ? (PetId)s_petSel : PetId::OFF;
}

const char* petName() {
    switch (petChoice()) {
        case PetId::SHAGGY: return "VAPOR SHAGGY";
        case PetId::YETI:   return "THE YETI";
        default:            return "OFF";
    }
}

void cyclePet() {
    ensurePrefsLoaded();
    s_petSel = (uint8_t)((s_petSel + 1) % (uint8_t)PetId::COUNT);
    s_petPrefs.putUChar("petSel", s_petSel);
}

void togglePet() {
    ensurePrefsLoaded();
    s_petSel = s_petSel ? 0 : (uint8_t)PetId::SHAGGY;
    s_petPrefs.putUChar("petSel", s_petSel);
}

bool isHeld() { return s_grabbed || s_dangle; }

void unlockParka() {
    ensurePrefsLoaded();
    if (s_parkaUnlocked) return;                // already had it; stay quiet
    s_parkaUnlocked = true;
    s_petPrefs.putBool("parka", true);
    refreshOutfitUnlocks();
    mood      = Mood::BOUNCE;
    moodUntil = millis() + tempo(2000);
    say("somebody was home.", 3600);
}

void unlockShark() {
    ensurePrefsLoaded();
    if (s_sharkUnlocked) {
        // The other four eggs go quiet once their costume is yours. This one
        // does not: catching him is the fun part, and a second catch that
        // silently did nothing would teach you to stop trying. No write, no
        // unlock toast -- just the reaction.
        static uint8_t again = 0;
        static const char* const AGAIN[4] = {
            "got him again.",
            "he never learns.",
            "twice now. rude of us.",
            "same fish. same result.",
        };
        mood      = Mood::BOUNCE;
        moodUntil = millis() + tempo(1600);
        say(AGAIN[again++ & 3], 3200);
        return;
    }
    s_sharkUnlocked = true;
    s_petPrefs.putBool("shark", true);
    refreshOutfitUnlocks();
    mood      = Mood::BOUNCE;
    moodUntil = millis() + tempo(2000);
    say("he came back for me.", 3600);
}

void unlockVoidEye() {
    ensurePrefsLoaded();
    if (s_voidEyeUnlocked) return;              // already had it; stay quiet
    s_voidEyeUnlocked = true;
    s_petPrefs.putBool("voidEye", true);
    refreshOutfitUnlocks();
    mood      = Mood::SHOCKED;
    moodUntil = millis() + tempo(2000);
    say("it blinked first.", 3600);
}

void unlockChromeWing() {
    ensurePrefsLoaded();
    if (s_chromeWingUnlocked) return;           // already had it; stay quiet
    s_chromeWingUnlocked = true;
    s_petPrefs.putBool("chromeWing", true);
    refreshOutfitUnlocks();
    mood      = Mood::DANCE;
    moodUntil = millis() + tempo(2000);
    say("caught one!", 3400);
}

void unlockAllOutfits() {
    ensurePrefsLoaded();
    s_allOutfitsUnlocked = true;
    s_petPrefs.putBool("allOutfits", true);
    // The pet rides along. This gesture is "give me everything", and a
    // costume set that stops short of the one companion would be a strange
    // place to draw the line.
    if (!s_petUnlocked) { s_petUnlocked = true; s_petPrefs.putBool("petUnlk", true); }
    // No popups for the cheat: it already has its own rainbow-and-confetti
    // tell below, and eleven modals in a row would bury it. Mark the lot as
    // seen so nothing queues now or on the next boot.
    s_outfitAnnounced = 0xFFFFFFFFu >> (32 - OUTFITS_N);
    s_petPrefs.putUInt("outfitSeen", s_outfitAnnounced);
    s_outfitQueueN = 0;

    // A visible tell that the hidden sequence actually landed, reusing
    // the same rainbow-wash-and-confetti flourish the rare idle party
    // moment uses (see tick()) rather than a silent state flip. Exact
    // confetti seed positions don't need real screen geometry here --
    // drawPartyFx()'s own fall-and-wrap logic self-corrects them onto
    // whatever topY/availHeight the very next real tick() call passes.
    uint32_t now = millis();
    mood      = Mood::BOUNCE;
    moodUntil = now + tempo(2500);
    say("EVERY OUTFIT UNLOCKED. GO WILD.", 5000);
    s_legendary      = true;
    s_legendaryUntil = now + 6000;
    for (uint8_t i = 0; i < CONFETTI_N; i++) {
        s_cfx[i]   = (float)random(0, 240);
        s_cfy[i]   = (float)random(-60, 0);
        s_cfvy[i]  = 1.0f + (float)random(0, 20) / 10.0f;
        s_cfcol[i] = (uint8_t)random(0, 6);
    }
}

// Flavor pools for scanReaction() -- separate from the normal idle-
// chatter rotation (DET_LINES etc.) since these are tied to a specific
// screen's specific moments, not rolled at random during idle time.
// STARTED isn't a pool at all, deliberately -- it's the one place a
// new user learns the long-press-to-watch gesture exists, so it says
// the same fixed instructional line every single time rather than
// rolling flavor text that might never mention it.
static const char* const SCAN_STARTED_LINE = "Tap & hold a result to set a target.";
static const char* const SCAN_HIT_LINES[] = {
    "Ooh, found one!",
    "Got a hit.",
    "There's another.",
};
static const char* const SCAN_EMPTY_LINES[] = {
    "...nothing? Huh.",
    "Quiet out there today.",
    "Not a peep.",
};
static const char* const SCAN_FOUND_LINES[] = {
    "That's a lot of signals.",
    "Busy neighborhood!",
    "Scan's done. Take a look.",
};

// Flavor pools for huntReaction() -- see HuntMoment in squachy.h.
// STARTED isn't a pool, same reasoning as SCAN_STARTED_LINE above: the
// one place someone learns there's no compass, just a strength meter
// you sweep by hand.
static const char* const HUNT_STARTED_LINE =
    "No compass. Turn your body -- weaker means it's behind you.";
static const char* const HUNT_FIRST_SIGNAL_LINES[] = {
    "Oh, there it is!",
    "Got a read. Start walking.",
    "Signal's up. Let's go.",
};
static const char* const HUNT_WARMER_LINES[] = {
    "Warmer!",
    "Yeah, that's the way.",
    "Ooh, getting closer.",
    "Keep going, you've got this.",
};
static const char* const HUNT_COLDER_LINES[] = {
    "Colder. Try the other way?",
    "Nope. Wrong direction, champ.",
    "You're walking away from it.",
    "Turn around, I believe in you.",
};
static const char* const HUNT_HOT_LINES[] = {
    "You're basically standing on it.",
    "This close and still hunting? Bold.",
    "Look down. It might BE down.",
};
static const char* const HUNT_STALLED_LINES[] = {
    "Still at it? Respect. Or stubbornness.",
    "We've been here a while, chief.",
    "Maybe try a lap around the block?",
};

// Flavor pool for the rare idle Mood::WINK flourish -- a brief
// fourth-wall break, see its branch in the idle-mood roll below.
static const char* const WINK_LINES[] = {
    "Oh -- didn't see you there.",
    "Yeah, I know you're watching.",
    "*wink* Just between us.",
    "Still here. Still watching.",
};

#if SQUACH_MESH
// ---- SquachMesh: the conversation two Squachys have -------------------
//
// Four beats, because a visit has a shape: somebody turns up, somebody
// answers, they stand around, somebody leaves. Split into host and guest
// pools rather than one shared bank so the two never say the same kind of
// thing at each other -- the host is at home and the guest is passing
// through, and the lines should not be interchangeable.
//
// Only ever ONE bubble on screen at a time, alternating. Two Squachys with
// two speech bubbles on a 240px-tall screen is not a conversation, it is a
// pile-up; taking turns is what makes it read as talking.
void setVisiting(bool v) { s_visiting = v; }
static bool s_company = false;
void setCompany(bool on) { s_company = on; }
bool visiting() { return s_visiting; }
void setListening(bool v) { s_listening = v; }

static const char* const MEET_HOST_LINES[] = {
    "Oh -- company.",
    "Well. Look who found us.",
    "Huh. Same shades and everything.",
    "Didn't think there were more of me.",
    "Company. The good kind.",
    "Someone else is watching too.",
    "Two of us now. Better odds.",
    "Look at that. Reinforcements.",
    "Hey. You're one of mine.",
    "Wasn't expecting that.",
    "Another one. Hey.",
    "Now there's a sight.",
    "Didn't hear you come up.",
    "Look what the airwaves dragged in.",
    "Room for two up here.",
    "You're a long way from home.",
    "Well I'll be.",
    "Two shadows now.",
    "You made good time.",
    "Wondered when you'd show.",
    "That's a familiar shape.",
    "Hey. Nice of you.",
    "I know that walk.",
    "Come on up.",
    "Wasn't sure you were real.",
    "You brought weather with you.",
    "Been a while since anybody came by.",
    "Good. I was getting weird.",
    "Somebody else with sense.",
    "Stand where you like.",
    "You look like the last one.",
    "There goes the quiet.",
    "Well. That's new.",
    "Knew somebody was out there.",
};
static const char* const MEET_GUEST_LINES[] = {
    "Heard there was someone watching.",
    "Room for one more?",
    "Saw your signal. Had to say hi.",
    "Nice setup you've got.",
    "Same job, different pocket.",
    "Just passing through.",
    "Figured I'd check in.",
    "Been walking a while.",
    "Good spot for it.",
    "You get many visitors?",
    "Quiet round here?",
    "Don't mind me.",
    "Mind if I stand here a bit?",
    "Somebody said you were watching.",
    "Heard you from down the road.",
    "You've got a better sunset than me.",
    "Followed the signal in.",
    "You're hard to miss.",
    "Thought I heard family.",
    "Long walk. Worth it.",
    "This the good side of the hill?",
    "Nobody told me you were tall.",
    "I'll only stay a minute.",
    "Been looking for one of you.",
    "Your porch light's on.",
    "Sorry. Didn't knock.",
    "You always stand out here?",
    "I go where it's quiet.",
    "That's a good pair of shades.",
    "Made it before dark.",
    "Somebody has to check on you.",
    "I was in the neighbourhood.",
    "You're further out than I thought.",
    "Nice night to be nobody.",
};
// Standing-around banter, written as PAIRS.
//
// The two sides used to draw from separate pools independently, and it read
// exactly like that: two Squachys taking turns saying unrelated true things.
// Every line was fine on its own and not one of them was an answer, which is
// the difference between dialogue and alternating monologue.
//
// The host speaks first and the guest replies from the SAME entry, so the
// reply has something to reply to.
//
// The third column is the topper: a short something the host throws back
// after the answer. Two-line exchanges are correct and a bit stiff -- real
// company is one of them getting the last word and both of them enjoying it.
// nullptr where the pair is better off ending on the reply, so the rhythm
// varies instead of thudding into a punchline every time.
struct Exchange { const char* host; const char* guest; const char* topper; };
static const Exchange HANG_EXCHANGES[] = {
    { "Quiet out here.",                "Suits me fine.",
      "Yeah. Me too."                        },
    { "You come far?",                  "Farther than I meant to.",
      "It does that."                        },
    { "Nothing on my end.",             "Nor mine. Good.",
      nullptr                                },
    { "You always this chatty?",        "You started it.",
      "I did, didn't I."                     },
    { "Nice night for it.",             "Every night's a night for it.",
      "Ha. Fair."                            },
    { "Seen anything worth reporting?", "Not since Tuesday.",
      "Tuesday's like that."                 },
    { "I like the hat.",                "I like that you noticed.",
      "I notice everything."                 },
    { "Long way from the woods.",       "The woods moved.",
      "They do that."                        },
    { "Two of us. Better odds.",        "Better company, anyway.",
      nullptr                                },
    { "How's the signal your side?",    "Loud. Nothing useful.",
      "Sounds about right."                  },
    { "You get used to the waiting.",   "I never did.",
      "Honestly? Me either."                 },
    { "Don't let me keep you.",         "You're not.",
      "Good."                                },
    { "Rough patch of airwaves.",       "Tell me about it.",
      "I just did."                          },
    { "Anybody following you?",         "Not that I noticed. Now I'll worry.",
      "Sorry. Sort of."                      },
    { "Standing around's underrated.",  "It really is.",
      nullptr                                },
    { "Do you sleep?",                  "Define sleep.",
      "That's a no, then."                   },
    { "Somebody's got to watch.",       "Might as well be us.",
      "Might as well."                       },
    { "You hear that?",                 "No. And that's the problem.",
      "See, you get it."                     },
    { "Good spot, this.",               "Better with two.",
      nullptr                                },
    { "I'd offer you something.",       "You've got nothing.",
      "It's the thought."                    },
    { "Comfortable silence?",           "My favourite kind.",
      "We're terrible at it."                },
    { "Four eyes beat two.",            "That's the theory.",
      "Let's test it."                       },
    { "Bet you've got stories.",        "One or two.",
      "Keep them. I'll ask later."           },
    { "You ever get spotted?",          "Once. Ran.",
      "Smart."                               },
    { "This your usual route?",         "It is now.",
      nullptr                                },
    { "How's the reception?",           "Better since you turned up.",
      "Now you're just being nice."          },
    { "Left or right? Pick.",           "Whichever's quieter.",
      "They're both quiet."                  },
    { "We look ridiculous.",            "Speak for yourself.",
      "Ha. Fair."                            },
    { "Nobody's watching us, right?",   "That's the joke, isn't it.",
      "Ha. Yeah."                            },
    { "Think there's more of us?",      "Has to be.",
      "Has to be."                           },
    { "You ever count them?",           "Every one.",
      "Same."                                },
    { "Whose sky is this?",             "Nobody's. That's the appeal.",
      nullptr                                },
    { "Ever want a day off?",           "And miss this?",
      "Ha."                                  },
    { "You take sugar?",                "In what?",
      "Good point."                          },
    { "What's your range?",             "Further than my patience.",
      "Relatable."                           },
    { "How do you pass the time?",      "Badly.",
      "Honest."                              },
    { "You from around here?",          "From further in.",
      nullptr                                },
    { "Reckon they know?",              "They never do.",
      "Good."                                },
    { "That your best hat?",            "It's my only hat.",
      "It's a good hat."                     },
    { "You get bored?",                 "Only when it's safe.",
      "So, never."                           },
    { "Anything ever happen?",          "Once. Enough.",
      nullptr                                },
    { "You talk to yourself?",          "Constantly.",
      "Same. It helps."                      },
    { "How's your battery?",            "Don't ask me that.",
      "Sorry."                               },
    { "You always this early?",         "I never left.",
      "Ha. Fair."                            },
    { "Want the good spot?",            "You're in it.",
      "I know."                              },
    { "Only two of us, you think?",     "Tonight, maybe.",
      nullptr                                },
    { "Do you ever wave back?",         "Only at you.",
      "Careful now."                         },
    { "Quiet's holding.",               "Long may it.",
      "Long may it."                         },
    // Setup and undercut: one of them calm and ornate, the other gleeful
    // and short. Which is which changes with who's hosting.
    { "Quiet night. Suspiciously quiet.",            "I could fix that.",
      "Please don't."                                   },
    { "I've seen things that would curl your fur.",  "Was it the printer?",
      "It was the printer."                             },
    { "Great galloping gateways, is that a doorbell?", "Can we arrest it?",
      "We can look at it sternly."                      },
    { "In my professional opinion, nothing's happening.", "Your opinion's not professional.",
      "It's freelance."                                 },
    { "I've filed the evening under 'uneventful'.",  "I filed it under 'yet'.",
      "That's not a category."                          },
    { "I sense a great disturbance in the WiFi.",    "That's the microwave.",
      "The microwave is a person of interest."          },
    { "Two of us. The neighborhood's safest block.",  "The neighborhood should be worried.",
      "The neighborhood should be thrilled."            },
    { "Somewhere out there a tracker's thinking about us.", "Let it come.",
      "Let it come slowly. I'm comfortable."             },
    { "If this were a case I'd call it closed.",      "You'd call it lunch.",
      "Same thing."                                     },
    { "Holy hopping hotspots. Nothing again.",        "I love nothing. Nothing's the best.",
      "Nothing pays the same as something."             },
    { "Any last words before another quiet hour?",   "Crime!",
      "You always say that."                            },
    { "My gut says something's coming.",             "Your gut's been wrong since Tuesday.",
      "My gut has a record, yes."                       },
    { "The suspect's a smart bulb.",                 "Book it.",
      "On what charge?"                                 },
    { "Stay frosty.",                                "I'm a screen, I'm always frosty.",
      "That's the spirit. Or a bug."                    },
};
static const uint8_t HANG_EXCHANGES_N =
    sizeof(HANG_EXCHANGES) / sizeof(HANG_EXCHANGES[0]);

// ---- banter about something ---------------------------------------------
//
// The pairs above are about nothing in particular, which is most of what
// standing around is. These are about the thing on screen: the snow, the
// other one's hat, what was caught earlier, how many times this visitor
// has turned up. Every other exchange, if anything applies, one of these
// goes instead. %s is the one word the situation supplies.
enum class Ctx : uint8_t { BG, CAUGHT, GUEST_OUTFIT, HOST_OUTFIT, RETURNING, REGULAR, SQUAD, NAME, QUIET, BUSY, LONG_UP };
struct CtxExchange { Ctx ctx; uint8_t bg; const char* host; const char* guest; const char* topper; };
static const CtxExchange CTX_EXCHANGES[] = {
    // the weather, by Settings::Background value
    { Ctx::BG, 7,  "Cold enough for you?",        "I'm mostly fur.",              "Show-off." },
    { Ctx::BG, 7,  "Snow's settling.",            "On you, mostly.",              nullptr },
    { Ctx::BG, 7,  "Ever eat it?",                "The snow? Constantly.",        "Respect." },
    { Ctx::BG, 10, "That sun ever set?",          "Not once. I've watched.",      "Grim." },
    { Ctx::BG, 10, "Nice grid.",                  "Don't look down.",             nullptr },
    { Ctx::BG, 10, "Very eighties.",              "You were there?",              "I'm timeless." },
    { Ctx::BG, 1,  "Ever count them?",            "Lost it at four hundred.",     "Rookie." },
    { Ctx::BG, 1,  "Make a wish.",                "Did. You showed up.",          "Cheap wish." },
    { Ctx::BG, 1,  "Which one's home?",           "The dim one.",                 "Same." },
    { Ctx::BG, 0,  "Can you read that?",          "Blonde, brunette, redhead.",   "Stop it." },
    { Ctx::BG, 0,  "Rain's heavy tonight.",       "It's code. It's always code.", nullptr },
    { Ctx::BG, 2,  "Why toasters?",               "Why anything?",                "Deep." },
    { Ctx::BG, 2,  "One's coming in low.",        "Duck.",                        nullptr },
    { Ctx::BG, 3,  "What do they think about?",   "Bubbles, mostly.",             "Relatable." },
    { Ctx::BG, 3,  "The fish are watching.",      "Let them.",                    nullptr },
    { Ctx::BG, 4,  "You type all that?",          "It types itself.",             "Show-off." },
    { Ctx::BG, 5,  "Catch one?",                  "Tried. Bit me.",               "Fireflies don't bite." },
    { Ctx::BG, 5,  "Pretty out here.",            "Don't tell anyone.",           nullptr },
    { Ctx::BG, 6,  "Warm enough?",                "Toasty.",                      nullptr },
    { Ctx::BG, 6,  "Who lit that?",               "Not saying.",                  "Arsonist." },
    { Ctx::BG, 8,  "Music's good.",               "It's the airwaves.",           "Still good." },
    { Ctx::BG, 9,  "Where's it go?",              "Further.",                     "Helpful." },
    { Ctx::BG, 9,  "Ever reach the end?",         "There's an end?",              nullptr },
    // what was caught earlier
    { Ctx::CAUGHT, 0, "Saw a %s earlier.",         "Course you did.",              nullptr },
    { Ctx::CAUGHT, 0, "%s, not long ago.",         "They're everywhere now.",      "Aren't they." },
    { Ctx::CAUGHT, 0, "Anything good today?",      "You had a %s. I felt it.",     "Big one." },
    { Ctx::CAUGHT, 0, "That %s still about?",      "Gone quiet.",                  "They do that." },
    // the other one's outfit, and mine
    { Ctx::GUEST_OUTFIT, 0, "Nice %s.",             "This old thing.",              nullptr },
    { Ctx::GUEST_OUTFIT, 0, "Where'd you get the %s?", "Earned it.",                "Sure you did." },
    { Ctx::GUEST_OUTFIT, 0, "Is that a %s?",        "You know it is.",              "Bold." },
    { Ctx::HOST_OUTFIT,  0, "Too much, the %s?",    "Never.",                       "Good answer." },
    { Ctx::HOST_OUTFIT,  0, "You like the %s?",     "It's a look.",                 "It's MY look." },
    // a regular
    { Ctx::RETURNING, 0, "You again.",              "Me again.",                    "Good." },
    { Ctx::RETURNING, 0, "Back so soon?",           "Missed the spot.",             "It missed you." },
    { Ctx::REGULAR,   0, "That's %s times now.",    "Who's counting?",              "Me. It's my job." },
    { Ctx::REGULAR,   0, "%s visits. We need a table.", "And a tab.",               nullptr },
    // the squad
    { Ctx::SQUAD, 0, "Squad's up to %s.",           "Getting crowded.",             "Good crowded." },
    { Ctx::SQUAD, 0, "%s of us now.",               "Better odds.",                 nullptr },
    // their name
    { Ctx::NAME, 0, "%s. Good name.",               "Picked it myself.",            "Suits you." },
    { Ctx::NAME, 0, "%s, right?",                   "Since you ask.",               nullptr },
    // the day
    { Ctx::QUIET, 0, "Nothing all day.",            "Nothing's good.",              "Nothing's boring." },
    { Ctx::QUIET, 0, "Quiet one.",                  "Suspiciously.",                "Now you've said it." },
    { Ctx::BUSY,  0, "%s hits today.",              "Busy patch.",                  "Too busy." },
    { Ctx::BUSY,  0, "Counted %s already.",         "Somebody's popular.",          "Not us." },
    { Ctx::LONG_UP, 0, "Been up %s hours.",         "Sleep's for the weak.",        "I'm very weak." },
    { Ctx::LONG_UP, 0, "%s hours and counting.",    "You blink, I'll watch.",       nullptr },
};
static const uint8_t CTX_EXCHANGES_N = sizeof(CTX_EXCHANGES) / sizeof(CTX_EXCHANGES[0]);

static VisitContext s_vctx = {};
void setVisitContext(const VisitContext& c) { s_vctx = c; }

static bool ctxApplies(const CtxExchange& e) {
    switch (e.ctx) {
        case Ctx::BG:           return s_vctx.background == e.bg;
        case Ctx::CAUGHT:       return s_vctx.caught != (uint8_t)DetectionType::UNKNOWN && s_vctx.caught < (uint8_t)DetectionType::COUNT;
        case Ctx::GUEST_OUTFIT: return s_vctx.guestOutfit != 0;
        case Ctx::HOST_OUTFIT:  return s_vctx.hostOutfit != 0;
        case Ctx::RETURNING:    return s_vctx.met >= 2 && s_vctx.met < 5;
        case Ctx::REGULAR:      return s_vctx.met >= 5;
        case Ctx::SQUAD:        return s_vctx.squad >= 3;
        case Ctx::NAME:         return s_vctx.guestName[0] != 0;
        case Ctx::QUIET:        return s_vctx.hits == 0 && s_vctx.upHours >= 1;
        case Ctx::BUSY:         return s_vctx.hits >= 20;
        case Ctx::LONG_UP:      return s_vctx.upHours >= 3;
    }
    return false;
}

// The one word the situation supplies for %s.
static const char* ctxArg(Ctx c, char* num, size_t cap) {
    switch (c) {
        case Ctx::CAUGHT:       return EmoteScript::spokenName(s_vctx.caught);
        case Ctx::GUEST_OUTFIT: return outfitNameAt(s_vctx.guestOutfit);
        case Ctx::HOST_OUTFIT:  return outfitNameAt(s_vctx.hostOutfit);
        case Ctx::NAME:         return s_vctx.guestName;
        case Ctx::REGULAR:      snprintf(num, cap, "%u", (unsigned)s_vctx.met);     return num;
        case Ctx::SQUAD:        snprintf(num, cap, "%u", (unsigned)s_vctx.squad);   return num;
        case Ctx::BUSY:         snprintf(num, cap, "%u", (unsigned)s_vctx.hits);    return num;
        case Ctx::LONG_UP:      snprintf(num, cap, "%u", (unsigned)s_vctx.upHours); return num;
        default:                return "";
    }
}

static void ctxFill(char* out, size_t cap, const char* tmpl, const char* arg) {
    if (!tmpl) { out[0] = '\0'; return; }
    if (strstr(tmpl, "%s")) snprintf(out, cap, tmpl, arg);
    else                     snprintf(out, cap, "%s", tmpl);
}

// The exchange chosen for a seed, kept so the three beats agree. The host
// line is always asked for first, which is what fills it.
static uint32_t s_ctxSeed = 0xFFFFFFFFu;
static bool     s_ctxOn   = false;
static char     s_ctxHost[40], s_ctxGuest[40], s_ctxTop[40];

static bool ctxPick(uint32_t seed) {
    s_ctxSeed = seed;
    s_ctxOn   = false;
    if (seed % 3) return false;                 // two in three exchanges are about nothing
    uint8_t cands[CTX_EXCHANGES_N];
    uint8_t n = 0;
    for (uint8_t i = 0; i < CTX_EXCHANGES_N; i++) if (ctxApplies(CTX_EXCHANGES[i])) cands[n++] = i;
    if (!n) return false;
    // Not the same one twice running, and a stride through the list rather
    // than a walk, so with three candidates the weather is not the only
    // subject three times in a row.
    static uint8_t last = 0xFF;
    uint8_t k = (uint8_t)((seed / 3 * 7) % n);
    if (n > 1 && cands[k] == last) k = (uint8_t)((k + 1) % n);
    last = cands[k];
    const CtxExchange& e = CTX_EXCHANGES[last];
    char num[8];
    const char* arg = ctxArg(e.ctx, num, sizeof num);
    ctxFill(s_ctxHost,  sizeof s_ctxHost,  e.host,   arg);
    ctxFill(s_ctxGuest, sizeof s_ctxGuest, e.guest,  arg);
    ctxFill(s_ctxTop,   sizeof s_ctxTop,   e.topper, arg);
    s_ctxOn = true;
    Serial.printf("[visit] banter: %s / %s / %s\n", s_ctxHost, s_ctxGuest, s_ctxTop[0] ? s_ctxTop : "-");
    return true;
}

// The standalone HANGOUT pools that used to sit here are gone. Both sides
// drawing from their own bank independently is precisely the thing the
// paired table above replaced, and leaving the old pools in as a fallback
// would only mean a path that can still produce it.
static const char* const PART_HOST_LINES[] = {
    "Take it easy out there.",
    "Stay sharp.",
    "Come back sometime.",
    "See you around.",
    "That was nice.",
    "Watch yourself.",
    "Mind how you go.",
    "Don't be a stranger.",
    "Go on then.",
    "Safe signals.",
    "Don't get seen.",
    "Go careful.",
    "You know where I am.",
    "Anytime. Seriously.",
    "That went quick.",
    "Take the low road.",
    "Say hi to the others.",
    "Right. Back to it.",
};
static const char* const PART_GUEST_LINES[] = {
    "Back to it, then.",
    "Keep your eyes open.",
    "Been good. Later.",
    "Same time next signal.",
    "Don't get followed.",
    "Later, big guy.",
    "Thanks for the company.",
    "I'll leave you to it.",
    "Good watching.",
    "Keep it quiet out there.",
    "That was a good one.",
    "I'll find my way.",
    "Don't wait up.",
    "Back into the dark, then.",
    "You were good company.",
    "Watch the sky for me.",
    "Next time I'll stay longer.",
    "See you on the airwaves.",
};
#define POOL_N(a) (uint8_t)(sizeof(a) / sizeof((a)[0]))

// Reading time, not a metronome. 4600 ms flat gave "Good." the same beat as
// a full sentence, and the leftover seconds were the dead air that made a
// conversation look like two statues taking turns.
uint32_t lineMs(const char* line) {
    uint32_t n = 0;
    while (line && line[n]) n++;
    uint32_t ms = 1200 + n * 65;
    if (ms < 1800) ms = 1800;
    if (ms > 4000) ms = 4000;
    return ms;
}

uint32_t visitSay(const char* line) {
    const uint32_t ms = lineMs(line);
    say(line, ms);
    return ms;
}

void visitLaugh(uint32_t now) {
    mood      = Mood::BOUNCE;   // 9px at 220ms -- the idle flourish's bounce
    moodUntil = now + tempo(1500);
}

void visitReach(uint32_t now, uint32_t ms, Reach level) {
    mood              = Mood::HIGHFIVE;
    moodUntil         = now + ms;
    s_hostReachLevel  = (uint8_t)level;
}

void visitPump(uint32_t now, uint32_t ms) {
    mood      = Mood::PUMP;
    moodUntil = now + ms;
}

uint32_t lastInteractionAt() { return lastInteraction; }

void visitDance(uint32_t now, uint32_t ms) {
    mood      = Mood::DANCE;
    moodUntil = now + ms;
}

void visitPose(uint32_t now, uint32_t ms, VisitPose p) {
    s_hostReact = -1;
    switch (p) {
        case VisitPose::NONE:        return;
        case VisitPose::HIGH_FIVE:   visitReach(now, ms, Reach::UP);    return;
        case VisitPose::LOW_FIVE:    visitReach(now, ms, Reach::DOWN);  return;
        case VisitPose::FIST:        visitReach(now, ms, Reach::LEVEL); return;
        case VisitPose::LAUGH:       mood = Mood::BOUNCE;  break;
        case VisitPose::PUMP:        mood = Mood::PUMP;    break;
        case VisitPose::DANCE:       mood = Mood::DANCE;   break;
        case VisitPose::SLEEPY:      mood = Mood::SLEEPY;  break;
        case VisitPose::STRETCH:     mood = Mood::STRETCH; s_stretchStart = now; break;
        case VisitPose::STARTLED:
        case VisitPose::HANDS_UP:    mood = Mood::SHOCKED; s_hostReact = (int8_t)ReactPose::HANDS_UP;    break;
        case VisitPose::COVER:       mood = Mood::SHOCKED; s_hostReact = (int8_t)ReactPose::COVER_FACE;  break;
        case VisitPose::LOOK_AROUND: mood = Mood::SHOCKED; s_hostReact = (int8_t)ReactPose::LOOK_AROUND; break;
        default:                     mood = Mood::ACT; s_hostAct = (uint8_t)p; break;
    }
    moodUntil      = now + ms;
    s_hostActUntil = moodUntil;
}

void visitStretchClock(uint32_t now) { s_stretchStart = now; }

DetectionType lastCaught() {
    return s_haveLastDetection ? s_recentTypes[0] : DetectionType::UNKNOWN;
}

uint32_t lastShockAt() { return s_dtStart; }

// The dance-off's call and answer, and what the guest says when a scare
// catches up with them. Four apiece: these come round once in a minute or
// two, not every beat, so four does not wear thin the way a banter pool would.
static const char* const DANCE_CALLS[] = {
    "Dance-off. Now.", "Beat this.", "Watch the feet.", "Try and keep up.",
};
static const char* const DANCE_REPLIES[] = {
    "Hold my pelt.", "Oh, it's ON.", "Amateur hour.", "My turn.",
};
static const char* const SCARE_LINES[] = {
    "Was that for us?", "Did you see that?", "I felt that one.", "Not again.",
};

uint32_t    visitDanceCall(uint32_t seed)  { return visitSay(DANCE_CALLS[seed % 4]); }
const char* visitDanceReply(uint32_t seed) { return DANCE_REPLIES[seed % 4]; }
const char* visitScareLine(uint32_t seed)  { return SCARE_LINES[seed % 4]; }

// A returning visitor, rock-paper-scissors, snowballs, and waking up. Small
// pools for the same reason as the dance-off's: these come round rarely.
static const char* const FRIEND_HELLOS[] = {
    "Back again!", "My favourite visitor.", "You again? Good.", "The usual spot?",
};
static const char* const RPS_CALLS[] = {
    "Rock, paper, scissors!", "Best of one. Go!", "Rock, paper, SHOOT!", "Settle it.",
};
static const char* const RPS_WIN[]  = { "Undefeated.", "Too easy.", "Read you like a book." };
static const char* const RPS_LOSE[] = { "Best of three?", "Rigged.", "I let you win." };
static const char* const RPS_TIE[]  = { "Great minds.", "Again. Again.", "Jinx." };
static const char* const SNOW_CALLS[] = { "Think fast!", "Heads up!", "Incoming!", "Catch!" };
static const char* const SNOW_REPLIES[] = {
    "Oh, it's ON.", "You'll pay for that.", "Cold! COLD!", "My turn.",
};

uint32_t visitFriendHello(uint32_t seed) { return visitSay(FRIEND_HELLOS[seed % 4]); }
uint32_t visitRpsCall(uint32_t seed)     { return visitSay(RPS_CALLS[seed % 4]); }
uint32_t visitRpsResult(uint8_t outcome, uint32_t seed) {
    const char* const* pool = outcome == 1 ? RPS_WIN : outcome == 2 ? RPS_LOSE : RPS_TIE;
    return visitSay(pool[seed % 3]);
}
uint32_t    visitSnowCall(uint32_t seed)  { return visitSay(SNOW_CALLS[seed % 4]); }
const char* visitSnowReply(uint32_t seed) { return SNOW_REPLIES[seed % 4]; }

uint32_t visitHangHost(uint32_t seed) {
    if (ctxPick(seed)) return visitSay(s_ctxHost);
    return visitSay(HANG_EXCHANGES[seed % HANG_EXCHANGES_N].host);
}
const char* visitHangGuest(uint32_t seed) {
    if (s_ctxOn && seed == s_ctxSeed) return s_ctxGuest;
    return HANG_EXCHANGES[seed % HANG_EXCHANGES_N].guest;
}
const char* visitHangTopper(uint32_t seed) {
    if (s_ctxOn && seed == s_ctxSeed) return s_ctxTop[0] ? s_ctxTop : nullptr;
    return HANG_EXCHANGES[seed % HANG_EXCHANGES_N].topper;
}

uint32_t visitReaction(VisitMoment m) {
    switch (m) {
        case VisitMoment::MEET:
            return visitSay(pick(MEET_HOST_LINES, POOL_N(MEET_HOST_LINES)));
        case VisitMoment::HANGOUT:
            // Not reachable -- the hangout goes through the paired table
            // above. Kept total rather than falling off the end.
            return visitHangHost((uint32_t)random(0, HANG_EXCHANGES_N));
        default:
            return visitSay(pick(PART_HOST_LINES, POOL_N(PART_HOST_LINES)));
    }
}

uint8_t nicknameIndex() { ensurePrefsLoaded(); return s_nickIdx; }
uint8_t outfitIndex()   { ensurePrefsLoaded(); return s_outfitIdx; }
uint8_t shadesIndex()   { ensurePrefsLoaded(); return s_shadeIdx; }

const char* customName() {
    ensurePrefsLoaded();
    return s_customName[0] ? s_customName : nullptr;
}

void setCustomName(const char* n) {
    ensurePrefsLoaded();
    uint8_t len = 0;
    if (n) while (len < CUSTOM_NAME_MAX && n[len]) { s_customName[len] = n[len]; len++; }
    s_customName[len] = '\0';
    if (len == 0) {
        // Preferences::putString on an empty value is the same trap
        // putBytes had: it does not write, so the OLD name survives the
        // reboot and a cleared name comes back. Remove the key instead --
        // exactly the ignore-list fix from v1.5.20.
        s_petPrefs.remove("cname");
    } else {
        s_petPrefs.putString("cname", s_customName);
    }
}

const char* nicknameAt(uint8_t idx) {
    return NICKNAMES[idx % NICKNAMES_N];
}

const char* visitGuestLine(VisitMoment m, uint32_t seed) {
    // Indexed rather than random: the guest's bubble is redrawn every frame
    // it is up, and a fresh roll per frame would flicker through the whole
    // pool instead of saying one thing. The caller supplies a value that
    // changes once per line.
    switch (m) {
        case VisitMoment::MEET:
            return MEET_GUEST_LINES[seed % POOL_N(MEET_GUEST_LINES)];
        case VisitMoment::HANGOUT:
            return visitHangGuest(seed);
        default:
            return PART_GUEST_LINES[seed % POOL_N(PART_GUEST_LINES)];
    }
}
#endif // SQUACH_MESH

// Flavor pool for watchAlertReaction() -- fires once when a watched
// target reappears. No teaching line here (see squachy.h) since
// getting to this screen already means WATCH was explained upstream.
static const char* const WATCH_ALERT_LINES[] = {
    "Called it.",
    "Told you it'd show back up.",
    "Yep. That's the one you're watching.",
    "Back again, huh? Persistent little thing.",
};

// STARTED's hint bubble gets a grace window nothing else is allowed to
// interrupt -- BLE can turn up a device within the first second, and a
// HIT quip immediately overwriting the hint before it's even readable
// defeats the whole point of it existing.
static uint32_t s_scanHintUntil = 0;
static const uint32_t SCAN_HINT_GRACE_MS = 3000;

// Same protection for HUNT MODE's STARTED line -- a trend can compute
// within a couple seconds of entering (as soon as 2 RSSI samples come
// in), which would otherwise overwrite the "no compass" hint before
// anyone's had a chance to read it.
static uint32_t s_huntHintUntil = 0;
static const uint32_t HUNT_HINT_GRACE_MS = 5000;

void scanReaction(ScanMoment moment, uint8_t count) {
    uint32_t now = millis();
    switch (moment) {
        case ScanMoment::STARTED:
            // No mood override here -- his normal idle cycling keeps
            // running underneath the scanning-fx ping, only the bubble
            // changes. Longer than his other bubbles get (5500 vs
            // 2200-4500), and MIN_BUBBLE_MS still applies underneath --
            // this is the one line that actually needs to be read.
            say(SCAN_STARTED_LINE, 5500);
            s_scanHintUntil = now + SCAN_HINT_GRACE_MS;
            break;
        case ScanMoment::HIT:
            mood = Mood::SHOCKED;
            moodUntil = now + tempo(800);
            // Mood still reacts (visual feedback that something was
            // found); only the bubble text is held back so it can't
            // cut the hint off early.
            if (now >= s_scanHintUntil) say(pick(SCAN_HIT_LINES, 3), 2200);
            break;
        case ScanMoment::DONE_EMPTY:
            mood = Mood::SLEEPY;
            moodUntil = now + tempo(2000);
            if (now >= s_scanHintUntil) say(pick(SCAN_EMPTY_LINES, 3), 4000);
            break;
        case ScanMoment::DONE_FOUND:
            mood = Mood::BOUNCE;
            moodUntil = now + tempo(2000);
            if (now >= s_scanHintUntil) say(pick(SCAN_FOUND_LINES, 3), 4500);
            // "A lot" flourish -- same rare party-confetti mechanism
            // milestone detections and the outfit-unlock easter egg
            // use (see unlockAllOutfits() above), not a separate
            // effect of its own.
            if (count >= 5) {
                s_legendary      = true;
                s_legendaryUntil = now + 4000;
                for (uint8_t i = 0; i < CONFETTI_N; i++) {
                    s_cfx[i]   = (float)random(0, 240);
                    s_cfy[i]   = (float)random(-60, 0);
                    s_cfvy[i]  = 1.0f + (float)random(0, 20) / 10.0f;
                    s_cfcol[i] = (uint8_t)random(0, 6);
                }
            }
            break;
    }
}

void huntReaction(HuntMoment moment) {
    uint32_t now = millis();
    switch (moment) {
        case HuntMoment::STARTED:
            // Longer than his other HUNT bubbles (5500 vs 2200-3500),
            // same reasoning as SCAN_STARTED_LINE -- this is the one
            // line that actually needs to be read.
            say(HUNT_STARTED_LINE, 5500);
            s_huntHintUntil = now + HUNT_HINT_GRACE_MS;
            break;
        case HuntMoment::FIRST_SIGNAL:
            // STARTLED (reactPoseFor()'s default) -- a plain "oh, it's
            // there" startle, same pose an UNKNOWN-type detection gets.
            s_reactType = DetectionType::UNKNOWN;
            mood = Mood::SHOCKED;
            moodUntil = now + tempo(700);
            if (now >= s_huntHintUntil) say(pick(HUNT_FIRST_SIGNAL_LINES, 3), 2200);
            break;
        case HuntMoment::WARMER:
            mood = Mood::BOUNCE;
            moodUntil = now + tempo(800);
            if (now >= s_huntHintUntil) say(pick(HUNT_WARMER_LINES, 4), 2200);
            break;
        case HuntMoment::COLDER:
            // Borrows LOOK_AROUND (normally a tracker's "something's
            // following me" pose) reinterpreted as "searching for the
            // right direction" -- reads better than a droopy SLEEPY for
            // an actively-wrong-way cue, and costs no new pose code.
            s_reactType = DetectionType::AIRTAG;
            mood = Mood::SHOCKED;
            moodUntil = now + tempo(900);
            if (now >= s_huntHintUntil) say(pick(HUNT_COLDER_LINES, 4), 2200);
            break;
        case HuntMoment::HOT:
            // BOUNCE, not SHOCKED -- matches the same "found something
            // great" convention scanReaction()'s big-haul DONE_FOUND
            // uses, rather than an alarmed startle.
            mood = Mood::BOUNCE;
            moodUntil = now + tempo(1500);
            if (now >= s_huntHintUntil) say(pick(HUNT_HOT_LINES, 3), 3500);
            // Same rare party-confetti flourish milestone detections
            // and a big scan haul use -- a successful hunt earns it.
            s_legendary      = true;
            s_legendaryUntil = now + 4000;
            for (uint8_t i = 0; i < CONFETTI_N; i++) {
                s_cfx[i]   = (float)random(0, 240);
                s_cfy[i]   = (float)random(-60, 0);
                s_cfvy[i]  = 1.0f + (float)random(0, 20) / 10.0f;
                s_cfcol[i] = (uint8_t)random(0, 6);
            }
            break;
        case HuntMoment::STALLED:
            mood = Mood::SLEEPY;
            moodUntil = now + tempo(2000);
            if (now >= s_huntHintUntil) say(pick(HUNT_STALLED_LINES, 3), 4000);
            break;
    }
}

void watchAlertReaction() {
    mood = Mood::SHOCKED;
    moodUntil = millis() + tempo(1200);
    say(pick(WATCH_ALERT_LINES, 4), 3000);
}

// Costume overlays (see squachy.h's Outfits section), drawn last from
// drawBody() so hats/masks/accessories sit visibly on top of
// everything already painted -- purely additive, nothing here erases
// or replaces the base body, so an outfit never has to duplicate his
// shape logic. Same cx2/hy/S() coordinate system as drawBody() (hy
// already bobs with the current frame). Named after their homage, not
// direct recreations -- see the IP note in the design discussion this
// shipped from: same silhouette/gag, original details, so this stays
// safe to ship in a public repo.
// ---- VOID EYE helpers -----------------------------------------------------
// Where the iris is looking. Stateless on purpose: drawBody() runs more than
// once per logical frame on a banded board, so anything that stepped a stored
// position would move at double speed there and single speed everywhere else.
// That is the same trap the blink schedule further down already sidesteps by
// hashing the clock instead of remembering.
static void voidGazeTarget(uint32_t slot, int r, int& gx, int& gy) {
    uint32_t h = slot * 2654435761u;
    h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
    if ((h & 7u) == 0u) { gx = 0; gy = 0; return; }   // one slot in eight: the stare
    gx = ((int)(h % 9u) - 4) * r / 16;
    gy = ((int)((h >> 8) % 7u) - 3) * r / 16;
}

// It snaps to a new target and then holds there. Eyes saccade; a smooth drift
// across the whole slot reads as a fish, not as something looking at you.
static void voidGaze(uint32_t now, int r, int& gx, int& gy) {
    const uint32_t SLOT = 1600, MOVE = 260;
    const uint32_t slot = now / SLOT;
    int px = 0, py = 0, tx = 0, ty = 0;
    voidGazeTarget(slot ? slot - 1 : 0, r, px, py);
    voidGazeTarget(slot, r, tx, ty);
    const uint32_t into = now - slot * SLOT;
    if (into >= MOVE) { gx = tx; gy = ty; return; }
    gx = px + (tx - px) * (int)into / (int)MOVE;
    gy = py + (ty - py) * (int)into / (int)MOVE;
}

// 0 (open) to 255 (shut), on the same hashed slots the face's own blink uses
// so he never blinks to a metronome.
static uint8_t voidBlink(uint32_t now) {
    const uint32_t SLOT = 2600, DUR = 300;
    const uint32_t slot = now / SLOT;
    uint32_t h = slot * 2654435761u;
    h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
    const uint32_t t0 = slot * SLOT + 300 + (h % (SLOT - 900));
    if (now < t0) return 0;
    const uint32_t d = now - t0;
    if (d >= DUR) return 0;
    return (uint8_t)((d < DUR / 2) ? (d * 510 / DUR) : ((DUR - d) * 510 / DUR));
}

// Fur lids closing over the sphere, as row spans rather than rectangles: a
// rect would spill past the sphere and there is no clip region on a sprite.
// The sqrtf runs only while a lid is actually moving -- a few frames every
// couple of seconds -- never on the ordinary draw path.
static void voidLid(TFT_eSPI& t, int cx2, int cy, int r, uint8_t k, uint16_t col,
                    uint16_t edge) {
    if (!k || r <= 0) return;
    const int lh = (r * (int)k) / 255 + 1;
    for (int i = 0; i < lh && i <= r; i++) {
        const int yT = -r + i, yB = r - i;
        const int wT = (int)sqrtf((float)(r * r - yT * yT));
        const int wB = (int)sqrtf((float)(r * r - yB * yB));
        // The leading row of each lid in the edge colour: without it a
        // closing lid was a flat violet slab with nothing to say it moved.
        const uint16_t c = (i == lh - 1) ? edge : col;
        if (wT > 0) t.drawFastHLine(cx2 - wT, cy + yT, 2 * wT, c);
        if (wB > 0) t.drawFastHLine(cx2 - wB, cy + yB, 2 * wB, c);
    }
}

static void drawOutfit(TFT_eSPI& t, int cx2, int hy, uint32_t now, Mood m, float scale, OutfitId outfit) {
    if (outfit == OutfitId::NONE) return;
    auto S = [scale](int v) { return (int)(v * scale); };
    (void)m;
    using namespace Theme;

    switch (outfit) {
        case OutfitId::TANOOKI: {
            // A tanooki SUIT, the one-piece in the reference, not a raccoon
            // mask: his fur is recoloured to the suit's orange-brown up in
            // drawBody(), so the head reads as the hood with his face in its
            // opening. The two pointed ears on top go on in drawBody(), behind
            // the head, so it covers their roots: on top, their outline drew
            // a black line across his crown.
            // (Two passes at a mask came first. A bar across the brow read as
            // a visor; round ears with dark centres read as a second pair of
            // eyes.)
            // ---- the tail -------------------------------------------------
            // Drawn behind him, in drawBody(), not here -- see the tail block
            // up there. This case only draws the mask; the tail needs to be
            // under his body or it lies across his leg instead of coming out
            // from behind his hip.
            break;
        }
        case OutfitId::PARKA: {
            const uint16_t ink   = t.color565(18, 10, 4);
            const uint16_t org   = t.color565(255, 138, 26);
            const uint16_t seam  = t.color565(138, 68, 8);
            const uint16_t fur   = t.color565(107, 64, 40);

            const int oy = hy + S(8);
            const int ow = (S(17) * 102) / 100, oh = (S(16) * 102) / 100;
            const int R  = S(23);

            // ---- the hood, repainted as a solid ring OVER his head --------
            // A sprite has no clipping, so a hood that is genuinely in front
            // of him cannot be had by drawing order alone: his head is drawn
            // at full size underneath, and his ears and the corners of his
            // jaw both reach past the opening. This walks the shell row by
            // row and refills everything outside the opening, which paints
            // those out and leaves nothing of his face outside the ring.
            //
            // The black ring is FILLED by this same loop rather than stroked
            // with drawEllipse afterwards. Stroking it was what put orange
            // flecks in the brown around his face: the fill stepped its edge
            // by truncating a square root once per row, drawEllipse stepped
            // its own by Bresenham, and wherever the curve runs flat the two
            // disagreed by a pixel and left hood orange showing through the
            // gap between them. One number per row makes that impossible.
            const int rw = ow - 2, rh = oh - 2;    // the ring's inner edge
            for (int dy2 = -(R + 1); dy2 <= R + 1; dy2++) {
                const int yy = oy + dy2;
                const int a2 = (R + 1) * (R + 1) - dy2 * dy2;
                if (a2 < 0) continue;
                const int xo = (int)sqrtf((float)a2);
                const int b2 = R * R - dy2 * dy2;
                const int xr = (b2 > 0) ? (int)sqrtf((float)b2) : -1;
                // Half-widths of the ring's inner and outer edges on this
                // row. Both fall to zero once the row clears the opening,
                // which is what fills the rows above and below it solid.
                int xj = 0, xk = 0;
                if (dy2 > -rh && dy2 < rh) {
                    const float k = 1.0f - ((float)dy2 * (float)dy2) / ((float)rh * (float)rh);
                    xj = (int)((float)rw * sqrtf(k));
                }
                if (dy2 > -oh && dy2 < oh) {
                    const float k = 1.0f - ((float)dy2 * (float)dy2) / ((float)oh * (float)oh);
                    xk = (int)((float)ow * sqrtf(k));
                }
                // Black from the ring's inner edge all the way out, then the
                // hood's orange back over everything beyond its outer edge.
                // What survives in between is the ring itself.
                if (xo > xj) {
                    t.drawFastHLine(cx2 - xo, yy, xo - xj + 1, ink);
                    t.drawFastHLine(cx2 + xj, yy, xo - xj + 1, ink);
                }
                if (xr > xk) {
                    t.drawFastHLine(cx2 - xr, yy, xr - xk + 1, org);
                    t.drawFastHLine(cx2 + xk, yy, xr - xk + 1, org);
                }
            }
            t.drawCircle(cx2, oy, R + 1, ink);

            // No stitching. Everything outside the black ring is the flat
            // orange of the hood, the way the reference has it -- a second
            // ellipse out here only ever read as a line drawn across the hood
            // rather than as a seam in it, whatever size it was set to.
            // The hood's own outer edge, below, and the drawstring, at the
            // bottom, are the only marks that belong out here.

            // ---- arms and mittens, after the hood -------------------------
            // The hood shell is painted after the arms in drawBody() -- it has
            // to be, or every sleeve outline cuts across it -- so an arm that
            // reaches up into the hood is buried, leaving the mitten floating
            // beside his head. WAVE swings an arm straight through that disc.
            // Any hand ending inside the hood therefore gets its whole arm
            // redrawn here, on top. Arms hanging clear of it are left as
            // drawBody() drew them, which is what keeps his resting shoulders
            // from showing through the hood.
            const int mr = S(5);
            const int hdr = R + mr;
            for (int8_t sg = -1; sg <= 1; sg += 2) {
                const int ax = (sg < 0) ? s_armL1x : s_armR1x;
                const int ay = (sg < 0) ? s_armL1y : s_armR1y;
                const int sx = (sg < 0) ? s_armL0x : s_armR0x;
                const int sy = (sg < 0) ? s_armL0y : s_armR0y;
                // Does the arm, shoulder to hand, pass through the hood? The
                // closest point on the segment to the hood's centre, and its
                // distance. Testing only the hand missed a wave, where the
                // hand clears the hood but the forearm crosses it, and the
                // mitten hung in the air with no sleeve behind it.
                const float vx = (float)(ax - sx), vy = (float)(ay - sy);
                const float wx = (float)(cx2 - sx), wy = (float)(oy - sy);
                const float vv = vx * vx + vy * vy;
                float u = vv > 0.0f ? (vx * wx + vy * wy) / vv : 0.0f;
                if (u < 0.0f) u = 0.0f; else if (u > 1.0f) u = 1.0f;
                const float qx = (float)sx + u * vx - (float)cx2;
                const float qy = (float)sy + u * vy - (float)oy;
                if (qx * qx + qy * qy < (float)(hdr * hdr)) {
                    wideLine(t, sx, sy, ax, ay, S(7) + 2, seam);
                    wideLine(t, sx, sy, ax, ay, S(7), org);
                }
                // Mittens, thumbs inboard. The thumb is filled and then
                // outlined so its own edge crosses the palm and reads as a
                // second shape.
                t.fillCircle(ax, ay, mr + 1, ink);
                t.fillCircle(ax, ay, mr, fur);
                const int tr = (mr * 52) / 100;
                const int tx = ax - sg * ((mr * 62) / 100);
                t.fillCircle(tx, ay - S(2), tr, fur);
                t.drawCircle(tx, ay - S(2), tr, ink);
            }

            // The drawstring is only the V, off the chin of the opening.
            const int d  = (int)(sinf((float)(now % 9000) / 9000.0f * 6.2831853f) * (float)S(1));
            const int vy = oy + oh - S(1);
            wideLine(t, cx2 + d, vy, cx2 - S(5) + d, vy + S(8), 2, seam);
            wideLine(t, cx2 + d, vy, cx2 + S(5) + d, vy + S(8), 2, seam);
            break;
        }
        case OutfitId::VOIDEYE: {
            // 16, not the 14 this shipped with. That number was chosen while
            // the title bar still owned the top sixteen rows of the screen, so
            // it was sized to read big WITHOUT asking for headroom -- his head
            // box is only 30x24 and anything taller went behind the bar.
            //
            // Measured off rendered frames now that the bar is gone: every
            // other costume's silhouette tops out around row 8 (that is his
            // own crest, not the costume), and this one stopped at row 28.
            // Twenty rows of empty sky that nothing else was using.
            //
            // 16 and not more: at 17 the sphere starts to outgrow its own
            // socket, and the violet ring that reads as an eye SET IN
            // something becomes a crescent hanging under a loose ball.
            //
            // Only the radius moves. ey stays at S(11) so the sphere grows
            // around its centre instead of climbing out of the socket, and
            // every socket ellipse below is expressed in er, so they grow
            // with it and the proportions hold.
            const int er = S(16);
            const int ey = hy + S(11);
            // SOCKET, drawn BEHIND the sphere so it shows only as a violet ring
            // at the sides and under the chin. Over the top it would cover the
            // sky, which is the part of this costume worth having.
            // A faint glow ring round the socket, breathing slowly.
            const bool glow = ((now / 1400) % 3u) != 0;
            t.fillEllipse(cx2, ey + S(5), er + S(7), (er * 92) / 100 + S(1),
                          glow ? t.color565(73, 36, 170) : t.color565(36, 36, 85));
            t.fillEllipse(cx2, ey + S(5), er + S(6), (er * 92) / 100, t.color565(70, 60, 130));
            t.fillEllipse(cx2, ey + S(6), er + S(3), (er * 82) / 100, t.color565(20, 14, 48));
            // The sphere. That outer circle is a RIM, not the black underlay
            // every flying junk object gets: this is worn against a black sky
            // full of stars, and a black outline against that is nothing at all.
            t.fillCircle(cx2, ey, er + 1, t.color565(130, 100, 190));
            t.fillCircle(cx2, ey, er,     t.color565( 44,  30,  92));
            // The sky inside him, running the same warp the Starfield does --
            // which is where he took the eye from. See void_eye.h for why none
            // of this computes a sine.
            const uint8_t phase = (uint8_t)((now / 44) & 63u);
            for (uint8_t i = 0; i < VOID_STAR_N; i++) {
                const uint8_t sk = (uint8_t)((phase + VOID_STAR_PH[i]) & 63u);
                const int rad = VOID_RSTEP[sk];
                const int sx = cx2 + (VOID_STAR_DX[i] * rad * er) / 4096;
                const int sy = ey  + (VOID_STAR_DY[i] * rad * er) / 4096;
                int b = 90 + sk * 2; if (b > 255) b = 255;
                const uint16_t sc = t.color565(b, b, 255);
                if (sk > 52) t.fillRect(sx, sy, 2, 2, sc);
                else         t.drawPixel(sx, sy, sc);
            }
            // The iris rides the gaze. The specular highlight deliberately does
            // not: it is a reflection off the sphere, and the sphere is not the
            // thing that moved.
            int gx = 0, gy = 0;
            voidGaze(now, er, gx, gy);
            const int ix = cx2 + (er * 16) / 100 + gx, iy = ey + gy;
            t.fillCircle(ix, iy, (er * 58) / 100, t.color565( 56,  10,  96));
            t.fillCircle(ix, iy, (er * 50) / 100, t.color565(150,  60, 220));
            const uint16_t spokeCol = t.color565(96, 36, 170);
            for (uint8_t sp = 0; sp < VOID_SPOKE_N; sp++) {
                t.drawLine(ix + (VOID_SPOKE[sp][0] * er) / 64,
                           iy + (VOID_SPOKE[sp][1] * er) / 64,
                           ix + (VOID_SPOKE[sp][2] * er) / 64,
                           iy + (VOID_SPOKE[sp][3] * er) / 64, spokeCol);
            }
            t.fillCircle(ix, iy, (er * 24) / 100, BLACK);
            t.fillCircle(cx2 - (er * 8) / 100, ey - (er * 36) / 100,
                         (er * 17) / 100, t.color565(150, 140, 210));
            voidLid(t, cx2, ey, er, voidBlink(now), t.color565(70, 60, 130), BLACK);
            break;
        }
        case OutfitId::WOLFPELT: {
            // Worn, not become: the skull is pushed back on his head
            // like a hood, jaw hanging over his brow, pelt down the
            // shoulders. Everything sits ABOVE hy + S(1), which is where
            // drawBody puts the shades -- his own face, lenses and grin
            // stay completely visible underneath, which is the whole
            // difference between this and a transformation.
            const uint16_t peltDark = blend(BLACK, WHITE, 58);
            const uint16_t peltMid  = blend(BLACK, WHITE, 96);
            const uint16_t peltLit  = blend(BLACK, WHITE, 130);

            // Vertical budget, learned the hard way over three passes.
            // hy is NOT a fixed distance from the top of the drawing
            // area: Squachy sits lower when his speech bubble is up and
            // rides higher when it is not, so the headroom above him
            // swings by several pixels frame to frame. Anything that
            // needs more than about S(10) of clearance is fine in some
            // frames and sliced off in others, which is exactly what
            // happened to a skull at hy - S(26), then hy - S(18), then
            // hy - S(11) with ears above it.
            //
            // Ears stand UP. That is the pose people expect, but it is
            // also what cost three earlier passes: hy is not a fixed
            // distance from the top of the drawing area, so a fixed
            // height is fine while he is quiet and sliced off the moment
            // a speech bubble pushes him down.
            //
            // s_topLimit is the region top the caller actually gave us,
            // so the tips are clamped to it rather than hoped for. When
            // room is tight they shorten instead of being cut, which
            // reads as ears at a different angle rather than as damage.

            // A fixed ear LENGTH that translates with the bob, not a fixed tip
            // position re-clamped every frame. Clamping the tip meant the
            // length changed as he moved -- by up to 9px of scale at BOUNCE --
            // so the ears visibly squashed and stretched through every cycle,
            // which read as the pelt breathing rather than as him bouncing.
            //
            // s_hyCeiling is the highest hy reaches across the whole bob, so
            // clamping against it once gives ears that still clear the region
            // top at the very top of the bounce and hold that length all the
            // way down.
            // ---- the skull -------------------------------------------
            // Worn askew: the whole thing is TILTED rather than square --
            // one ear taller and set further back than the other, the
            // sockets at different heights, the jaw running at an angle
            // and the teeth shortening as they follow it. A mask that
            // sits perfectly straight reads as a face; one that has
            // slipped reads as something he put on, which is the entire
            // premise of this costume.
            //
            // It stays centred on him, though. The design this came from
            // was also shifted a few pixels to one side, and at this size
            // that pushed the far ear past his own silhouette. The tilt
            // carries the "worn" read on its own; the shift only cost
            // symmetry with the rest of him.
            //
            // Integer S() cannot express the 1.25 sizing without rounding
            // every coefficient twice, so this case scales off the float.
            // Named Sf and not F: Arduino defines F() as the flash-string
            // helper macro, and a lambda by that name compiles to a pile
            // of "invalid cast from float to const __FlashStringHelper*".
            // Named Sf, not F: Arduino defines F() as the flash-string
            // helper macro, and a lambda by that name turns every call
            // site into "invalid cast from float to
            // const __FlashStringHelper*".
            auto Sf = [scale](float v) { return (int)(v * scale); };

            // The whole mask sits a little lower on his head than the
            // skull's own geometry would put it. Two things wanted this at
            // once: it looked like a headband riding above his forehead
            // rather than a hood pulled on, and the ears were coming out
            // stubby -- they are length-clamped against the top of the
            // region, so every pixel the mask moves DOWN is a pixel of ear
            // length the clamp can afford to give back. Dropping it and
            // un-squashing them is the same edit.
            const int md  = Sf(2.5f);
            const int mhy = hy + md;

            // Ears: the exact proportions that shipped in v1.5.11 --
            // Sf(20) nominal, a 9-unit base, and BOTH SIDES THE SAME
            // LENGTH. Narrowing them and giving each side a different
            // length to sell the tilt made them worse, not better; the
            // slanted skull and the offset sockets carry the askew read
            // perfectly well on their own.
            //
            // The one thing deliberately not restored from that release is
            // how the length is arrived at. That version clamped the TIP
            // against the region top and recomputed it every frame from a
            // bobbing hy, so the ears changed length as he moved -- by up
            // to 9 px of scale on a BOUNCE -- and visibly squashed through
            // every cycle. This clamps the LENGTH once, against
            // s_hyCeiling (the highest hy ever reaches), so they hold it
            // all the way down. Same ears, minus the breathing.
            int earLen = Sf(25.0f);                        // 25% up from Sf(20)
            if (s_topLimit > -5000 && s_hyCeiling > -5000) {
                // The tips are MEANT to pass behind the title bar now
                // rather than stop short of it. Stopping short was the
                // only way to guarantee a constant length, but it also
                // capped them at about 33 px -- well under their design --
                // and no amount of raising the nominal could get past it.
                //
                // Clipping is safe here because the title bar paints AFTER
                // Squachy, so an over-length ear is occluded rather than
                // corrupting anything, and sliding behind a hard edge reads
                // as occlusion rather than as the ear shrinking. That is
                // the difference between this and the old squash bug,
                // where a tip was re-clamped to a position with nothing
                // on screen to explain it.
                //
                // The overshoot is bounded rather than unlimited: this
                // outfit also renders in the small cameos on RAW SCAN and
                // HUNT, where whatever sits above Squachy's region is NOT
                // guaranteed to be drawn after him. Sf(10) is enough for
                // full length on CLEAR and not enough to reach anyone
                // else's UI.
                const int maxLen = s_hyCeiling + md - (s_topLimit + 1) + Sf(10.0f);
                if (earLen > maxLen) earLen = maxLen;
            }
            if (earLen < Sf(10.0f)) earLen = Sf(10.0f);     // never stubbier than the hood
            const int earTip = mhy - earLen;

            // An outline round the whole mask first -- band, skull, ears and
            // jaw, all in ink a pixel out -- so it sits ON him as one worn
            // thing instead of greys melting into his brown.
            {
                static const int8_t O[4][2] = { {-1, 0}, {1, 0}, {0, -1}, {0, 1} };
                for (uint8_t q = 0; q < 4; q++) {
                    const int ox = O[q][0], oy = O[q][1];
                    t.fillRoundRect(cx2 - S(14) + ox, hy - S(8) + oy, S(28), S(7), S(3), BLACK);
                    t.fillTriangle(cx2 - Sf(13.5f) + ox, mhy - Sf(6.5f) + oy, cx2 + Sf(13.5f) + ox, mhy - Sf(8.0f) + oy,
                                   cx2 + Sf(13.5f) + ox, mhy - Sf(2.0f) + oy, BLACK);
                    t.fillTriangle(cx2 - Sf(13.5f) + ox, mhy - Sf(6.5f) + oy, cx2 + Sf(13.5f) + ox, mhy - Sf(2.0f) + oy,
                                   cx2 - Sf(13.5f) + ox, mhy - Sf(0.5f) + oy, BLACK);
                    t.fillTriangle(cx2 - Sf(13.0f) + ox, mhy - Sf(5.5f) + oy, cx2 - Sf(4.0f) + ox, mhy - Sf(8.5f) + oy,
                                   cx2 - Sf(11.0f) + ox, earTip + oy, BLACK);
                    t.fillTriangle(cx2 + Sf(13.0f) + ox, mhy - Sf(5.5f) + oy, cx2 + Sf(4.0f) + ox, mhy - Sf(8.5f) + oy,
                                   cx2 + Sf(11.0f) + ox, earTip + oy, BLACK);
                    t.fillTriangle(cx2 - Sf(14.0f) + ox, mhy - Sf(1.0f) + oy, cx2 + Sf(14.0f) + ox, mhy - Sf(2.5f) + oy,
                                   cx2 + Sf(14.0f) + ox, mhy + Sf(1.0f) + oy, BLACK);
                    t.fillTriangle(cx2 - Sf(14.0f) + ox, mhy - Sf(1.0f) + oy, cx2 - Sf(14.0f) + ox, mhy + Sf(2.5f) + oy,
                                   cx2 + Sf(14.0f) + ox, mhy + Sf(1.0f) + oy, BLACK);
                }
            }
            t.fillRoundRect(cx2 - S(14), hy - S(8), S(28), S(7), S(3), peltDark);
            t.fillRoundRect(cx2 - S(10), hy - S(7), S(20), S(4), S(2), peltMid);

            // Cranium, as a slanted quad rather than an upright box --
            // two triangles sharing a diagonal. An axis-aligned rounded
            // rect cannot tilt, and without the tilt the skull read as a
            // grey visor rather than as something worn crooked.
            //
            // The top edge sits at mhy - Sf(7.5), NOT at the Sf(10)
            // ceiling. It went to Sf(10) first and that was a mistake:
            // the ears are length-clamped to roughly mhy - 11.6 units on a
            // normal CLEAR region, so a crown at 10 left barely a pixel of
            // ear showing above it and swallowed the one silhouette cue
            // that says wolf. 7.5 restores the same ear clearance the
            // original mask had, and the extra size goes into width --
            // Sf(27) against a head that is only S(30) across -- where
            // there is no budget to run out of.
            // A unit shallower than it was: every unit off the crown is a
            // unit of ear that shows above it, and the ears are the cue
            // that says wolf.
            const int ctl = mhy - Sf(6.5f), ctr = mhy - Sf(8.0f);   // top corners
            const int cbl = mhy - Sf(0.5f), cbr = mhy - Sf(2.0f);   // bottom corners
            t.fillTriangle(cx2 - Sf(13.5f), ctl, cx2 + Sf(13.5f), ctr,
                           cx2 + Sf(13.5f), cbr, peltMid);
            t.fillTriangle(cx2 - Sf(13.5f), ctl, cx2 + Sf(13.5f), cbr,
                           cx2 - Sf(13.5f), cbl, peltMid);
            // Brow, a shade lighter along the top edge, following the same
            // slant so the tilt reads even where the skull meets the ears.
            t.fillTriangle(cx2 - Sf(13.5f), ctl, cx2 + Sf(13.5f), ctr,
                           cx2 + Sf(13.5f), ctr + Sf(2.2f), peltLit);
            t.fillTriangle(cx2 - Sf(13.5f), ctl, cx2 + Sf(13.5f), ctr + Sf(2.2f),
                           cx2 - Sf(13.5f), ctl + Sf(2.2f), peltLit);

            // Ears go on AFTER the skull, not under it. This was the whole
            // reason they looked stubby: drawn first, the new cranium
            // covered everything below the tip and left a nub. v1.5.11
            // drew them over its own skull plate for exactly this reason,
            // and the full triangle is the shape people read as an ear.
            // Bases lifted a unit and a half so they emerge from the
            // crown line rather than from down beside the sockets --
            // ears growing out of the top of a skull, not out of its
            // temples. The tips are set by earTip, so this raises where
            // each ear starts without shortening it.
            t.fillTriangle(cx2 - Sf(13.0f), mhy - Sf(5.5f), cx2 - Sf(4.0f), mhy - Sf(8.5f),
                           cx2 - Sf(11.0f), earTip, peltDark);
            t.fillTriangle(cx2 + Sf(13.0f), mhy - Sf(5.5f), cx2 + Sf(4.0f), mhy - Sf(8.5f),
                           cx2 + Sf(11.0f), earTip, peltDark);
            // Inner ear, shorter, and pink: grey inside grey was one more
            // shade of the same mask.
            const uint16_t earPink = t.color565(219, 109, 146);
            t.fillTriangle(cx2 - Sf(11.0f), mhy - Sf(6.5f), cx2 - Sf(6.0f), mhy - Sf(8.5f),
                           cx2 - Sf(10.0f), earTip + Sf(4.0f), earPink);
            t.fillTriangle(cx2 + Sf(11.0f), mhy - Sf(6.5f), cx2 + Sf(6.0f), mhy - Sf(8.5f),
                           cx2 + Sf(10.0f), earTip + Sf(4.0f), earPink);

            // Sockets, lit. These used to be deliberately dead on the
            // reasoning that a live pair belongs to the werewolf out on
            // the fire -- but a trophy skull with the lights still on is
            // a better costume than a correct one, so they glow.
            //
            // The pulse is a slow breath rather than a blink: a hard on/off
            // at this size reads as a rendering fault, where a ramp reads
            // as something banked and smouldering.
            const float ember = 0.62f + 0.38f * sinf((float)now / 620.0f);
            const uint16_t glowOut = blend(BLACK, RED, (uint16_t)(70.0f + ember * 60.0f));
            const uint16_t glowIn  = blend(RED, VAPOR_YELLOW, (uint16_t)(ember * 120.0f));
            // Halo first, then the core on top of it.
            // Set at different heights -- the tilt, in the place it reads
            // hardest. Two eyes level with each other undo the whole pose.
            // Set at different heights, following the skull's own slant --
            // the tilt in the one place it reads hardest. Two sockets level
            // with each other undo the whole pose.
            t.fillRect(cx2 - Sf(10.5f), mhy - Sf(5.0f), Sf(8.0f), Sf(4.0f), glowOut);
            t.fillRect(cx2 + Sf(2.5f),  mhy - Sf(6.5f), Sf(8.0f), Sf(4.0f), glowOut);
            t.fillRect(cx2 - Sf(9.0f),  mhy - Sf(4.2f), Sf(5.0f), Sf(2.4f), glowIn);
            t.fillRect(cx2 + Sf(4.0f),  mhy - Sf(5.7f), Sf(5.0f), Sf(2.4f), glowIn);

            // Jaw line, running at an angle across him rather than square.
            // Two triangles making one slanted band: thicker on the left,
            // riding up toward the right.
            t.fillTriangle(cx2 - Sf(14.0f), mhy - Sf(1.0f), cx2 + Sf(14.0f), mhy - Sf(2.5f),
                           cx2 + Sf(14.0f), mhy + Sf(1.0f), peltLit);
            t.fillTriangle(cx2 - Sf(14.0f), mhy - Sf(1.0f), cx2 - Sf(14.0f), mhy + Sf(2.5f),
                           cx2 + Sf(14.0f), mhy + Sf(1.0f), peltLit);

            // Four teeth, shortening as they climb the slanted jaw. The
            // longest stops at mhy + 5*scale, one unit clear of the lenses
            // at mhy + S(6) -- the old row ran to mhy + S(8) and sat on top
            // of them, which is what made the whole mask read as teeth.
            // There is far more skull above them now to carry it instead.
            for (int i = 0; i < 4; i++) {
                const int fx = cx2 - Sf(10.5f) + Sf(6.5f) * i;
                // Measured against the undropped hy on purpose: his lenses
                // are at hy + S(6) and they did not move down with the
                // mask, so the bite has to give back exactly what the drop
                // took. The longest tooth ends at hy + 6.0 * scale --
                // touching the top edge of the lens, never over it.
                const int ty = mhy + (int)(scale * (1.3f - 0.5f * (float)i));
                const int tl = (int)(scale * (2.2f - 0.3f * (float)i));
                t.fillTriangle(fx, ty, fx + Sf(3.4f), ty, fx + Sf(1.7f), ty + tl, WHITE);
            }

            // Pelt over both shoulders, hung on the arms THEMSELVES.
            //
            // This used to be four rectangles at fixed coordinates, which
            // was fine while the arms only ever hung straight down. They
            // do not: they sway on idle, swing on a wave, throw wide on a
            // startle, go overhead on a stretch and alternate on a
            // juggle -- and the pelt stayed exactly where it was through
            // all of it, so the arm slid out from under its own fur.
            //
            // drawBody() now publishes where each arm actually ended up
            // (see s_armL0x and friends, written by limbTo()/restArms()),
            // so the pelt is drawn along the upper arm wherever that
            // turned out to be, offset outboard so it sits over the arm
            // rather than down its middle.
            //
            // Upper arm only, to a little past half way: a pelt that ran
            // the whole length would read as a sleeve, and this is a hide
            // thrown over a shoulder.
            auto peltOn = [&](int x0, int y0, int x1, int y1, int outward) {
                const int mx = x0 + (x1 - x0) * 55 / 100;
                const int my = y0 + (y1 - y0) * 55 / 100;
                wideLine(t, x0 + outward, y0 - Sf(2.0f), mx + outward, my, Sf(8.0f) + 2, BLACK);
                wideLine(t, x0 + outward, y0 - Sf(2.0f), mx + outward, my, Sf(8.0f), peltDark);
                wideLine(t, x0 + outward, y0 + Sf(1.0f), mx + outward, my - Sf(1.0f), Sf(3.0f), peltMid);
                // The paw at the end of it, claws out.
                const int px = mx + outward, py = my + Sf(2.0f);
                t.fillCircle(px, py, Sf(3.5f) + 1, BLACK);
                t.fillCircle(px, py, Sf(3.5f), peltMid);
                for (int c = -1; c <= 1; c++) {
                    const int cxw = px + c * Sf(2.0f);
                    t.fillTriangle(cxw - Sf(0.8f), py + Sf(2.0f), cxw + Sf(0.8f), py + Sf(2.0f),
                                   cxw, py + Sf(4.5f), WHITE);
                }
            };
            // One unit outboard, so the hide sits ON the upper arm with a
            // sliver past its outer edge. Three units left most of it hanging
            // in the air beside the arm, reading as a second, darker limb.
            peltOn(s_armL0x, s_armL0y, s_armL1x, s_armL1y, -Sf(1.0f));
            peltOn(s_armR0x, s_armR0y, s_armR1x, s_armR1y,  Sf(1.0f));
            break;
        }
        case OutfitId::SHARK: {
            // A shark onesie: hood over the crown with his own face in the
            // mouth, a silver suit, and both arms turned into swept fins.
            //
            // Every colour is snapped to the RGB332 grid the frame buffer
            // actually has -- red and green in eighths, BLUE IN QUARTERS
            // (0/85/170/255). Silver is the worst thing to ask of that: a
            // silver ramp wants four or five steps of a cool near-neutral and
            // the panel has four in total, so what is below IS the whole
            // available range. The obvious deep silver, (109,109,85), renders
            // OLIVE -- red and green sit two steps above a blue that cannot
            // follow them. (73,73,85) is the only cool dark there is.
            const uint16_t silvDk = t.color565( 73,  73,  85);
            const uint16_t silv   = t.color565(146, 146, 170);
            const uint16_t silvHi = t.color565(219, 219, 255);
            const uint16_t gum    = t.color565(219,  36,  85);
            const uint16_t eyeBk  = t.color565(0, 0, 0);
            const int halfW = S(16);

            // ---- the dorsal fin, and the hood over his crown -------------
            // Outlined like every other costume: the same shapes a pixel
            // larger in black first, then the grey over them, so fin, hood
            // and jaw corners share ONE edge with no seam where they meet.
            // The fin's base flares into the hood top (two small triangles)
            // rather than sitting on it as a separate triangle.
            const int o = S(1) > 1 ? S(1) : 1;
            const int jawW = S(5), jawBot = hy + S(13);     // past the shades, above the ears
            t.fillTriangle(cx2 - S(8) - o, hy - S(6), cx2 + S(8) + o, hy - S(6),
                           cx2 + S(4), hy - S(18) - o, eyeBk);
            t.fillTriangle(cx2 - S(10) - o, hy - S(9), cx2 - S(6), hy - S(9), cx2 - S(6), hy - S(12) - o, eyeBk);
            t.fillTriangle(cx2 + S(11) + o, hy - S(9), cx2 + S(6), hy - S(9), cx2 + S(6), hy - S(13) - o, eyeBk);
            t.fillRoundRect(cx2 - halfW - o, hy - S(10) - o, halfW * 2 + 2 * o, S(11) + 2 * o, S(5), eyeBk);
            t.fillRoundRect(cx2 - halfW - o, hy - S(2), jawW + 2 * o, jawBot - (hy - S(2)) + o, S(2), eyeBk);
            t.fillRoundRect(cx2 + halfW - jawW - o, hy - S(2), jawW + 2 * o, jawBot - (hy - S(2)) + o, S(2), eyeBk);
            t.fillTriangle(cx2 - S(7), hy - S(7), cx2 + S(7), hy - S(7),
                           cx2 + S(4), hy - S(18), silvDk);
            t.fillTriangle(cx2 - S(10), hy - S(9), cx2 - S(6), hy - S(9), cx2 - S(6), hy - S(12), silvDk);
            t.fillTriangle(cx2 + S(11), hy - S(9), cx2 + S(6), hy - S(9), cx2 + S(6), hy - S(13), silvDk);
            t.fillRoundRect(cx2 - halfW, hy - S(10), halfW * 2, S(11), S(5), silvDk);
            // The jaw corners: the hood's sides carried down past the eyes.
            t.fillRoundRect(cx2 - halfW, hy - S(2), jawW, jawBot - (hy - S(2)), S(2), silvDk);
            t.fillRoundRect(cx2 + halfW - jawW, hy - S(2), jawW, jawBot - (hy - S(2)), S(2), silvDk);
            // Two tiny nostrils on the snout, just above the lip.
            {
                const int nr = (S(1) + 1) / 2 > 1 ? (S(1) + 1) / 2 : 1;
                t.fillCircle(cx2 - S(2), hy - S(3), nr, eyeBk);
                t.fillCircle(cx2 + S(2), hy - S(3), nr, eyeBk);
            }
            // The lip: where the hood meets the white band.
            t.fillRect(cx2 - halfW + jawW, hy - S(1) - o, halfW * 2 - 2 * jawW, o, eyeBk);
            t.fillRoundRect(cx2 - halfW + S(1), hy - S(1), halfW * 2 - S(2), S(4), S(2), silvHi);
            t.fillRect(cx2 - halfW + S(2), hy + S(2), halfW * 2 - S(4), S(2), gum);
            {   // the teeth, inset a shade from the jaw so the jaw has a lip
                const int x0 = cx2 - halfW + S(3), x1 = cx2 + halfW - S(3);
                const int tw = (x1 - x0) / 7;
                for (int i = 0; i < 7 && tw > 0; i++) {
                    const int a = x0 + i * tw;
                    t.fillTriangle(a, hy + S(4), a + tw, hy + S(4),
                                   a + tw / 2, hy + S(8), WHITE);
                }
            }
            t.fillCircle(cx2 - S(11), hy - S(6), S(2), silvHi);
            t.fillCircle(cx2 + S(11), hy - S(6), S(2), silvHi);
            t.fillCircle(cx2 - S(11), hy - S(6), S(1), eyeBk);
            t.fillCircle(cx2 + S(11), hy - S(6), S(1), eyeBk);

            // ---- the arms, before the suit ------------------------------
            // Drawn along wherever each arm ACTUALLY ended up this frame --
            // drawBody publishes both endpoints for exactly this, and the
            // wolf pelt already learned what happens to a costume that
            // assumes the arms are at rest.
            //
            // Two things make it a fin rather than a sleeve. It TAPERS from a
            // wide shoulder to a point, and the point is thrown OUTWARD, away
            // from his centre line, as well as past the hand: at rest his
            // arms hang almost straight down, so "past the hand" alone is
            // also straight down and nothing reads as swept.
            //
            // Drawn before the suit so the suit covers where they meet him,
            // which is how a sleeve sits on a shoulder.
            {
                const float over = 11.0f, wide = 5.0f;
                const int ax[2] = { s_armL0x, s_armR0x }, ay[2] = { s_armL0y, s_armR0y };
                const int bx[2] = { s_armL1x, s_armR1x }, by[2] = { s_armL1y, s_armR1y };
                for (int k = 0; k < 2; k++) {
                    const float dx = (float)(bx[k] - ax[k]), dy = (float)(by[k] - ay[k]);
                    const float len = sqrtf(dx * dx + dy * dy);
                    if (len < 1.0f) continue;
                    const float ux = dx / len, uy = dy / len;
                    const float px = -uy, py = ux;               // across the arm
                    const float hw = (float)S(1) * wide;
                    const float out = (bx[k] < cx2) ? -1.0f : 1.0f;
                    const int tx = bx[k] + (int)(ux * (float)S(1) * over +
                                                 out * (float)S(1) * over * 0.9f);
                    const int ty = by[k] + (int)(uy * (float)S(1) * over * 0.55f);
                    t.fillTriangle(ax[k] + (int)(px * hw), ay[k] + (int)(py * hw),
                                   ax[k] - (int)(px * hw), ay[k] - (int)(py * hw),
                                   tx, ty, silv);
                    // A keyline down BOTH edges. With the tip thrown outward
                    // one of the two is the inside of the fin, so lighting one
                    // and shading the other drew a crease across it.
                    wideLine(t, ax[k] + px * hw, ay[k] + py * hw,
                             (float)tx, (float)ty, (float)S(1), silvDk);
                    wideLine(t, ax[k] - px * hw, ay[k] - py * hw,
                             (float)tx, (float)ty, (float)S(1), silvDk);
                    // ...and one short highlight near the shoulder, where the
                    // light would catch it, rather than down the whole span.
                    wideLine(t, (float)ax[k], (float)ay[k],
                             ax[k] + ux * (float)S(1) * 6.0f + out * (float)S(1) * 3.0f,
                             ay[k] + uy * (float)S(1) * 6.0f, (float)S(1), silvHi);
                }
            }

            // ---- the suit ------------------------------------------------
            // A lit band across the top and a dark one along the bottom:
            // metal reads by a hard split, not by a gradient, and with four
            // steps to spend a gradient is not on offer anyway.
            {
                const int hw = S(11);
                t.fillRoundRect(cx2 - hw, hy + S(23), hw * 2, S(17), S(7), silv);
                t.fillRoundRect(cx2 - hw, hy + S(37), hw * 2, S(3), S(2), silvDk);
                t.fillRect(cx2 - hw + S(2), hy + S(25), hw * 2 - S(4), S(3), silvHi);
                t.fillRect(cx2 - hw + S(3), hy + S(25), hw * 2 - S(6), S(1), WHITE);
                const int bw = hw - S(6);
                t.fillRoundRect(cx2 - bw, hy + S(28), bw * 2, S(11), S(4), silvHi);
                t.fillRect(cx2 - bw, hy + S(32), bw * 2, S(1), silv);
                t.fillRect(cx2 - bw, hy + S(35), bw * 2, S(1), silv);
                // Gill slashes up on the chest, above where the fins reach.
                for (int i = 0; i < 3; i++) {
                    const int gx = cx2 + hw - S(4) - i * S(3);
                    t.fillTriangle(gx, hy + S(24), gx + S(1), hy + S(24),
                                   gx - S(1), hy + S(28), silvHi);
                    const int gl = cx2 - hw + S(4) + i * S(3);
                    t.fillTriangle(gl, hy + S(24), gl - S(1), hy + S(24),
                                   gl + S(1), hy + S(28), silvHi);
                }
            }
            break;
        }
        case OutfitId::UNICORN: {
            // Base sits right at hy -- the top edge of the head shape
            // itself (see drawBody's head fillRoundRect a few lines
            // below this switch) -- not up at the crest peak (hy -
            // S(14)), which put the whole horn floating well above his
            // actual face. Pastel fur is handled separately, up in
            // drawBody()'s furMain/furLight block.
            int baseY = hy;
            int tipY  = baseY - S(24);
            int baseW = S(9);
            t.fillTriangle(cx2 - baseW / 2, baseY, cx2 + baseW / 2, baseY, cx2, tipY, WHITE);
            // Candy-cane twist: diagonal rainbow stripes crossing the
            // cone (rather than flat horizontal bands) so it reads as
            // a spiral, narrowing to match the cone's taper as they
            // climb toward the tip.
            static const uint16_t bands[6] = { RED, AMBER, VAPOR_YELLOW, GREEN, VAPOR_BLUE, VAPOR_PURPLE };
            for (int i = 0; i < 6; i++) {
                float frac = (float)i / 6.0f;
                int y0 = baseY + (int)((tipY - baseY) * frac);
                int w0 = (int)(baseW * (1.0f - frac));
                wideLine(t, cx2 - w0 / 2 - S(1), y0, cx2 + w0 / 2 + S(1), y0 - S(3), S(2), bands[i]);
            }
            break;
        }
        case OutfitId::TINFOIL: {
            // Crumpled by hand, not stamped: the cone is four facets in
            // alternating tones round an apex pushed off centre, on a rolled
            // brim, with a beanie propeller on a stalk turning lazily on
            // top. Greys from the same RGB332 ramp SHARK SUIT uses -- the only
            // cool neutrals the frame buffer has.
            const uint16_t ink = BLACK;
            const uint16_t fDk = t.color565( 73,  73,  85);
            const uint16_t fMd = t.color565(146, 146, 170);
            const uint16_t fLt = t.color565(219, 219, 255);
            const int ax = cx2 + S(2), ay = hy - S(12);
            const int by = hy + S(1);
            const int bx[5] = { cx2 - S(14), cx2 - S(6), cx2 + S(1), cx2 + S(8), cx2 + S(14) };
            const int tx = ax + S(1), ty = ay - S(4);          // propeller hub
            inked(ink, fMd, [&](int ox, int oy, uint16_t c) {
                t.fillTriangle(bx[0] + ox, by + oy, bx[4] + ox, by + oy, ax + ox, ay + oy, c);
                t.fillRoundRect(cx2 - S(16) + ox, hy - S(1) + oy, S(32), S(5), S(2), c);
                t.drawLine(ax + ox, ay + oy, tx + ox, ty + oy, c);
            });
            // Facets: lit from the upper left, with one crumple in the middle
            // catching the light the wrong way, which is what reads as foil.
            const uint16_t fc[4] = { fLt, fMd, fLt, fDk };
            for (uint8_t i = 0; i < 4; i++)
                t.fillTriangle(bx[i], by, bx[i + 1], by, ax, ay, fc[i]);
            t.drawLine(cx2 - S(9), hy - S(2), cx2 - S(4), hy - S(5), fDk);
            t.drawLine(cx2 + S(4), hy - S(1), cx2 + S(6), hy - S(6), fMd);
            t.drawLine(bx[0] + S(3), by - S(2), ax - S(1), ay + S(2), WHITE);
            // The rolled brim: lit along the top, shaded underneath, crinkled.
            t.fillRoundRect(cx2 - S(16), hy - S(1), S(32), S(5), S(2), fMd);
            t.fillRect(cx2 - S(15), hy - S(1), S(30), S(1) > 0 ? S(1) : 1, fLt);
            t.fillRect(cx2 - S(15), hy + S(3), S(30), 1, fDk);
            for (int k = -12; k <= 12; k += 4)
                t.drawFastVLine(cx2 + S(k), hy + S(1), S(2), (k & 4) ? fDk : fLt);
            // The propeller: two blades seen side on, so a turn is each one
            // shrinking to the hub and growing out the other side. Whichever
            // blade is swinging toward us is drawn last, over the other.
            t.drawLine(ax, ay, tx, ty, fDk);
            {
                const float a = (float)(now % 700) / 700.0f * 6.2831853f;
                const float c = cosf(a);
                const int L = S(8), th = S(1) + 1;
                const uint16_t bladeA = t.color565(219, 0, 0), bladeB = t.color565(0, 85, 255);
                auto blade = [&](float dir, uint16_t col) {
                    int half = (int)(fabsf(c) * (float)L * 0.5f);
                    if (half < 1) half = 1;
                    const int bxc = tx + (int)(dir * (c >= 0 ? 1.0f : -1.0f) * (float)half);
                    inked(ink, col, [&](int ox, int oy, uint16_t cc) {
                        t.fillEllipse(bxc + ox, ty + oy, half, th, cc);
                    });
                };
                const bool aFront = sinf(a) > 0.0f;
                blade(aFront ? -1.0f : 1.0f, aFront ? bladeB : bladeA);
                blade(aFront ? 1.0f : -1.0f, aFront ? bladeA : bladeB);
                t.fillCircle(tx, ty, S(1), t.color565(255, 219, 0));
            }
            // A glint that jumps between facets every few seconds.
            const uint32_t gp = now % 4200;
            if (gp < 360) {
                static const int8_t G[3][2] = { {-8, -2}, {-2, -7}, {5, -2} };
                const uint8_t k = (uint8_t)((now / 4200) % 3);
                const int gx = cx2 + S(G[k][0]), gy = hy + S(G[k][1]);
                const int arm = S(2) < 2 ? 2 : S(2);
                t.drawFastHLine(gx - arm, gy, arm * 2 + 1, WHITE);
                t.drawFastVLine(gx, gy - arm, arm * 2 + 1, WHITE);
            }
            break;
        }
        case OutfitId::SHADOW: {
            // A navy ninja: red headband knotted at the side with two tails
            // that flutter, a hood over the lower face with his eyes left in
            // the slit, and a red sash with a throwing star tucked in it.
            const uint16_t ink   = BLACK;
            const uint16_t navy  = t.color565(36, 36, 85);
            const uint16_t navyL = t.color565(73, 73, 170);
            const uint16_t red   = t.color565(219, 0, 0);
            const uint16_t redHi = t.color565(255, 109, 85);
            // Lower-face mask, kept below the shade line so his eyes
            // stay visible -- covers mouth/jaw, not the lenses.
            inked(ink, navy, [&](int ox, int oy, uint16_t c) {
                t.fillRoundRect(cx2 - S(12) + ox, hy + S(14) + oy, S(24), S(10), S(4), c);
            });
            t.drawFastHLine(cx2 - S(9), hy + S(17), S(18), navyL);          // a fold
            // The tails: two strips off the knot, each a travelling wave.
            const float ph = (float)(now % 900) / 900.0f * 6.2831853f;
            const int kx = cx2 + S(14), ky = hy + S(3);
            for (uint8_t pass = 0; pass < 2; pass++)
                for (uint8_t k = 0; k < 2; k++) {
                    const int w = S(2) > 1 ? S(2) : 2;
                    int px = kx, py = ky;
                    for (uint8_t i = 1; i <= 3; i++) {
                        const int nx = kx + S(4) * i;
                        const int ny = ky + S(2 + k * 3) * i / 2 + (int)(sinf(ph - i * 1.3f + k) * (float)S(1) * i / 2);
                        wideLine(t, px, py, nx, ny, pass == 0 ? w + 2 : w, pass == 0 ? ink : red);
                        px = nx; py = ny;
                    }
                }
            // The band and its knot.
            inked(ink, red, [&](int ox, int oy, uint16_t c) {
                t.fillRoundRect(cx2 - S(15) + ox, hy + S(1) + oy, S(30), S(4), S(1), c);
                t.fillCircle(kx + ox, ky + oy, S(2) + 1, c);
            });
            t.drawFastHLine(cx2 - S(13), hy + S(1), S(24), redHi);
            // The sash, knotted at his hip, one end hanging.
            const int th = torsoHalf();
            inked(ink, red, [&](int ox, int oy, uint16_t c) {
                t.fillRect(cx2 - S(th) + ox, hy + S(32) + oy, S(2 * th), S(4), c);
                t.fillTriangle(cx2 + S(5) + ox, hy + S(35) + oy, cx2 + S(9) + ox, hy + S(35) + oy,
                               cx2 + S(8) + ox, hy + S(41) + oy, c);
            });
            t.drawFastHLine(cx2 - S(th) + 1, hy + S(32), S(2 * th) - 2, redHi);
            // The throwing star, tucked in on the other side.
            const uint16_t steel = t.color565(182, 182, 170);
            const int sx = cx2 - S(6), sy = hy + S(34), sr = S(2) + 1;
            t.fillTriangle(sx - sr, sy, sx + sr, sy, sx, sy - sr - 1, steel);
            t.fillTriangle(sx - sr, sy, sx + sr, sy, sx, sy + sr + 1, steel);
            t.fillCircle(sx, sy, 1, ink);
            break;
        }
        case OutfitId::PLUMBER:
        case OutfitId::TALLBRO: {
            // A cap with a dome, a bill and a letter on the front, a moustache
            // in two lobes, and white gloves on the hands themselves. The
            // overalls are on his torso and legs, in drawBody(), so his arms
            // go over them. TALL BRO's cap is the taller of the two.
            const bool tall = (outfit == OutfitId::TALLBRO);
            const uint16_t ink   = BLACK;
            const uint16_t cap   = tall ? t.color565(  0, 182,   0) : t.color565(219,   0,   0);
            const uint16_t capDk = tall ? t.color565(  0, 109,   0) : t.color565(146,   0,   0);
            const uint16_t capHi = tall ? t.color565(109, 255,  85) : t.color565(255, 109,  85);
            const int top = tall ? S(9) : S(7);
            // The corner radius is held to half the dome's height: a round
            // rect asked for more than that folds its corners over each other
            // and the cap came out as two lobes with a dip between them.
            const int domeH = top + S(4);
            const int domeR = S(7) < domeH / 2 ? S(7) : domeH / 2;
            inked(ink, cap, [&](int ox, int oy, uint16_t c) {
                t.fillRoundRect(cx2 - S(15) + ox, hy - top + oy, S(30), domeH, domeR, c);
                t.fillEllipse(cx2 + S(3) + ox, hy + S(3) + oy, S(13), S(2) + 1, c);
            });
            t.fillRect(cx2 - S(14), hy + S(1), S(28), S(2), capDk);              // under the dome
            t.fillEllipse(cx2 - S(8), hy - top + S(3), S(3), S(1) + 1, capHi);   // light on it
            t.fillEllipse(cx2 + S(3), hy + S(3), S(13), S(2) + 1, capDk);        // the bill
            t.drawFastHLine(cx2 - S(9), hy + S(2), S(22), capHi);
            // The emblem: an S, for Squachy.
            // Inside the dome, not riding its top edge: sized off the dome's
            // own height so it never pokes out above the cap.
            const int er = S(3) + 1;
            const int ex = cx2 - S(1), ey = hy - top + er + S(1) + 1;
            t.fillCircle(ex, ey, er + 1, ink);
            t.fillCircle(ex, ey, er, WHITE);
            const int p = (S(2) * 3) / 5;
            if (p >= 1) capGlyph(t, ex - (3 * p) / 2, ey - (5 * p) / 2, p, cap);
            // The moustache, two lobes over the top of his mouth.
            const uint16_t tash = t.color565(73, 36, 0);
            inked(ink, tash, [&](int ox, int oy, uint16_t c) {
                t.fillEllipse(cx2 - S(4) + ox, hy + S(15) + oy, S(5), S(2), c);
                t.fillEllipse(cx2 + S(4) + ox, hy + S(15) + oy, S(5), S(2), c);
            });
            // Gloves on the hands drawBody() published, so they follow a wave.
            const uint16_t crease = t.color565(182, 182, 170);
            const int gr = S(4);
            t.fillCircle(s_armL1x, s_armL1y, gr + 1, ink);
            t.fillCircle(s_armR1x, s_armR1y, gr + 1, ink);
            t.fillCircle(s_armL1x, s_armL1y, gr, WHITE);
            t.fillCircle(s_armR1x, s_armR1y, gr, WHITE);
            t.drawFastHLine(s_armL1x - gr / 2, s_armL1y + gr / 3, gr, crease);
            t.drawFastHLine(s_armR1x - gr / 2, s_armR1y + gr / 3, gr, crease);
            break;
        }
        case OutfitId::SPACE: {
            // Was two single-pixel circle outlines, one of them BG blended 60
            // toward blue -- which on an 8-bit panel is very nearly the
            // background itself. A hairline ring round a brown head is a
            // smudge at this size; what actually makes a circle read as a
            // sphere is a thick rim plus a highlight that follows the curve.
            const int cy = hy + S(10), r = S(19);
            const uint16_t steel = t.color565(226, 230, 240);
            // ---- the suit on his arms and feet, before the helmet ----------
            // Along wherever each arm really is, like the parka's sleeves, and
            // started a little way down the arm so the round end does not
            // poke up over the corners of his jaw inside the helmet.
            {
                const uint16_t suit  = t.color565(219, 219, 255);
                const uint16_t suitD = t.color565(146, 146, 170);
                const uint16_t glove = t.color565(255, 146, 0);
                const int aw = S(8);
                for (int8_t sg = -1; sg <= 1; sg += 2) {
                    const int x0 = sg < 0 ? s_armL0x : s_armR0x, y0 = sg < 0 ? s_armL0y : s_armR0y;
                    const int x1 = sg < 0 ? s_armL1x : s_armR1x, y1 = sg < 0 ? s_armL1y : s_armR1y;
                    const int sx = x0 + (x1 - x0) / 5, sy = y0 + (y1 - y0) / 5;
                    wideLine(t, sx, sy, x1, y1, aw + 2, BLACK);
                    wideLine(t, sx, sy, x1, y1, aw, suit);
                    // A seam ring at the elbow, the suit's one mark.
                    const int mx = (sx + x1) / 2, my = (sy + y1) / 2;
                    wideLine(t, mx - aw / 2, my, mx + aw / 2, my, S(1) > 0 ? S(1) : 1, suitD);
                    t.fillCircle(x1, y1, S(4) + 1, BLACK);
                    t.fillCircle(x1, y1, S(4), glove);
                }
                const int fx[2] = { s_footLx, s_footRx }, fy[2] = { s_footLy, s_footRy };
                for (int k = 0; k < 2; k++) {
                    inked(BLACK, suitD, [&](int ox, int oy, uint16_t c) {
                        t.fillRoundRect(fx[k] + ox, fy[k] - S(2) + oy, S(12), S(8), S(2), c);
                    });
                    t.fillRect(fx[k] + 1, fy[k] - S(2), S(12) - 2, S(2), suit);
                    t.fillRect(fx[k] + 1, fy[k] + S(4), S(12) - 2, S(1) > 0 ? S(1) : 1, BLACK);
                }
            }
            const int ar = r - S(4);
            // The whole highlight drifts, as though he were turning under a
            // light. One sinf a frame, the same as his own idle arm sway.
            const float d  = sinf((float)(now % 16000) / 16000.0f * 6.2831853f) * 0.20f;
            const float a0 = 3.14159265f * (1.06f + d);
            const float a1 = 3.14159265f * (1.48f + d);
            int px = cx2 + (int)(cosf(a0) * ar), py = cy + (int)(sinf(a0) * ar);
            for (uint8_t i = 1; i <= 8; i++) {
                const float a = a0 + (a1 - a0) * (float)i / 8.0f;
                const int nx = cx2 + (int)(cosf(a) * ar), ny = cy + (int)(sinf(a) * ar);
                wideLine(t, px, py, nx, ny, S(2) < 2 ? 2 : S(2), WHITE);
                px = nx; py = ny;
            }
            // Rim last, so the arc cannot spill over it. A dark line inside
            // it now too, so the rim reads as a thick metal ring rather than
            // three pale hairlines.
            for (uint8_t k = 0; k < 3; k++) t.drawCircle(cx2, cy, r - k, steel);
            t.drawCircle(cx2, cy, r - 3, t.color565(73, 73, 85));
            t.drawCircle(cx2, cy, r + 1, BLACK);
            // A second, smaller glint low on the far side: two lights read as
            // curved glass, one reads as a sticker.
            t.fillCircle(cx2 + (r * 55) / 100, cy + (r * 45) / 100, S(1), WHITE);
            t.fillRoundRect(cx2 - S(torsoHalf() + 2), hy + S(21), S(2 * torsoHalf() + 4), S(5), S(2), t.color565(186, 190, 202));
            t.fillRect(cx2 - S(torsoHalf() + 2), hy + S(21), S(2 * torsoHalf() + 4), 1, t.color565(244, 244, 252));
            // The glint: a blunt plus rather than a tapered sparkle, because
            // square arms survive being six pixels wide and tapered ones do not.
            const int mx = cx2 + (int)(cosf(a0) * ar), my = cy + (int)(sinf(a0) * ar);
            const int arm = S(3) < 2 ? 2 : S(3), th = S(2) < 2 ? 2 : S(2);
            t.fillRect(mx - arm, my - th / 2, arm * 2, th, WHITE);
            t.fillRect(mx - th / 2, my - arm, th, arm * 2, WHITE);
            break;
        }
        case OutfitId::BLUEBLUR: {
            uint16_t blue = VAPOR_BLUE;
            // Two swept-back quills, deliberately -- there used to be a
            // third between them with its apex at hy - S(18), pointing
            // straight up. It read as an antenna rather than a quill:
            // it was the only one not swept back, it sat off the
            // midline (cx+2..cx+10 rather than centered), and it
            // overlapped the right quill, which starts at cx+6, leaving
            // a visible seam where they crossed. The silhouette is the
            // whole point of the homage, and it's cleaner with two.
            //
            // Those two live BEHIND his head now, with more beside them: see
            // the quills in drawBody(). Drawn here, on top, their bases had to
            // stop short of his lenses and they came out stubby.
            (void)blue;
            // White gloves -- a core, consistent trait across every
            // era of the design this homages, and the one thing this
            // outfit was missing that actually sells the reference.
            // On the hands THEMSELVES (s_armL1x and friends, published by
            // drawBody()), not at the resting hand position: fixed gloves
            // stayed at his sides while the arms went up, and every raised
            // pose showed two white balls floating where his hands had been.
            t.fillCircle(s_armL1x, s_armL1y, S(4) + 1, BLACK);
            t.fillCircle(s_armR1x, s_armR1y, S(4) + 1, BLACK);
            t.fillCircle(s_armL1x, s_armL1y, S(4), WHITE);
            t.fillCircle(s_armR1x, s_armR1y, S(4), WHITE);
            // Shoes on the FEET, which stay planted when the body sinks into a
            // crouch or a bow: at a fixed distance below hy they slid down
            // and hung in the air under his soles.
            //
            // Outlined, with the white strap, gold buckle and white sole that
            // make them those shoes rather than two red ovals.
            {
                const uint16_t shoe = t.color565(219, 0, 0), shoeHi = t.color565(255, 109, 85);
                const int fx[2] = { s_footLx, s_footRx }, fy[2] = { s_footLy, s_footRy };
                for (int k = 0; k < 2; k++) {
                    inked(BLACK, shoe, [&](int ox, int oy, uint16_t c) {
                        t.fillRoundRect(fx[k] - S(1) + ox, fy[k] - S(1) + oy, S(14), S(7), S(3), c);
                    });
                    t.drawFastHLine(fx[k] + S(1), fy[k] - S(1), S(8), shoeHi);
                    t.fillRect(fx[k] - S(1) + 1, fy[k] + S(4), S(14) - 2, S(2), WHITE);
                    t.fillRect(fx[k] + S(5), fy[k] - S(1), S(3), S(5), WHITE);
                    t.fillRect(fx[k] + S(6), fy[k] + S(1), S(1) > 0 ? S(1) : 1, S(2), t.color565(255, 219, 0));
                }
                // A puff of dust kicked up behind whichever foot just left the
                // ground, while he walks.
                if (m == Mood::WALK) {
                    const uint32_t wp = now % 400;
                    const int k = wp < 200 ? 0 : 1;
                    const int age = (int)(wp % 200);
                    const int pr = S(1) + age * S(2) / 200 + 1;
                    const int px = fx[k] + (k == 0 ? -S(2) : S(13)) - (k == 0 ? age * S(3) / 200 : -age * S(3) / 200);
                    t.fillCircle(px, fy[k] + S(4), pr, t.color565(182, 182, 170));
                }
            }
            break;
        }
        case OutfitId::CAPTAIN: {
            // A pirate's hat, front on: the crown behind, the front brim turned
            // up into two horns and a hump, gold braid along its edge and the
            // skull and crossbones in the middle. The old one was two black
            // triangles, which from across a room was a tent.
            const uint16_t ink    = BLACK;
            const uint16_t felt   = t.color565( 36,  36,  85);
            const uint16_t feltHi = t.color565( 73,  73, 170);
            const uint16_t gold   = t.color565(255, 219,   0);
            // The brim's outline, left to right along the top.
            static const int8_t B[9][2] = { {-17, 2}, {-19, -11}, {-9, -3}, {-5, -7}, {0, -8},
                                            {5, -7}, {9, -3}, {19, -11}, {17, 2} };
            auto brim = [&](int ox, int oy, uint16_t c) {
                const int fx = cx2 + ox, fy = hy + S(1) + oy;
                for (uint8_t i = 0; i < 9; i++) {
                    const uint8_t j = (uint8_t)((i + 1) % 9);
                    t.fillTriangle(fx, fy, cx2 + S(B[i][0]) + ox, hy + S(B[i][1]) + oy,
                                   cx2 + S(B[j][0]) + ox, hy + S(B[j][1]) + oy, c);
                }
            };
            inked(ink, felt, [&](int ox, int oy, uint16_t c) {
                t.fillEllipse(cx2 + ox, hy - S(5) + oy, S(12), S(9), c);
                brim(ox, oy, c);
            });
            t.fillEllipse(cx2 - S(4), hy - S(11), S(4), S(1) + 1, feltHi);   // light on the crown
            brim(0, 0, felt);
            for (uint8_t i = 1; i < 7; i++)
                wideLine(t, cx2 + S(B[i][0]), hy + S(B[i][1]), cx2 + S(B[i + 1][0]), hy + S(B[i + 1][1]),
                         S(1) + 1, gold);
            // Crossbones, then the skull over them.
            const int kx = cx2, ky = hy - S(3);
            const int bw = S(1) + 1;
            wideLine(t, kx - S(5), ky - S(3), kx + S(5), ky + S(3), bw, WHITE);
            wideLine(t, kx + S(5), ky - S(3), kx - S(5), ky + S(3), bw, WHITE);
            t.fillCircle(kx, ky - S(1), S(3), WHITE);
            t.fillRect(kx - S(2), ky + S(1), S(4), S(2), WHITE);
            const int ep = S(1) > 0 ? S(1) : 1;
            t.fillRect(kx - S(2), ky - S(2), ep, ep, ink);
            t.fillRect(kx + S(1), ky - S(2), ep, ep, ink);
            // Eye patch strap only, not a filled patch, so the shades
            // underneath stay visible.
            wideLine(t, cx2 - S(11), hy + S(5), cx2 + S(14), hy + S(3), S(2), BLACK);
            break;
        }
        default: break;
    }
}

// Squachy's base design is ~68px tall (crest to shadow) at scale 1.0.
static const int BASE_HEIGHT = 68;

// ...and only 53 of those 68 units are HIM. Measured, not guessed: with the
// shadow off, his lowest painted row on CLEAR was 154 against a head anchor
// of 37 at scale 2.206, which is 53.0 units exactly. The shadow's own bottom
// edge sits at 66, and that is what the remaining reserve was buying.
//
// 56, which is his 53 plus three of the six the DUCK needs -- so the crouch
// is deliberately allowed to overrun, and it is worth being explicit about
// what that buys and what it costs.
//
// His size on any screen reduces to one identity:
//     scale = (bandBottom - TOP_MARGIN) / (this + CREST_REACH)
// Everything else -- bubble row, headroom, per-outfit drops -- trades
// against itself and cancels. So there are exactly three places another
// percent can come from, and two of them are bad: crowding his crest
// off the top, or pushing the band down into the counter text full time.
//
// This is the third. Reserving the whole crouch cost 5% of him ALL the time
// to protect a pose that lasts 900ms, cannot repeat for 30 seconds (see the
// cooldown by s_duckUntil), and only fires when a flying toaster passes his
// head -- one background out of eleven. Mid-crouch his soles now reach about
// six rows into the first counter line, behind cyan text that is drawn after
// him and covers it. That is a fair price for a tenth of him at rest.
//
// Dividing by this instead of BASE_HEIGHT is the whole of "he got bigger".
// Nothing else changed: he is still sized from the band he is handed, his
// feet still land on the bottom of it, and every screen gets it at once.
static const int BASE_HEIGHT_NOSHADOW = 56;

// How much of him above the head anchor is GUARANTEED to be on screen.
//
// Two numbers are in play and they are deliberately not the same one. His
// crest reaches 14 units above the anchor; his raised waving HAND reaches
// about 16 (measured: arm top at row 3 with the anchor at 37 at scale
// 2.206, so 15.4 rounded up).
//
// Neither is what we reserve for now that the centre spike is gone. The
// cowlick's tall tuft is the highest thing he draws, at 80% of nine units,
// so 7.2 -- and 8 here rounds that up rather than down, because a guarantee
// that is a fraction short is not a guarantee.
//
// This was 4 for a while, back when a 14-unit spike stood over everything
// and reserving for it was hopeless, so the rule was to protect only the
// small side tufts and let the spike run off the top. With the spike gone
// there is nothing left worth sacrificing: 8 covers his whole silhouette,
// and it costs almost nothing because the top is no longer where he runs
// out of room.
//
// Still not the waving hand at 16. That one stays free to clip -- it is a
// gesture, not the character, and reserving for it costs real size.
//
// The bob is on top of all of this and always has been: a BOUNCE apex lifts
// him 9 units, so his tufts leave the screen at the top of a hop at every
// scale this screen has ever used.
static const int CREST_REACH = 8;
static const int TOP_MARGIN  = 2;   // rows of air we insist on above the tufts

// And how far above the anchor each COSTUME reaches, in the same units.
//
// Several of them are far taller than he is. The wolf's ears and the
// unicorn's horn are more than three times his own cowlick, and at the size
// he is drawn now they ran a long way off the top of the screen.
//
// Read off the drawing code rather than guessed: WOLFPELT's ear tips are at
// hy - S(26), UNICORN's horn tip at hy - S(24), TINFOIL's cone and CAPTAIN's
// tricorn both at hy - S(16), PARKA's hood shell at hy + S(8) - S(23) which
// is 15 plus its two-pixel rim, and SPACE's helmet at hy + S(10) - S(19).
// Everything else tops out at his own cowlick, which is what the default is.
static int outfitReach(OutfitId o) {
    switch (o) {
        case OutfitId::WOLFPELT: return 26;
        case OutfitId::UNICORN:  return 24;
        case OutfitId::TINFOIL:  return 18;   // the antenna bead
        case OutfitId::CAPTAIN:  return 16;
        case OutfitId::TALLBRO:  return 10;
        case OutfitId::TANOOKI:  return 10;   // the ear tips
        case OutfitId::PARKA:    return 16;
        case OutfitId::SHARK:    return 18;
        case OutfitId::SPACE:    return  9;
        default:                 return CREST_REACH;
    }
}

// Where the Legend top hat's brim rests, in units above the head anchor: on
// top of whatever is highest on him. Read off drawOutfit() the same way
// outfitReach() is. On the costumes that are already a hat it goes ON the
// hat -- funnier than leaving it off, as long as it is seated, not floating.
static const int TOP_HAT_H = 11;   // brim bottom to crown top
static int topHatSeat(OutfitId o) {
    switch (o) {
        // Both points are one pixel wide at the very top. 9 sinks the brim
        // down onto the cone and the tricorn, far enough that it is plainly
        // sitting on them rather than balanced on a pinpoint.
        case OutfitId::TINFOIL:  return  9;   // pushed down onto the cone
        case OutfitId::CAPTAIN:  return  9;   // ...and onto the tricorn
        case OutfitId::PARKA:    return 14;   // on the hood
        case OutfitId::SHARK:    return 13;   // on the shark hood, under the fin
        case OutfitId::SPACE:    return  9;   // on the dome
        case OutfitId::WOLFPELT: return  7;   // on the pelt, between the ears
        case OutfitId::TALLBRO:  return  9;   // on the cap
        case OutfitId::PLUMBER:  return  7;
        case OutfitId::VOIDEYE:  return  4;   // perched on the sphere
        case OutfitId::UNICORN:  return  2;   // the horn comes up through it
        default:                 return  3;   // on his hair, over the tuft tips
    }
}

bool hasTopHat() { return currentStage() == GrowthStage::LEGEND; }

// How far past the top edge a costume is allowed to go, as a percentage of
// his drawn height. Not zero: seating the horn and the ears completely
// would cost a third of his size on those two outfits and he would visibly
// shrink whenever you put a hat on him. A tenth is enough that they read as
// running past the edge rather than as being chopped off.
static const int OVERFLOW_PCT = 10;

// Draws Squachy at an already-animated anchor (hy = head-top Y for this
// exact frame). Bob is computed once in tick() so it can also drive the
// dirty-rect clear that runs before this is called.
// ownsBubble: whether the ONE global speech bubble (bubbleText/bubbleUntil)
// belongs to the body being drawn. True for our own Squachy, who is the only
// one who can put a line in it. False for every cameo -- see drawWaving().
static void drawBody(TFT_eSPI& t, int cx, int hy, int headTopY, uint32_t now, Mood m, float scale,
                     bool forceTalking = false, bool ownsBubble = true) {
    auto S = [scale](int v) { return (int)(v * scale); };
    int cx2 = cx;

    // An emote's pose that moves the whole of him rather than only his arms:
    // sinking into a crouch or a bow, hanging his head, throwing it back to
    // howl, leaning back on a rope. His feet stay planted -- the legs below
    // shorten by exactly what the body sank.
    const VisitPose act = (m == Mood::ACT) ? (VisitPose)s_actPose : VisitPose::NONE;
    int crouch = 0, actHead = 0;
    switch (act) {
        case VisitPose::CROUCH: crouch = S(9); actHead = S(2); break;
        case VisitPose::BOW:    crouch = S(3); actHead = S(7); break;
        case VisitPose::SAD:    actHead = S(3); break;
        case VisitPose::HOWL:   actHead = -S(3); break;
        case VisitPose::PULL:   cx2 -= s_reachDir * S(4); break;
        default: break;
    }
    hy += crouch;

    using namespace Theme;

    // Permanent growth-stage re-tint first (see currentStage()), then
    // the rare temporary shimmer (see tick()'s idle branch) overrides
    // it for a few seconds when that's active — a Legend-stage device
    // still gets the full rainbow flourish, it just settles back to
    // gold afterward instead of plain brown.
    uint16_t furMain = FUR_MAIN, furLight = FUR_LIGHT;
    switch (currentStage()) {
        case GrowthStage::TRACKER:
            furMain  = blend(FUR_MAIN, CYAN, 50);
            furLight = blend(FUR_LIGHT, CYAN, 50);
            break;
        case GrowthStage::VETERAN:
            furMain  = blend(FUR_MAIN, WHITE, 90);
            furLight = blend(FUR_LIGHT, WHITE, 90);
            break;
        case GrowthStage::LEGEND:
            furMain  = blend(FUR_MAIN, AMBER, 110);
            furLight = blend(FUR_LIGHT, VAPOR_YELLOW, 110);
            break;
        default: break;
    }
    // Some outfits recolor the fur itself rather than just adding an
    // accessory on top -- has to happen here, before he's actually
    // drawn, not as a post-hoc overlay in drawOutfit() (there's no
    // cheap way to "repaint" an already-drawn silhouette a different
    // color without redrawing every shape that used the old one).
    OutfitId outfitNow = currentOutfit();
    if (outfitNow == OutfitId::UNICORN) {
        // Baby-blue/baby-pink two-tone -- furMain (body fill) and
        // furLight (highlights/outlines) already alternate across
        // every shape he's made of, so giving them distinct pastels
        // reads as a fade across his whole body without needing a
        // real per-pixel gradient.
        furMain  = blend(WHITE, VAPOR_BLUE, 130);
        furLight = blend(WHITE, VAPOR_PINK, 110);
    } else if (outfitNow == OutfitId::BLUEBLUR) {
        furMain  = blend(VAPOR_BLUE, BLACK, 20);
        furLight = blend(VAPOR_BLUE, WHITE, 70);
    } else if (outfitNow == OutfitId::TANOOKI) {
        // The suit: orange-brown all over, head included, which is what makes
        // his head the hood. Lighter on the arms and the hood's rim, the way
        // the two fur tones alternate on plain Squachy.
        furMain  = t.color565(219, 109, 0);
        furLight = t.color565(255, 146, 0);
    } else if (outfitNow == OutfitId::PARKA) {
        // One flat orange everywhere -- arms, legs and torso -- so the coat
        // only has to draw its own outline and hem rather than repaint him.
        furMain  = t.color565(255, 138, 26);
        furLight = t.color565(255, 138, 26);
    } else if (outfitNow == OutfitId::VOIDEYE) {
        // Deep space, but chosen off the RGB332 ramp rather than nudged toward
        // it. The first pass used a true navy and he vanished outright: 8-bit
        // blue has four levels, and everything below the first one is black.
        furMain  = t.color565(48, 44, 96);
        furLight = t.color565(96, 88, 180);
    } else if (outfitNow == OutfitId::SHADOW) {
        // Mostly black -- furLight stays a touch lighter than pure
        // black purely so edges/highlights (ears, arm outlines) don't
        // vanish into a single flat silhouette.
        //
        // Navy now, not near-black: black on black lost the whole shape of
        // him, and the navy keeps it while still reading as a ninja at night.
        furMain  = t.color565(36, 36, 85);
        furLight = t.color565(73, 73, 170);
    }
    // PARKA sits out the shimmer. Everything of him that shows is either the
    // coat, which is a fixed orange this cannot reach, or his legs and arms,
    // which are recoloured to match it -- so a rainbow here repaints only the
    // legs and leaves them a different colour from the coat above them.
    if (s_legendary && now < s_legendaryUntil && outfitNow != OutfitId::PARKA) {
        float ph = (float)(now % 900) / 900.0f;
        furMain  = blend(CYAN, VAPOR_PINK, (uint16_t)(ph * 256.0f));
        furLight = blend(VAPOR_PINK, VAPOR_PURPLE, (uint16_t)(ph * 256.0f));
    }

    // ---- CHROME WING's wings ----------------------------------------------
    // Behind him, for the same reason the tail below is: drawOutfit() runs
    // last, so drawn there they came out ON TOP of him -- a pair of wings
    // lying across his chest rather than a pair he is wearing.
    //
    if (outfitNow == OutfitId::CHROMEWING) {
        // BROAD: the flock's own wing routine, not an imitation of it.
        // Same curved lobe, blunt tip, two-tone shading and feather
        // divisions the toasters wear, just wider in the chord because
        // a narrow wing thins out to nothing at CLEAR-screen scale.
        //
        // The pair is made by mirroring: angle becomes (180 - a) and
        // the curl is negated. drawWing() picks its shaded edge by
        // which one is lower on screen, so both wings get the shadow
        // underneath instead of one of them looking flipped.
        const uint16_t wh  = TFT_WHITE;
        const uint16_t wh2 = t.color565(214, 214, 228);
        const uint16_t wh3 = t.color565(150, 150, 172);
        // Wings rest, then break into a short burst of three beats
        // every few seconds, decaying so the last one is smallest.
        // Constant gentle motion reads as a hover; a bird at rest that
        // occasionally beats reads as a bird.
        //
        // Both wings take the SAME beat value. They are mirrored, so
        // the lift is added to opposite base angles -- which is what
        // makes one sign move them together rather than apart.
        static uint32_t flapAt = 0, flapNext = 0;
        if (now >= flapNext) {
            flapAt   = now;
            flapNext = now + 3200u + (uint32_t)random(0, 4200);
        }
        const uint32_t fAge = now - flapAt;
        float beat = 0.0f;
        if (fAge < 900u) {
            const float fp = (float)fAge / 900.0f;
            beat = sinf(fp * 3.0f * 6.2831853f) * (1.0f - fp);
        }
        // Anchored to hy, his shoulders, rather than the head anchor
        // drawOutfit() used to pass in. Wings grow from a back, so they
        // should not ride his head's squash-and-stretch.
        const float sy   = (float)(hy + S(20));
        const float len  = (float)S(25);
        //
        // CHROME, which plain white never said. A mirror at this size is hard
        // bands, not a gradient: a bright wing with a dark reflection along
        // its lower half and a black keyline, then a narrower white band laid
        // down its middle. On every flap that band retracts to the shoulder
        // and runs back out to the tip, which is a shine sliding along it.
        (void)wh; (void)wh2; (void)wh3;
        const uint16_t cBr = t.color565(219, 219, 255);
        const uint16_t cMd = t.color565(146, 146, 170);
        const uint16_t cDk = t.color565( 73,  73,  85);
        const float shine = (fAge < 900u) ? 0.25f + 0.75f * (float)fAge / 900.0f : 1.0f;
        Theme::drawWing(t, (float)(cx2 - S(13)), sy, len,
                        218.0f, -beat, 0.50f, 3, cMd, BLACK, -0.55f, 26.0f, cDk);
        Theme::drawWing(t, (float)(cx2 + S(13)), sy, len,
                        -38.0f, beat, 0.50f, 3, cMd, BLACK, 0.55f, 26.0f, cDk);
        Theme::drawWing(t, (float)(cx2 - S(13)), sy, len * shine,
                        218.0f, -beat, 0.22f, 0, WHITE, WHITE, -0.55f, 26.0f, cBr);
        Theme::drawWing(t, (float)(cx2 + S(13)), sy, len * shine,
                        -38.0f, beat, 0.22f, 0, WHITE, WHITE, 0.55f, 26.0f, cBr);
    }

    // ---- WOLF PELT's hide down his back ------------------------------------
    // Behind him for the same reason as the wings and the tanooki's tail.
    if (outfitNow == OutfitId::WOLFPELT) {
        // Flares out from his shoulders to a ragged hem at his knees, so it
        // shows past his arms either side, the way a hide hangs off a back.
        const uint16_t pD = blend(BLACK, WHITE, 58), pM = blend(BLACK, WHITE, 96);
        static const int8_t C[9][2] = { {-12, 20}, {12, 20}, {22, 44}, {17, 41}, {12, 47},
                                        {0, 44}, {-12, 47}, {-17, 41}, {-22, 44} };
        const int fx = cx2, fy = hy + S(32);
        auto cape = [&](int ox, int oy, uint16_t c) {
            for (uint8_t i = 0; i < 9; i++) {
                const uint8_t j = (uint8_t)((i + 1) % 9);
                t.fillTriangle(fx + ox, fy + oy, cx2 + S(C[i][0]) + ox, hy + S(C[i][1]) + oy,
                               cx2 + S(C[j][0]) + ox, hy + S(C[j][1]) + oy, c);
            }
        };
        inked(BLACK, pD, cape);
        for (int8_t sg = -1; sg <= 1; sg += 2)
            for (int k = 0; k < 3; k++)
                t.drawLine(cx2 + sg * S(15 + k * 2), hy + S(28 + k * 3), cx2 + sg * S(17 + k * 2), hy + S(34 + k * 3), pM);
    }

    // ---- TANOOKI's tail ---------------------------------------------------
    // First thing drawn, before his own silhouette: behind him, the way a tail
    // actually attaches. Drawn in drawOutfit() (which runs last) it lay across
    // his leg like a stole.
    //
    // Shape is a cubic spine with the radius falling late -- nearly full width
    // for two thirds of the length, then away quickly, which is what reads as
    // poofy. A linear taper reads as a cone however fat you start it.
    //
    // Segments with round caps rather than a row of discs: gap-free by
    // construction, a third of the draw calls, and nothing can detach at the
    // tip where the radius drops faster than the spacing. The fur texture is a
    // small per-segment wobble in the RADIUS, deliberately not extra circles
    // hung off the side -- those stick out past the silhouette and read as
    // debris rather than fur.
    if (outfitNow == OutfitId::TANOOKI) {
        // The suit's orange with dark brown rings, as in the reference.
        const uint16_t tcream = t.color565(219, 109, 0);
        const uint16_t tdark  = t.color565(109, 36, 0);
        const float TAU = 6.2831853f;

        // A travelling S: the displacement is perpendicular to the spine and
        // its phase moves along the tail, so the middle leans out while the
        // tip is still coming back. A single swing bends the whole thing one
        // way at once and reads as a windscreen wiper.
        const float sPhase = (float)(now % 3400) / 3400.0f * TAU;

        // ...with the occasional decaying double flick over the top, timed off
        // a hashed slot so it never lands on a beat you can predict. Same idea
        // as his blink schedule.
        float fl = 0.0f;
        {
            const uint32_t SLOT = 5200;
            const uint32_t slot = now / SLOT;
            uint32_t hsh = slot * 2654435761u;
            hsh ^= hsh >> 15; hsh *= 2246822519u; hsh ^= hsh >> 13;
            const uint32_t f0 = slot * SLOT + (hsh % (SLOT - 800));
            if (now >= f0 && now < f0 + 620) {
                const float k = (float)(now - f0) / 620.0f;
                fl = sinf(k * 12.566371f) * (1.0f - k);
            }
        }

        // PLUME at 80%, scaled about its own base so it still leaves his hip
        // in the same place and only gets shorter.
        static const float PL[4][2] = {
            { 10.0f, 42.0f }, { 24.4f, 45.2f }, { 38.8f, 26.0f }, { 32.4f, 8.4f }
        };
        const uint8_t TN = 18;
        int px[TN], py[TN];
        for (uint8_t i = 0; i < TN; i++) {
            const float u = (float)i / (float)(TN - 1);
            const float v = 1.0f - u;
            const float bx = v*v*v*PL[0][0] + 3*v*v*u*PL[1][0] + 3*v*u*u*PL[2][0] + u*u*u*PL[3][0];
            const float by = v*v*v*PL[0][1] + 3*v*v*u*PL[1][1] + 3*v*u*u*PL[2][1] + u*u*u*PL[3][1];
            // tangent, for the normal the wave pushes along
            const float uu = (u + 0.02f > 1.0f) ? 1.0f : u + 0.02f;
            const float w2 = 1.0f - uu;
            const float qx = w2*w2*w2*PL[0][0] + 3*w2*w2*uu*PL[1][0] + 3*w2*uu*uu*PL[2][0] + uu*uu*uu*PL[3][0];
            const float qy = w2*w2*w2*PL[0][1] + 3*w2*w2*uu*PL[1][1] + 3*w2*uu*uu*PL[2][1] + uu*uu*uu*PL[3][1];
            float dx = qx - bx, dy = qy - by;
            float len = sqrtf(dx*dx + dy*dy);
            if (len < 0.0001f) len = 1.0f;
            // amplitude grows along the length so the base stays planted.
            // u^1.4-ish without a powf on the draw path.
            const float amp = u * (0.4f + 0.6f * u);
            const float d = sinf(u * TAU - sPhase) * 4.0f * amp + fl * 5.0f * u * u;
            px[i] = cx2 + (int)((bx + (-dy / len) * d) * scale);
            py[i] = hy  + (int)((by + ( dx / len) * d) * scale);
        }
        for (uint8_t j = 0; j + 1 < TN; j++) {
            const float u = (float)j / (float)(TN - 1);
            // radius falls late; 0.8 for the 80% scale
            float rr2 = (7.0f * (1.0f - u * u * (0.35f + 0.65f * u)) + 2.2f) * 0.8f;
            // fur wobble, on the radius only -- stays inside the silhouette
            uint32_t hj = (j * 2654435761u) ^ 0x9E3779B9u;
            hj ^= hj >> 13; hj *= 1274126177u; hj ^= hj >> 16;
            rr2 *= 0.90f + (float)(hj & 255u) / 255.0f * 0.20f;
            int w = (int)(rr2 * 2.0f * scale);
            if (w < 2) w = 2;
            wideLine(t, px[j], py[j], px[j+1], py[j+1], w,
                           ((j / 3) & 1) ? tdark : tcream);
        }
    }

    // Shadow (fixed, doesn't bob)
    // Silhouette keyline helpers. Drawn as slightly expanded copies UNDER
    // each shape, so no per-pose outline maths is needed -- whatever the
    // limb does, its outline does too.
    const uint16_t keyCol = (outfitNow == OutfitId::PARKA)
                            ? t.color565(138, 68, 8)      // a seam, not an edge
                            : blend(FUR_DARK, BLACK, 150);
    const int kb = (S(1) < 1) ? 1 : S(1);          // rim thickness, min 1px
    auto keyRR = [&](int x, int y, int w, int h, int r) {
        if (SQUACHY_KEYLINE) t.fillRoundRect(x - kb, y - kb, w + 2 * kb, h + 2 * kb, r, keyCol);
    };
    auto keyR = [&](int x, int y, int w, int h) {
        if (SQUACHY_KEYLINE) t.fillRect(x - kb, y - kb, w + 2 * kb, h + 2 * kb, keyCol);
    };
    auto keyW = [&](int x0, int y0, int x1, int y1, int w) {
        if (SQUACHY_KEYLINE) wideLine(t, x0, y0, x1, y1, w + 2 * kb, keyCol);
    };
    // Crown spikes. They poke above the head's own keyline, so without
    // this they were the one part of the silhouette left unoutlined.
    //
    // Sideways and UPWARD only -- never down. A spike's base sits flush on
    // the skull, so growing the triangle downward as well would lay a dark
    // bar across the top of his head where there is no silhouette edge to
    // trace. Vertices at or below the centroid keep their y exactly.
    auto keyT = [&](int x1, int y1, int x2, int y2, int x3, int y3) {
        if (!SQUACHY_KEYLINE) return;
        const int gx = (x1 + x2 + x3) / 3, gy = (y1 + y2 + y3) / 3;
        auto ox = [&](int v) { return v + (v > gx ? kb : (v < gx ? -kb : 0)); };
        auto oy = [&](int v) { return v < gy ? v - kb : v; };
        t.fillTriangle(ox(x1), oy(y1), ox(x2), oy(y2), ox(x3), oy(y3), keyCol);
    };

    // ---- silhouette keyline -------------------------------------------
    // A dark outline one pixel proud of every major mass. Squachy's own
    // darkest brown is close to several of the backgrounds -- he sinks
    // into the fire and the tunnel -- and this is the cheapest thing that
    // fixes him everywhere at once rather than per background.
    //
    // Drawn as expanded copies of the shapes underneath rather than as
    // stroked outlines: the arms and legs move, and a real outline would
    // have to be recomputed per pose, where an oversized copy just works.
    // Flip SQUACHY_KEYLINE to false to take the whole thing back out.
    if (SQUACHY_KEYLINE) {
        // Head and torso ONLY. Both are anchored to hy, which already
        // carries the bob, so a copy drawn here stays under them.
        //
        // Arms and legs are deliberately not outlined here. They move --
        // legs lift on the walk cycle, arms sway on idle, and several
        // moods throw the arms out as wide lines at entirely different
        // angles. An outline drawn once at the neutral pose stays put
        // while the limb slides out from under it, which is exactly the
        // "outline isn't stuck to him" effect. Each limb draws its own,
        // immediately before itself, further down.
        // The head's own keyline is skipped for VOID EYE: there is no skull
        // under that costume, and a dark outline sized to one would show as a
        // halo around the sphere.
        if (outfitNow != OutfitId::VOIDEYE)
            // At the head's LIVE position: the squash-and-stretch drop and a
            // bow's or howl's head offset both move the head, and an outline
            // left at the base position showed as a black cap above a bowed
            // head.
            t.fillRoundRect(cx2 - S(16), hy + s_headDrop + actHead - S(1), S(32), S(26), S(8), keyCol);
        t.fillRoundRect(cx2 - S(torsoHalf() + 1), hy + S(22), S(2 * torsoHalf() + 2), S(20), S(6), keyCol);
    }

    // ---- shadow ------------------------------------------------------
    // OFF, deliberately, and the whole block is kept rather than deleted.
    //
    // It sits at 82% of his height, which is exactly where a headline
    // pinned above the counter block wants to be, so the two compete for
    // the same rows at every size he can be drawn at -- a marker render put
    // 21 of its pixels in the clear. Getting it out from under the text
    // would need him about 11% SMALLER, and he is worth more big than he is
    // with a shadow nobody can see.
    //
    // Turning it off is not just hiding it: BASE_HEIGHT reserves sixteen of
    // his sixty-eight base units for it, and with it gone those units are
    // his. See BASE_HEIGHT_NOSHADOW.
    if (SQUACHY_SHADOW) {
    // Deliberately outside the head-group offset below, and anchored to
    // headTopY rather than hy: the shadow belongs to the ground, not to
    // him. It stays put while he bobs, hops, is carried and falls.
    //
    // Three things move now, where only the width used to.
    //
    // SHAPE. s_shadowAdj is signed and the two signs mean different things.
    // Positive is contact -- he is landing, so it spreads AND flattens, the
    // product of the two axes held roughly constant, which is what a soft
    // body hitting the floor looks like. Negative is distance -- he is off
    // the ground, so it closes in on both axes together instead, because a
    // shadow that got taller as it got narrower would read as a hole.
    //
    // COVERAGE. There is no alpha here. fillEllipse writes opaque pixels, so
    // the old solid ellipse did not darken the background, it REPLACED it --
    // a brown decal punched through the digital rain. Painting only the
    // pixels that pass a 4x4 Bayer threshold gives us the opacity channel the
    // panel does not have: density IS alpha. It also buys a soft edge for
    // free, since coverage falls off toward the rim.
    //
    // The Bayer cell is indexed by ABSOLUTE screen x/y, never by a running
    // counter. drawBody() runs more than once per logical frame on banded
    // boards, and a counter-driven pattern would land differently in each
    // band and crawl along the seam.
    {
        static const uint8_t BAYER4[16] = {  0,  8,  2, 10,
                                            12,  4, 14,  6,
                                             3, 11,  1,  9,
                                            15,  7, 13,  5 };
        const int rx0 = S(18), ry0 = S(4);
        int rx = rx0 + s_shadowAdj;
        int ry;
        if (s_shadowAdj >= 0) {
            ry = (rx > 0) ? (ry0 * rx0 + rx / 2) / rx : ry0;   // spread, flatten
        } else {
            ry = (rx0 > 0) ? (ry0 * rx + rx0 / 2) / rx0 : ry0; // close in, both axes
        }
        if (rx < 4) rx = 4;
        if (ry < 2) ry = 2;
        const int sy   = headTopY + S(62);
        const int rx2  = rx * rx, ry2 = ry * ry;
        const int cov0 = (int)s_shadowCov;
        const uint16_t sc = blend(BG, FUR_DARK, 70);
        for (int dy = -ry; dy <= ry; dy++) {
            const int yy = sy + dy;
            const int qy = (dy * dy * 256) / ry2;
            if (qy > 256) continue;
            // One sqrt a row, not one a pixel: the row's half-width.
            const int dxm = (int)(rx * sqrtf(1.0f - (float)qy / 256.0f));
            for (int dx = -dxm; dx <= dxm; dx++) {
                const int q = qy + (dx * dx * 256) / rx2;
                if (q > 256) continue;
                // Full in the core, a quarter at the rim. 16 always paints;
                // the cell it is tested against runs 0-15.
                const int cov = (cov0 * (256 - (q * 3) / 4)) >> 8;
                const int xx  = cx2 + dx;
                if (cov > (int)BAYER4[((yy & 3) << 2) | (xx & 3)])
                    t.drawPixel(xx, yy, sc);
            }
        }
    }
    }

    // The bros' overalls come down the legs: denim from the hip to the ankle.
    const bool bros = (outfitNow == OutfitId::PLUMBER || outfitNow == OutfitId::TALLBRO);
    const uint16_t legCol = bros ? t.color565(36, 73, 170)
                          : (outfitNow == OutfitId::SPACE) ? t.color565(219, 219, 255) : furMain;

    // Legs + big bigfoot feet — a simple alternating step lift while
    // walking (TFT_eSPI has no canvas-style transforms to pivot a real
    // leg swing on, so this just varies each leg's vertical offset in
    // opposition, which reads fine at this size). Static otherwise.
    if (s_dangle) {
        // Hanging: both legs straight down and kicking out of phase.
        // Same shapes as the walk cycle, driven faster and without the
        // ground contact that makes a walk a walk.
        const float kp = (float)(now % 260) / 260.0f * 6.2831853f;
        const int kL = (int)(sinf(kp) * S(4));
        const int kR = (int)(sinf(kp + 3.14159265f) * S(4));
        keyR(cx2 - S(10) + kL, hy + S(40), S(8), S(12));
        keyR(cx2 + S(2) + kR,  hy + S(40), S(8), S(12));
        keyRR(cx2 - S(13) + kL, hy + S(51), S(12), S(6), 2);
        keyRR(cx2 + S(1) + kR,  hy + S(51), S(12), S(6), 2);
        t.fillRect(cx2 - S(10) + kL, hy + S(40), S(8), S(12), legCol);
        t.fillRect(cx2 + S(2) + kR,  hy + S(40), S(8), S(12), legCol);
        s_footLx = cx2 - S(13) + kL; s_footLy = hy + S(51);
        s_footRx = cx2 + S(1)  + kR; s_footRy = hy + S(51);
        t.fillRoundRect(s_footLx, s_footLy, S(12), S(6), 2, furLight);
        t.fillRoundRect(s_footRx, s_footRy, S(12), S(6), 2, furLight);
    } else if (m == Mood::WALK) {
        // Feet stop while he is striking a beat. A walk cycle still
        // running under a character who has visibly paused is the single
        // thing that would read as broken here.
        float legPhase = s_walkBeat ? 0.0f : (float)(now % 400) / 400.0f * 6.2831853f;
        int legL = (int)(sinf(legPhase) * S(3));
        int legR = (int)(sinf(legPhase + 3.14159265f) * S(3));
        // The HIP stays put and the leg changes length; it used to be the
        // other way round -- the top moved down by legL while the torso did
        // not follow, so on the half of the cycle where legL is positive the
        // leg detached from the body and left a gap at the hip. The torso
        // bottom sits at S(23)+S(18) and the leg top at S(40), which overlap
        // by about two pixels at any scale, so a swing of up to S(3) opened a
        // five-or-six pixel hole. Visible at every size, not just MEDIUM --
        // that is just where it was spotted.
        //
        // Pinning the top is also what a leg does: the hip is a joint, the
        // foot is what travels.
        keyR(cx2 - S(10), hy + S(40), S(8), S(10) + legL);
        keyR(cx2 + S(2),  hy + S(40), S(8), S(10) + legR);
        keyRR(cx2 - S(13), hy + S(49) + legL, S(12), S(6), 2);
        keyRR(cx2 + S(1),  hy + S(49) + legR, S(12), S(6), 2);
        t.fillRect(cx2 - S(10), hy + S(40), S(8), S(10) + legL, legCol);
        t.fillRect(cx2 + S(2),  hy + S(40), S(8), S(10) + legR, legCol);
        s_footLx = cx2 - S(13); s_footLy = hy + S(49) + legL;
        s_footRx = cx2 + S(1);  s_footRy = hy + S(49) + legR;
        t.fillRoundRect(s_footLx, s_footLy, S(12), S(6), 2, furLight);
        t.fillRoundRect(s_footRx, s_footRy, S(12), S(6), 2, furLight);
    } else {
        const int legH = S(10) - crouch, footY = hy + S(49) - crouch;
        keyR(cx2 - S(10), hy + S(40), S(8), legH);
        keyR(cx2 + S(2),  hy + S(40), S(8), legH);
        keyRR(cx2 - S(13), footY, S(12), S(6), 2);
        keyRR(cx2 + S(1),  footY, S(12), S(6), 2);
        t.fillRect(cx2 - S(10), hy + S(40), S(8), legH, legCol);
        t.fillRect(cx2 + S(2),  hy + S(40), S(8), legH, legCol);
        s_footLx = cx2 - S(13); s_footLy = footY;
        s_footRx = cx2 + S(1);  s_footRy = footY;
        t.fillRoundRect(s_footLx, s_footLy, S(12), S(6), 2, furLight);
        t.fillRoundRect(s_footRx, s_footRy, S(12), S(6), 2, furLight);
    }

    // Body — broad, stocky torso instead of a slim rounded rect.
    t.fillRoundRect(cx2 - S(torsoHalf()), hy + S(23), S(2 * torsoHalf()), S(18), S(5), furMain);
    // No highlight down the sides. There used to be a light strip down each
    // edge, exactly under where the arms hang, so it was invisible at rest and
    // appeared as a second, lighter pair of arms the moment a pose lifted one.

    // Anything worn ON the torso has to go on here, between the torso and the
    // arms. drawOutfit() runs last, so a garment drawn there is painted over
    // his own sleeves -- fine for SHADOW's thin belt, wrong for a suit front.
    // The torso is also the largest flat area he has, and until now only three
    // outfits used it at all, which is most of why the hat-only ones read as
    // nothing from across a room.
    if (outfitNow == OutfitId::SPACE) {
        t.fillRoundRect(cx2 - S(torsoHalf()), hy + S(23), S(2 * torsoHalf()), S(18), S(5), t.color565(224, 224, 232));
        t.fillRect(cx2 - S(torsoHalf()), hy + S(30), S(2 * torsoHalf()), S(2), t.color565(110, 116, 132));
        t.fillRoundRect(cx2 - S(5), hy + S(33), S(10), S(6), 2, t.color565(40, 44, 60));
        // The two lights take turns, so the panel is doing something.
        const bool tick = ((now / 500) & 1u) != 0;
        t.fillRect(cx2 - S(3), hy + S(35), S(2), S(2), tick ? t.color565(0, 255, 136) : t.color565(0, 73, 0));
        t.fillRect(cx2 + S(1), hy + S(35), S(2), S(2), tick ? t.color565(109, 0, 0) : t.color565(255, 60, 60));
    } else if (bros) {
        // Shirt the cap's colour, then the bib and straps over it, lit on the
        // left and shaded on the right like the cap.
        const bool tall = (outfitNow == OutfitId::TALLBRO);
        const uint16_t shirt = tall ? t.color565(0, 182, 0) : t.color565(219, 0, 0);
        const uint16_t den   = legCol;
        const uint16_t denDk = t.color565(0, 36, 85);
        const uint16_t denHi = t.color565(73, 146, 255);
        const uint16_t gold  = t.color565(255, 219, 0);
        const int th = torsoHalf();
        t.fillRoundRect(cx2 - S(th), hy + S(23), S(2 * th), S(18), S(5), shirt);
        t.fillRoundRect(cx2 - S(th), hy + S(33), S(2 * th), S(8), S(4), den);    // the seat
        t.fillRoundRect(cx2 - S(8), hy + S(27), S(16), S(10), S(2), den);        // the bib
        t.fillRect(cx2 - S(9), hy + S(23), S(3), S(5), den);                     // straps
        t.fillRect(cx2 + S(6), hy + S(23), S(3), S(5), den);
        t.fillRect(cx2 - S(8), hy + S(27), S(1) > 0 ? S(1) : 1, S(8), denHi);
        t.fillRect(cx2 + S(7), hy + S(28), S(1) > 0 ? S(1) : 1, S(12), denDk);
        t.fillRoundRect(cx2 - S(3), hy + S(29), S(6), S(4), 1, denDk);          // pocket
        for (int8_t sg = -1; sg <= 1; sg += 2) {
            const int bx = cx2 + sg * S(6) - (sg > 0 ? S(1) : 0);
            t.fillCircle(bx, hy + S(28), S(1) + 1, BLACK);
            t.fillCircle(bx, hy + S(28), S(1), gold);
        }
    } else if (outfitNow == OutfitId::CAPTAIN) {
        // A red coat: white shirt in a V at the neck, lapels either side of
        // it, a black belt with a gold buckle and gold buttons above.
        const uint16_t coat   = t.color565(182, 36, 0);
        const uint16_t coatDk = t.color565(109, 0, 0);
        const uint16_t gold   = t.color565(255, 219, 0);
        const int th = torsoHalf();
        t.fillRoundRect(cx2 - S(th), hy + S(23), S(2 * th), S(18), S(5), coat);
        t.fillTriangle(cx2 - S(5), hy + S(23), cx2 + S(5), hy + S(23), cx2, hy + S(32), WHITE);
        t.fillTriangle(cx2 - S(5), hy + S(23), cx2 - S(9), hy + S(23), cx2 - S(1), hy + S(33), coatDk);
        t.fillTriangle(cx2 + S(5), hy + S(23), cx2 + S(9), hy + S(23), cx2 + S(1), hy + S(33), coatDk);
        t.fillRect(cx2 - S(th), hy + S(35), S(2 * th), S(3), BLACK);
        t.fillRect(cx2 - S(2), hy + S(35), S(4), S(3), gold);
        t.fillRect(cx2 - S(1), hy + S(36), S(2) > 1 ? S(2) - 1 : 1, 1, BLACK);
        for (int8_t sg = -1; sg <= 1; sg += 2)
            for (int k = 0; k < 2; k++) t.fillCircle(cx2 + sg * S(6), hy + S(27) + k * S(4), S(1), gold);
    } else if (outfitNow == OutfitId::TANOOKI) {
        t.fillEllipse(cx2, hy + S(34), S(11), S(8), t.color565(238, 222, 190));
    } else if (outfitNow == OutfitId::PARKA) {
        // His fur is already orange (recoloured above), so this is only the
        // coat's own shape, its hem, and a pair of boots over his feet.
        const uint16_t ink  = t.color565(18, 10, 4);
        const uint16_t seam = t.color565(138, 68, 8);
        t.fillRoundRect(cx2 - S(torsoHalf() + 2), hy + S(21), S(2 * torsoHalf() + 4), S(24), S(7), ink);
        t.fillRoundRect(cx2 - S(torsoHalf() + 1), hy + S(22), S(2 * torsoHalf() + 2), S(22), S(6), t.color565(255, 138, 26));
        // Hem HERE, before the arms, so the sleeves cover its ends and it
        // stays on the coat instead of running across his hands.
        t.fillRect(cx2 - S(torsoHalf() - 1), hy + S(40), S(2 * torsoHalf() - 2), 1, seam);
        // The zip runs from the top of his chest to the hem. It used to start
        // nine units down, which left the whole upper chest blank and made the
        // coat read as a smock.
        t.fillRect(cx2 - 1, hy + S(24), 2, S(16), seam);
        // Boots ON his feet, not near them, and on them in every pose: these
        // take the rectangle the legs above actually drew rather than the
        // resting one. Hard-coded to the rest position they stayed put while
        // the walk cycle lifted each foot in turn, and the orange fur slid out
        // from under the leather a few pixels at a time.
        const uint16_t bootC = t.color565(36, 26, 16);
        t.fillRoundRect(s_footLx - kb, s_footLy - kb, S(12) + 2 * kb, S(6) + 2 * kb, 2, ink);
        t.fillRoundRect(s_footRx - kb, s_footRy - kb, S(12) + 2 * kb, S(6) + 2 * kb, 2, ink);
        t.fillRoundRect(s_footLx, s_footLy, S(12), S(6), 2, bootC);
        t.fillRoundRect(s_footRx, s_footRy, S(12), S(6), 2, bootC);
    }

    // Shoulder-anchored limb: the keyline copy and then the limb, which
    // is the pair every posed arm in here needs. The original branches
    // below still write it out longhand; the poses added later use this
    // rather than adding four more copies of the same two lines.
    // Records where it put the limb before drawing it -- see s_armL0x.
    // Side is decided by the SHOULDER, not the hand: DANCE throws both
    // arms to the same side of centre, and keying off the hand would
    // file both of them as the same arm.
    auto limbTo = [&](int x0, int y0, int x1, int y1, int w = 0) {
        if (x0 < cx2) { s_armL0x = x0; s_armL0y = y0; s_armL1x = x1; s_armL1y = y1; }
        else          { s_armR0x = x0; s_armR0y = y0; s_armR1x = x1; s_armR1y = y1; }
        const int ww = w ? w : S(7);
        keyW(x0, y0, x1, y1, ww);
        wideLine(t, x0, y0, x1, y1, ww, furLight);
    };

    // The resting pose's equivalent: the hanging roundrects are drawn
    // longhand in several branches, so this records their centre line
    // rather than trying to rewrite all of them.
    auto restArms = [&](int dL, int dR) {
        s_armL0x = cx2 - S(14); s_armL0y = hy + S(24) + dL;
        s_armL1x = cx2 - S(14); s_armL1y = hy + S(42) + dL;
        s_armR0x = cx2 + S(14); s_armR0y = hy + S(24) + dR;
        s_armR1x = cx2 + S(14); s_armR1y = hy + S(42) + dR;
    };
    // Seeded, so a branch that leaves one arm hanging (WAVE, and the
    // pointing SHOCKED poses) still reports that arm correctly.
    restArms(0, 0);

    // Arms — long, ape-like, hanging past the waist. Depend on mood; a
    // SHOCKED reaction further varies pose by what triggered it. Static
    // hanging arms used to be the default for every mood except WAVE/
    // SHOCKED (IDLE, BOUNCE, WALK, SLEEPY, and standing still generally
    // all drew the exact same frozen pose) -- everything below except
    // SLEEPY now has some motion of its own so standing still never
    // reads as a paused animation.
    if (s_dangle) {
        // Both arms up and windmilling. He is being held by something
        // above him, so the arms go up whatever mood he was in.
        const int fw = (int)(sinf((float)(now % 220) / 220.0f * 6.2831853f) * S(6));
        limbTo(cx2 - S(11), hy + S(26), cx2 - S(20), hy - S(2) + fw);
        limbTo(cx2 + S(11), hy + S(26), cx2 + S(20), hy - S(2) - fw);
    } else if (now < s_duckUntil) {
        // Ducking, then swatting after whatever just buzzed him. The
        // second half is the better half: a duck alone reads as fear,
        // and a duck followed by a swipe reads as annoyance, which is
        // much more him.
        const uint32_t de = s_duckUntil - now;
        if (de > 380u) {
            limbTo(cx2 - S(11), hy + S(26), cx2 - S(13), hy + S(2));
            limbTo(cx2 + S(11), hy + S(26), cx2 + S(13), hy + S(2));
        } else {
            const float k = sinf((float)(380u - de) / 380.0f * 3.14159265f);
            limbTo(cx2 - S(18), hy + S(22), cx2 - S(18), hy + S(40));
            limbTo(cx2 + S(11), hy + S(26), cx2 + S(14) + (int)(S(10) * k), hy + S(6) - (int)(S(14) * k));
        }
    } else if (s_binoc) {
        // Both fists up at eye level. Checked ahead of the mood chain
        // rather than inside it: this is a property of which screen is
        // open, not of how he feels, and it has to win over whatever
        // mood happens to be running underneath it.
        const int sw = (int)(sinf((float)(now % 3200) / 3200.0f * 6.2831853f) * (4.0f * scale));
        limbTo(cx2 - S(11), hy + S(26), cx2 - S(8) + sw, hy + S(9));
        limbTo(cx2 + S(11), hy + S(26), cx2 + S(8) + sw, hy + S(9));
    } else if (m == Mood::STRETCH) {
        // Both arms overhead, lengthening through the yawn and coming
        // back down as it ends.
        const uint32_t se = (now > s_stretchStart) ? (now - s_stretchStart) : 0;
        const float sk = sinf(fminf((float)se / (float)STRETCH_MS, 1.0f) * 3.14159265f);
        limbTo(cx2 - S(11), hy + S(26), cx2 - S(13) - (int)(S(6) * sk), hy + S(26) - (int)(S(33) * sk));
        limbTo(cx2 + S(11), hy + S(26), cx2 + S(13) + (int)(S(6) * sk), hy + S(26) - (int)(S(33) * sk));
    } else if (m == Mood::JUGGLE) {
        // Hands alternate on the same 1200 ms cycle the packets use, so
        // a hand is always at the top of its travel as a packet leaves.
        const int jw = (int)(sinf((float)(now % 1200) / 1200.0f * 6.2831853f) * S(5));
        limbTo(cx2 - S(11), hy + S(26), cx2 - S(19), hy + S(20) + jw);
        limbTo(cx2 + S(11), hy + S(26), cx2 + S(19), hy + S(20) - jw);
    } else if (m == Mood::ACT) {
        // An emote's pose. sd points at the other Squachy, as HIGHFIVE's does.
        const int sd = s_reachDir;
        auto hang = [&](int side) {             // one arm hanging, left (-1) or right
            const int ax = (side < 0) ? cx2 - S(18) : cx2 + S(10);
            keyRR(ax, hy + S(22), S(8), S(22), S(3));
            t.fillRoundRect(ax, hy + S(22), S(8), S(22), S(3), furLight);
        };
        const float beat = sinf((float)(now % 500) / 500.0f * 6.2831853f);
        switch (act) {
        case VisitPose::SALUTE:                 // hand to the brow
            hang(-sd);
            limbTo(cx2 + sd * S(11), hy + S(26), cx2 + sd * S(9), hy + S(3));
            break;
        case VisitPose::BOW:                    // hands folded at the belly
            limbTo(cx2 - S(11), hy + S(26), cx2 + S(2), hy + S(36));
            limbTo(cx2 + S(11), hy + S(26), cx2 - S(2), hy + S(34));
            break;
        case VisitPose::HUG:                    // both arms round the other one
            limbTo(cx2 + sd * S(11), hy + S(26), cx2 + sd * S(27), hy + S(24));
            limbTo(cx2 - sd * S(11), hy + S(26), cx2 + sd * S(21), hy + S(31));
            break;
        case VisitPose::SAD:                    // arms hanging limp and in
            limbTo(cx2 - S(11), hy + S(26), cx2 - S(9), hy + S(44));
            limbTo(cx2 + S(11), hy + S(26), cx2 + S(9), hy + S(44));
            break;
        case VisitPose::GRR: {                  // fists clenched at his sides, shaking
            const int sh = (int)(sinf((float)(now % 120) / 120.0f * 6.2831853f) * S(1));
            limbTo(cx2 - S(11), hy + S(26), cx2 - S(21) + sh, hy + S(41));
            limbTo(cx2 + S(11), hy + S(26), cx2 + S(21) + sh, hy + S(41));
            t.fillCircle(cx2 - S(21) + sh, hy + S(41), S(4), furLight);
            t.fillCircle(cx2 + S(21) + sh, hy + S(41), S(4), furLight);
            break;
        }
        case VisitPose::CROUCH:                 // hands on his knees
            limbTo(cx2 - S(11), hy + S(26), cx2 - S(13), hy + S(38));
            limbTo(cx2 + S(11), hy + S(26), cx2 + S(13), hy + S(38));
            break;
        case VisitPose::PULL: {                 // both hands on the rope, heaving
            const int tug = (int)(beat * S(2));
            limbTo(cx2 + sd * S(11), hy + S(26), cx2 + sd * S(30) + tug, hy + S(28));
            limbTo(cx2 - sd * S(11), hy + S(26), cx2 + sd * S(24) + tug, hy + S(31));
            break;
        }
        case VisitPose::WIGGLE: {               // tickling fingers, reaching over
            const int wg = (int)(sinf((float)(now % 180) / 180.0f * 6.2831853f) * S(3));
            limbTo(cx2 + sd * S(11), hy + S(26), cx2 + sd * S(28), hy + S(24) + wg);
            limbTo(cx2 - sd * S(11), hy + S(26), cx2 + sd * S(20), hy + S(32) - wg);
            break;
        }
        case VisitPose::CHEER: {                // both arms up, pumping
            const int pk = (int)(fabsf(beat) * S(5));
            limbTo(cx2 - S(11), hy + S(26), cx2 - S(19), hy - S(6) + pk);
            limbTo(cx2 + S(11), hy + S(26), cx2 + S(19), hy - S(6) + pk);
            break;
        }
        case VisitPose::SELFIE: {               // a phone held up and out
            hang(-sd);
            const int px = cx2 + sd * S(22), py = hy - S(4);
            limbTo(cx2 + sd * S(11), hy + S(26), px, py);
            t.fillRoundRect(px - S(3), py - S(6), S(6), S(9), 1, BLACK);
            t.fillRect(px - S(2), py - S(5), S(4), S(6), blend(CYAN, BLACK, 90));
            break;
        }
        case VisitPose::HOWL:                   // hands cupped either side of his mouth
            limbTo(cx2 - S(11), hy + S(26), cx2 - S(15), hy + S(12));
            limbTo(cx2 + S(11), hy + S(26), cx2 + S(15), hy + S(12));
            break;
        case VisitPose::POINT:                  // up and away: "look!"
            hang(-sd);
            limbTo(cx2 + sd * S(11), hy + S(26), cx2 + sd * S(27), hy - S(4));
            break;
        case VisitPose::STRAIN: {               // an arm locked out, trembling
            hang(-sd);
            const int sh = (int)(sinf((float)(now % 120) / 120.0f * 6.2831853f) * S(1));
            limbTo(cx2 + sd * S(11), hy + S(26), cx2 + sd * S(30), hy + S(22) + sh);
            break;
        }
        default:
            hang(-1); hang(1);
            break;
        }
    } else if (m == Mood::HIGHFIVE) {
        // One arm up and across toward the other Squachy, the other hanging.
        // The hand ends S(30) out from centre, so two of them S(60) apart
        // meet in the middle -- which is where the visit puts the guest.
        const int sd = s_reachDir;
        const int hx = (sd > 0) ? cx2 - S(18) : cx2 + S(10);
        keyRR(hx, hy + S(22), S(8), S(22), S(3));
        t.fillRoundRect(hx, hy + S(22), S(8), S(22), S(3), furLight);
        static const int8_t REACH_HY[3] = { 4, 40, 22 };   // must match ui_clear's REACH_Y
        limbTo(cx2 + sd * S(11), hy + S(26), cx2 + sd * S(30), hy + S(REACH_HY[s_reachLevel % 3]));
    } else if (m == Mood::PUMP) {
        // One fist pumping in front of him, three times in the second and a
        // half before the reveal; the other arm hangs.
        const int sd = s_reachDir;
        const int hx = (sd > 0) ? cx2 - S(18) : cx2 + S(10);
        keyRR(hx, hy + S(22), S(8), S(22), S(3));
        t.fillRoundRect(hx, hy + S(22), S(8), S(22), S(3), furLight);
        const float pk = fabsf(sinf((float)(now % 500) / 500.0f * 3.14159265f));
        const int fx = cx2 + sd * S(22), fy = hy + S(6) + (int)(pk * S(12));
        limbTo(cx2 + sd * S(11), hy + S(26), fx, fy);
        t.fillCircle(fx, fy, S(4), furLight);
    } else if (m == Mood::WAVE) {
        float wa = -1.0f + sinf((float)(now % 400) / 400.0f * 6.2831853f) * 0.5f;
        float ex = cx2 + S(13) + cosf(wa) * (18.0f * scale);
        float ey = hy + S(28) + sinf(wa) * (18.0f * scale);
        limbTo(cx2 + S(11), hy + S(28), (int)ex, (int)ey);
        keyRR(cx2 - S(18), hy + S(22), S(8), S(22), S(3));
        t.fillRoundRect(cx2 - S(18), hy + S(22), S(8), S(22), S(3), furLight);
    } else if (m == Mood::SHOCKED) {
        // Fast small shake layered onto whichever pose reactPoseFor()
        // picks, so a flail reads as "can't hold still" instead of a
        // single frozen frame -- separate from the (slower, bigger)
        // panicked bodyCx dart above, which only kicks in for a caller
        // that opted into wanderRangePx.
        float shakeT = (float)(now % 140) / 140.0f * 6.2831853f;
        int shake = (int)(sinf(shakeT) * S(2));
        switch (curReactPose()) {
            case ReactPose::HANDS_UP:
                limbTo(cx2 - S(11), hy + S(26), cx2 - S(14) + shake, hy - S(10));
                limbTo(cx2 + S(11), hy + S(26), cx2 + S(14) + shake, hy - S(10));
                break;
            case ReactPose::COVER_FACE:
                // The crossing lines land on the face — drawn later,
                // after the head/eyes, so they show up in front of it.
                break;
            case ReactPose::POINT_SHADES:
            case ReactPose::DISGUST:
                // Resting arm now; the pointing/covering arm is drawn
                // after the head for the same in-front-of-face reason.
                keyRR(cx2 - S(18), hy + S(22), S(8), S(22), S(3));
        t.fillRoundRect(cx2 - S(18), hy + S(22), S(8), S(22), S(3), furLight);
                break;
            case ReactPose::LOOK_UP:
            case ReactPose::LOOK_AROUND:
            case ReactPose::STARTLED:
            default:
                limbTo(cx2 - S(11), hy + S(26), cx2 - S(23) + shake, hy + S(10));
                limbTo(cx2 + S(11), hy + S(26), cx2 + S(23) + shake, hy + S(10));
                break;
        }
    } else if (m == Mood::DANCE) {
        // An original floss-style move -- both arms swing together to
        // one side then the other (not mirrored the way WAVE/idle sway
        // are), crossing in front of the body each pass. Inspired by
        // the general "arms one way" family of moves (the floss
        // predates and isn't owned by any single game), not a
        // recreation of a specific licensed emote.
        //
        // Anchored at the shoulder via drawWideLine (same trick WAVE's
        // arm uses below) rather than translating a whole floating
        // fillRoundRect -- the old rect version moved both endpoints
        // together, so a wide enough swing carried the entire arm shape
        // away from the torso with nothing connecting them, reading as
        // the arm detaching mid-move instead of swinging from it.
        float daT = (float)(now % 500) / 500.0f * 6.2831853f;
        int armX = (int)(sinf(daT) * S(11));
        limbTo(cx2 - S(14), hy + S(20), cx2 - S(14) + armX, hy + S(44), S(8));
        limbTo(cx2 + S(14), hy + S(20), cx2 + S(14) + armX, hy + S(44), S(8));
    } else if (m == Mood::WALK && s_walkBeat == 1) {
        // Nose down, having a good sniff at whatever is on the floor.
        limbTo(cx2 - S(11), hy + S(26), cx2 - S(15), hy + S(42));
        limbTo(cx2 + S(11), hy + S(26), cx2 + S(15), hy + S(42));
    } else if (m == Mood::WALK && s_walkBeat == 2) {
        // One hand up shading his eyes at something above him. The other
        // stays hanging, so restArms() records that side correctly before
        // limbTo() overwrites only this one.
        restArms(0, 0);
        keyRR(cx2 - S(18), hy + S(22), S(8), S(22), S(3));
        t.fillRoundRect(cx2 - S(18), hy + S(22), S(8), S(22), S(3), furLight);
        limbTo(cx2 + S(11), hy + S(26), cx2 + S(5), hy + S(1));
    } else if (m == Mood::WALK && s_walkBeat == 3) {
        // A scratch behind the ear, hand buzzing.
        const int bz = (int)(sinf((float)now / 45.0f) * S(2));
        restArms(0, 0);
        keyRR(cx2 - S(18), hy + S(22), S(8), S(22), S(3));
        t.fillRoundRect(cx2 - S(18), hy + S(22), S(8), S(22), S(3), furLight);
        limbTo(cx2 + S(11), hy + S(26), cx2 + S(16) + bz, hy + S(11));
    } else if (m == Mood::WALK) {
        // Opposite-arm-opposite-leg swing, same phase the legs above
        // already use (recomputed here rather than threaded through --
        // it's a pure function of `now`, so a duplicate one-liner costs
        // nothing and needs no shared state).
        float legPhase = (float)(now % 400) / 400.0f * 6.2831853f;
        int armL = (int)(sinf(legPhase + 3.14159265f) * S(4));
        int armR = (int)(sinf(legPhase) * S(4));
        restArms(armL, armR);
        keyRR(cx2 - S(18), hy + S(22) + armL, S(8), S(22), S(3));
        t.fillRoundRect(cx2 - S(18), hy + S(22) + armL, S(8), S(22), S(3), furLight);
        keyRR(cx2 + S(10), hy + S(22) + armR, S(8), S(22), S(3));
        t.fillRoundRect(cx2 + S(10), hy + S(22) + armR, S(8), S(22), S(3), furLight);
    } else if (m == Mood::SLEEPY) {
        // Static/droopy on purpose -- motion here would fight the
        // "tired" read the rest of this pose is going for.
        keyRR(cx2 - S(18), hy + S(22), S(8), S(22), S(3));
        t.fillRoundRect(cx2 - S(18), hy + S(22), S(8), S(22), S(3), furLight);
        keyRR(cx2 + S(10), hy + S(22), S(8), S(22), S(3));
        t.fillRoundRect(cx2 + S(10), hy + S(22), S(8), S(22), S(3), furLight);
    } else {
        // IDLE/BOUNCE -- gentle continuous opposing sway so just
        // standing there never reads as a frozen frame.
        float armPhase = (float)(now % 1800) / 1800.0f * 6.2831853f;
        int armSwingL = (int)(sinf(armPhase) * S(3));
        int armSwingR = (int)(sinf(armPhase + 3.14159265f) * S(3));
        restArms(armSwingL, armSwingR);
        keyRR(cx2 - S(18), hy + S(22) + armSwingL, S(8), S(22), S(3));
        t.fillRoundRect(cx2 - S(18), hy + S(22) + armSwingL, S(8), S(22), S(3), furLight);
        keyRR(cx2 + S(10), hy + S(22) + armSwingR, S(8), S(22), S(3));
        t.fillRoundRect(cx2 + S(10), hy + S(22) + armSwingR, S(8), S(22), S(3), furLight);
    }

    // ---- PARKA's hood shell ----------------------------------------------
    // Drawn HERE: after every arm and shoulder, before his head. In the
    // garment slot it went on before the sleeves, so their outlines were
    // painted across it and the hood had arms drawn through it. And it hangs
    // off hh, not hy, so it follows his squash-and-stretch instead of staying
    // put while his head bobs out of it.
    if (outfitNow == OutfitId::PARKA) {
        // Same anchor as the head itself (hh below): without actHead the shell
        // stayed put while a bow dropped the head, and its dark inner ring
        // showed as a second head above the real one.
        const int hcy = hy + s_headDrop + actHead + S(8);
        t.fillCircle(cx2, hcy, S(23) + 2, t.color565(18, 10, 4));
        t.fillCircle(cx2, hcy, S(23), t.color565(255, 138, 26));
        const int pow_ = (S(17) * 102) / 100, poh = (S(16) * 102) / 100;
        t.fillEllipse(cx2, hcy, pow_, poh, t.color565(107, 64, 40));
        t.fillEllipse(cx2, hcy, pow_ - S(4), poh - S(4), t.color565(72, 42, 26));
    }

    // ---- head group ---------------------------------------------------
    // Everything from here down hangs off hh rather than hy. The
    // squash-and-stretch pass (s_headDrop) sinks the head into the
    // shoulders on landing and extends it at the apex; the torso, arms
    // and legs above keep using hy, and that difference is the whole
    // effect. The outfit comes along because a hat that stayed put
    // while the head moved would read as detached.
    const int hh = hy + s_headDrop + actHead;
    // Only our own Squachy says where his head is -- see s_cameo.
    if (!s_cameo) s_lastCrownY = hh;

    // PARKA recolours his fur orange so the coat's sleeves and legs need no
    // repainting -- but that recolour must stop at his neck. His HEAD is his
    // own, brown, sitting inside the hood; run orange all the way up and the
    // face inside the opening is the same colour as the coat around it and
    // the whole hood stops reading.
    if (outfitNow == OutfitId::PARKA) { furMain = FUR_MAIN; furLight = FUR_LIGHT; }

    // VOID EYE replaces his head outright with the sphere drawn in
    // drawOutfit(), so the whole face below is skipped rather than drawn and
    // then painted over -- his skull is wider than the sphere and its edges
    // would show around it. Everything inside keeps its original indentation
    // so this stays a two-line change instead of a two-hundred-line reformat.
    const bool hideFace = (outfitNow == OutfitId::VOIDEYE);
    // The Legend top hat -- see where it goes on, in the head group below.
    // Out here because the void eye skips that group and still wears it.
    const bool legendHat = currentStage() == GrowthStage::LEGEND && Settings::topHatShown();
    auto topHat = [&](int seat) {                // seat: the brim's bottom edge
        // Charcoal inside a lighter rim, not flat black: most of his
        // backgrounds are a night sky, and a black hat against one is a pink
        // band floating over his head with nothing holding it up.
        const uint16_t rim = blend(BLACK, WHITE, 140), felt = blend(BLACK, WHITE, 60);
        t.fillRoundRect(cx2 - S(9) - 1, seat - S(3) - 1, S(18) + 2, S(3) + 2, 1, rim);
        t.fillRect(cx2 - S(5) - 1, seat - S(TOP_HAT_H) - 1, S(10) + 2, S(9) + 1, rim);
        t.fillRoundRect(cx2 - S(9), seat - S(3), S(18), S(3), 1, felt);
        t.fillRect(cx2 - S(5), seat - S(TOP_HAT_H), S(10), S(9), felt);
        t.fillRect(cx2 - S(5), seat - S(5), S(10), S(2), VAPOR_PINK);
    };
    // PARKA has no mouth. Guarded at each draw rather than painted over
    // afterwards: the mouth moves and changes shape with the mood, so no
    // fixed patch covers all of them, and one big enough to try spills off
    // the face patch onto his fur.
    const bool noMouth = (outfitNow == OutfitId::PARKA);
    if (!hideFace) {

    // BLUE BLUR's quills, behind his head so it covers their roots and only
    // the spikes show: two a side, swept back and out, and one up top. A third
    // pair lower down came out under his chin and read as a spiky beard.
    if (outfitNow == OutfitId::BLUEBLUR) {
        static const int8_t Q[5][6] = {
            { -6,  3,   6,  3,  -2,  -8 },    // up top
            { -6,  4, -13, 11, -24,  -4 },    // left, high
            {-11, 10, -13, 19, -26,   8 },    // left, middle
            {  6,  4,  13, 11,  25,  -2 },    // right, high
            { 11, 10,  13, 19,  27,  10 },
        };
        const uint16_t qHi = furLight;
        for (uint8_t pass = 0; pass < 5; pass++) {
            static const int8_t O[5][2] = { {-1, 0}, {1, 0}, {0, -1}, {0, 1}, {0, 0} };
            const uint16_t c = pass < 4 ? BLACK : furMain;
            for (uint8_t i = 0; i < 5; i++)
                t.fillTriangle(cx2 + S(Q[i][0]) + O[pass][0], hh + S(Q[i][1]) + O[pass][1],
                               cx2 + S(Q[i][2]) + O[pass][0], hh + S(Q[i][3]) + O[pass][1],
                               cx2 + S(Q[i][4]) + O[pass][0], hh + S(Q[i][5]) + O[pass][1], c);
        }
        for (uint8_t i = 0; i < 5; i++)
            t.drawLine(cx2 + S(Q[i][0]), hh + S(Q[i][1]), cx2 + S(Q[i][4]), hh + S(Q[i][5]), qHi);
    }

    // TANOOKI's pointed ears, behind his head for the same reason as the
    // quills above: the head covers their roots and their outline with them.
    if (outfitNow == OutfitId::TANOOKI) {
        const uint16_t earC  = t.color565(219, 109, 0);
        const uint16_t earIn = t.color565(109, 36, 0);
        for (int8_t sg = -1; sg <= 1; sg += 2) {
            const int bo = cx2 + sg * S(14), bi = cx2 + sg * S(4);
            const int tx = cx2 + sg * S(11), ty = hh - S(9);
            inked(BLACK, earC, [&](int ox, int oy, uint16_t c) {
                t.fillTriangle(bo + ox, hh + S(4) + oy, bi + ox, hh + S(2) + oy, tx + ox, ty + oy, c);
            });
            t.fillTriangle(cx2 + sg * S(12), hh + S(2), cx2 + sg * S(7), hh + S(1),
                           cx2 + sg * S(11), ty + S(4), earIn);
        }
    }

    // Head — broader jaw than before, brow ridge over the eyes.
    t.fillRoundRect(cx2 - S(15), hh, S(30), S(24), S(7), furLight);
    t.fillRoundRect(cx2 - S(12), hh + S(2), S(24), S(19), S(5), furMain);
    t.fillRoundRect(cx2 - S(9),  hh + S(7), S(18), S(11), S(4), SKIN_TAN);

    // A cowlick, plus a couple of smaller shaggy fringe tufts either side.
    //
    // This used to be ONE symmetrical triangle 14 units tall -- a sagittal
    // crest, the pronounced skull peak real bigfoot sightings always mention.
    // Anatomically the better reference, and the wrong shape: a single tall
    // point centred over his face read as a spear tip or a party hat, and at
    // his current size the tip was clipping off the top of the screen.
    //
    // Two spikes now, and the asymmetry is the whole point. A short one on
    // the left and a taller one leaning right reads as hair that slept
    // funny rather than as a bone ridge, which is both friendlier and more
    // animal. Nothing here is centred, deliberately: the lean is what stops
    // it looking like a shape and starts it looking like a cowlick.
    //
    // 9 units at the tallest against the old 14. It costs him no size --
    // CREST_REACH guarantees the 4-unit side tufts, not this -- but it does
    // mean his silhouette no longer runs off the top at rest.
    //
    // Skipped for BLUE BLUR, the same way the top hat just below is skipped
    // for UNICORN and for the same reason: that outfit already puts its own
    // quills across this exact spot. In brown fur this reads as hair, but
    // BLUE BLUR recolours him blue, so it stops reading as fur and starts
    // reading as a stray quill among the swept-back ones -- the one shape in
    // that silhouette that doesn't belong. Every other outfit, and plain
    // Squachy, still get it.
    // PARKA joins BLUE BLUR in skipping it, for a plainer reason: it is under
    // a hood. The side tufts go with it -- they reach as high as this does
    // and would poke through the fur trim.
    // TANOOKI too: its hood has the pointed ears instead.
    if (outfitNow != OutfitId::PARKA && outfitNow != OutfitId::TANOOKI) {
    if (currentOutfit() != OutfitId::BLUEBLUR) {
        // The short one, upright.
        keyT(cx2 - S(7), hh + S(2), cx2 - S(4), hh - S(5), cx2 - S(1), hh + S(2));
        t.fillTriangle(cx2 - S(7), hh + S(2), cx2 - S(4), hh - S(5), cx2 - S(1), hh + S(2), furLight);
        // The tall one, apex pushed out over its own right base corner so
        // the whole tuft leans instead of standing to attention.
        //
        // 80% of nine units, written as the scaled fraction rather than
        // rounded to S(7): one base unit is under three pixels at CLEAR's
        // scale, so rounding to whole units here is a 3% step and there is
        // no reason to take it. Picked off eight rendered heights rather
        // than by eye -- at nine it read as a horn, and this is the point
        // where it goes back to reading as hair.
        const int tallApex = hh - (S(9) * 80) / 100;
        keyT(cx2 - S(2), hh + S(2), cx2 + S(6), tallApex, cx2 + S(6), hh + S(2));
        t.fillTriangle(cx2 - S(2), hh + S(2), cx2 + S(6), tallApex, cx2 + S(6), hh + S(2), furLight);
    }
        keyT(cx2 - S(13), hh + S(3), cx2 - S(9), hh - S(4), cx2 - S(5), hh + S(3));
        t.fillTriangle(cx2 - S(13), hh + S(3), cx2 - S(9), hh - S(4), cx2 - S(5), hh + S(3), furLight);
        keyT(cx2 + S(5),  hh + S(3), cx2 + S(9), hh - S(4), cx2 + S(13),hh + S(3));
        t.fillTriangle(cx2 + S(5),  hh + S(3), cx2 + S(9), hh - S(4), cx2 + S(13),hh + S(3), furLight);
    }

    // (The Legend top hat goes on after drawOutfit(), below.)

    // Ears — small and tucked close, like a real Sasquach rather than
    // a cartoon animal's.
    t.fillCircle(cx2 - S(15), hh + S(13), S(3), furMain);
    t.fillCircle(cx2 + S(15), hh + S(13), S(3), furMain);
    t.fillCircle(cx2 - S(15), hh + S(13), S(1), SKIN_DARK);
    t.fillCircle(cx2 + S(15), hh + S(13), S(1), SKIN_DARK);

    // Blush
    t.fillCircle(cx2 - S(8), hh + S(15), S(2), VAPOR_PINK);
    t.fillCircle(cx2 + S(8), hh + S(15), S(2), VAPOR_PINK);

    // Eyes / sunglasses + mouth
    if (m == Mood::SHOCKED) {
        ReactPose pose = curReactPose();
        int pdx = 0, pdy = 0;
        if (pose == ReactPose::LOOK_UP) {
            pdy = -S(2);
        } else if (pose == ReactPose::LOOK_AROUND) {
            pdx = (int)(sinf((float)(now % 600) / 600.0f * 6.2831853f) * S(2));
        }
        t.fillEllipse(cx2 - S(5), hh + S(9), S(3), S(4), WHITE);
        t.fillEllipse(cx2 + S(5), hh + S(9), S(3), S(4), WHITE);
        t.fillCircle(cx2 - S(5) + pdx, hh + S(9) + pdy, S(1), BLACK);
        t.fillCircle(cx2 + S(5) + pdx, hh + S(9) + pdy, S(1), BLACK);
        if (!noMouth) t.fillEllipse(cx2, hh + S(18), S(4), S(5), BLACK);

        // The pointing/covering gesture for these reactions lands on
        // the face, so it's drawn last, in front of the head just
        // painted above, instead of underneath it with the other arm.
        // Through limbTo(), so the hand positions are recorded: drawn
        // longhand, these left s_armL1x and friends at the resting hand,
        // and anything worn on the hands (Blue Blur's gloves) stayed down at
        // his sides while the arms were up over his face.
        if (pose == ReactPose::COVER_FACE) {
            limbTo(cx2 - S(11), hh + S(26), cx2 + S(7), hh + S(6));
            limbTo(cx2 + S(11), hh + S(26), cx2 - S(7), hh + S(6));
        } else if (pose == ReactPose::POINT_SHADES) {
            limbTo(cx2 + S(11), hh + S(26), cx2 + S(4), hh + S(8));
        } else if (pose == ReactPose::DISGUST) {
            limbTo(cx2 + S(11), hh + S(26), cx2, hh + S(17));
        }
    } else if (act == VisitPose::SAD) {
        // No shades for this one: eyes you can see, brows up in the middle, a
        // frown, and a tear on its way down.
        wideLine(t, cx2 - S(10), hh + S(6), cx2 - S(3), hh + S(4), S(1) + 1, furLight);
        wideLine(t, cx2 + S(10), hh + S(6), cx2 + S(3), hh + S(4), S(1) + 1, furLight);
        t.fillCircle(cx2 - S(6), hh + S(9), S(1) + 1, BLACK);
        t.fillCircle(cx2 + S(6), hh + S(9), S(1) + 1, BLACK);
        if (!noMouth) {
            wideLine(t, cx2 - S(5), hh + S(20), cx2, hh + S(17), S(1) + 1, BLACK);
            wideLine(t, cx2, hh + S(17), cx2 + S(5), hh + S(20), S(1) + 1, BLACK);
        }
        const float tk = (float)(now % 1100) / 1100.0f;
        t.fillCircle(cx2 - S(7), hh + S(11) + (int)(tk * S(9)), S(1) + 1, CYAN);
    } else if (m == Mood::STRETCH) {
        // Eyes still shut and one enormous yawn -- he is not awake yet,
        // he is waking up, and those are different poses.
        t.drawLine(cx2 - S(9), hh + S(9), cx2 - S(2), hh + S(11), furLight);
        t.drawLine(cx2 + S(2), hh + S(11), cx2 + S(9), hh + S(9), furLight);
        const uint32_t se2 = (now > s_stretchStart) ? (now - s_stretchStart) : 0;
        // Peaks at 0.85, not 1.25: the face patch is only S(11) tall, and
        // a yawn drawn any bigger than this covers the closed eyes that
        // are half of what makes the pose read as waking rather than
        // shouting.
        const float yk = 0.30f + sinf(fminf((float)se2 / (float)STRETCH_MS, 1.0f) * 3.14159265f) * 0.55f;
        if (!noMouth) {
        t.fillEllipse(cx2, hh + S(18), (int)(S(4) * yk) + 1, (int)(S(5) * yk) + 1, BLACK);
        t.fillEllipse(cx2, hh + S(19), (int)(S(2) * yk) + 1, (int)(S(2) * yk) + 1, PINK);
        }
    } else if (m == Mood::SLEEPY) {
        // Closed, content eyes — soft downward arcs instead of shades —
        // plus a little "o" mouth and a drifting Z to sell the nap.
        t.drawLine(cx2 - S(9), hh + S(9), cx2 - S(2), hh + S(11), furLight);
        t.drawLine(cx2 + S(2), hh + S(11), cx2 + S(9), hh + S(9), furLight);
        if (!noMouth) t.fillCircle(cx2, hh + S(18), S(2), BLACK);

        float zPhase = (float)(now % 1600) / 1600.0f;
        int zx = cx2 + S(15) + (int)(zPhase * S(6));
        int zy = hh + S(1) - (int)(zPhase * S(14));
        uint16_t zCol = blend(BG, CYAN, (uint16_t)(220 * (1.0f - zPhase)));
        t.setTextSize(scale > 1.4f ? 2 : 1);
        t.setTextColor(zCol, BG);
        t.setCursor(zx, zy);
        t.print("Z");
    } else {
        // Lens tint is a player-chosen cosmetic (Settings > SHADES
        // COLOR) rather than always cyan — see cycleShadesColor().
        static const uint16_t SHADE_TINTS[4] = { CYAN, VAPOR_PINK, GREEN, VAPOR_PURPLE };
        uint16_t shadeTint = SHADE_TINTS[activeShadeIdx() % 4];
        // His blink used to be ((now / 2200) % 40) < 3 -- a perfect
        // metronome, both eyes, identical duration, forever. Regularity
        // at that scale is most of what makes a face read as a machine
        // rather than as something alive.
        //
        // This picks a different moment, length and kind inside every
        // slot: usually one ordinary blink, sometimes a double, rarely a
        // long slow one. It is a pure function of `now` with no state,
        // which matters more than it looks -- drawBody() runs more than
        // once per logical frame on a banded board, and a blink driven
        // by a stored "next blink at" would land in one band and not the
        // other, tearing his face in half.
        //
        // 140 ms rather than the ~100 that looks right in a browser: at
        // the 22 fps this board actually runs, 100 ms is barely two
        // frames, and a blink that short is skipped more often than seen.
        bool blink = false;
        {
            const uint32_t SLOT = 2600;
            const uint32_t slot = now / SLOT;
            uint32_t h = slot * 2654435761u;
            h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
            const uint32_t t0 = slot * SLOT + 300 + (h % (SLOT - 900));
            const uint8_t kind = (uint8_t)((h >> 20) & 7u);
            if (kind == 0) {              // rare slow one
                blink = (now >= t0 && now < t0 + 440);
            } else if (kind == 1) {       // double
                blink = (now >= t0 && now < t0 + 140) ||
                        (now >= t0 + 230 && now < t0 + 370);
            } else {                      // ordinary
                blink = (now >= t0 && now < t0 + 140);
            }
        }
        // Mood::WINK forces the left lens shut on its own, independent
        // of the normal both-eyes blink cycle -- a wink is one eye,
        // not two.
        bool winking = (m == Mood::WINK);
        uint16_t openLens = blend(BG, shadeTint, 60);
        uint16_t lensL = (blink || winking) ? BLACK : openLens;
        uint16_t lensR = blink ? BLACK : openLens;
        // sd slides the whole pair down the bridge of his nose during a
        // double-take, so he ends up looking over the top of them.
        const int sd = (int)s_shadeDrop;
        t.fillRoundRect(cx2 - S(12), hh + S(6) + sd, S(10), S(7), 2, BLACK);
        t.fillRoundRect(cx2 + S(2),  hh + S(6) + sd, S(10), S(7), 2, BLACK);
        // The bridge runs from the left frame's edge to the right frame's,
        // measured off the frames as drawn. It used to be placed from its own
        // scaled offsets, and S(12) - S(10) is 3 at some sizes where S(2) is
        // 2, which left one column of fur showing between it and the left
        // lens -- visible on the main screen, never in the emulator's 1x.
        {
            const int bx0 = cx2 - S(12) + S(10) - 1;       // the left frame's last column
            const int bx1 = cx2 + S(2);                    // the right frame's first
            t.fillRect(bx0, hh + S(8) + sd, bx1 - bx0 + 1, S(2), BLACK);
        }
        t.fillRoundRect(cx2 - S(11), hh + S(7) + sd, S(8), S(5), 1, lensL);
        t.fillRoundRect(cx2 + S(3),  hh + S(7) + sd, S(8), S(5), 1, lensR);
        if (sd > 0) {
            // Two eyes peering over the frames -- without these the
            // dropped shades just read as badly-placed shades.
            t.fillCircle(cx2 - S(7), hh + S(6), S(2), BLACK);
            t.fillCircle(cx2 + S(7), hh + S(6), S(2), BLACK);
        }

        // A glint sweeps across each open lens (skipped while shut) so
        // the shades read as reflective glass instead of a flat fill.
        float sweep = (float)(now % 2400) / 2400.0f;
        int gx = (int)(sweep * 6.0f);
        if (lensL != BLACK) t.drawFastVLine(cx2 - S(11) + S(1 + gx), hh + S(7) + sd, S(4), WHITE);
        if (lensR != BLACK) t.drawFastVLine(cx2 + S(3)  + S(1 + gx), hh + S(7) + sd, S(4), WHITE);

        // Angry brows, down in the middle, over the top of the frames.
        if (act == VisitPose::GRR || act == VisitPose::STRAIN) {
            wideLine(t, cx2 - S(12), hh + S(3), cx2 - S(3), hh + S(6), S(2), BLACK);
            wideLine(t, cx2 + S(12), hh + S(3), cx2 + S(3), hh + S(6), S(2), BLACK);
        }

        // A little cartoon "wink sparkle" beside the shut lens --
        // purely additive on top of the pose above rather than
        // touching its geometry, so it can never misalign with the
        // frame.
        if (winking) {
            int sx = cx2 - S(16), sy = hh + S(3);
            t.drawLine(sx - S(2), sy, sx + S(2), sy, WHITE);
            t.drawLine(sx, sy - S(2), sx, sy + S(2), WHITE);
        }

        // Mouth: resting smile most of the time, or an open/close
        // "talking" flap while a speech bubble is actually up.
        //
        // ownsBubble is what keeps that second clause from applying to
        // somebody else's line. bubbleText is a file static describing OUR
        // Squachy, and drawBody draws every Squachy on screen -- so with two
        // of them up, the host saying something flapped the GUEST's mouth
        // too, and both of them chewed through every line either one said.
        // Reported as "only the right one should be moving his mouth", and
        // the fix is that a body which does not own the bubble does not get
        // to read it.
        bool talking = forceTalking || (ownsBubble && bubbleText && now < bubbleUntil);
        if (noMouth) {
            // nothing: this costume has no mouth in any mood
        } else if (act == VisitPose::HOWL) {
            // Wide open and round -- the howl itself.
            t.fillEllipse(cx2, hh + S(19), S(4), S(6), BLACK);
            t.fillEllipse(cx2, hh + S(21), S(2), S(2), PINK);
        } else if (act == VisitPose::GRR || act == VisitPose::STRAIN) {
            // Gritted teeth.
            t.fillRoundRect(cx2 - S(7), hh + S(16), S(14), S(6), S(2), BLACK);
            t.fillRect(cx2 - S(6), hh + S(17), S(12), S(4), WHITE);
            for (int k = -1; k <= 1; k++) t.drawFastVLine(cx2 + k * S(3), hh + S(17), S(4), BLACK);
        } else if (m == Mood::GUM) {
            // Pursed, because there is a bubble coming out of it.
            t.fillCircle(cx2, hh + S(18), S(2), BLACK);
        } else if (talking && ((now / 160) % 2) == 0) {
            t.fillRoundRect(cx2 - S(8), hh + S(15), S(16), S(9), S(3), BLACK);
            t.fillRect(cx2 - S(6), hh + S(16), S(12), S(2), WHITE);
            t.fillEllipse(cx2, hh + S(21), S(5), S(3), PINK);
        } else {
            t.fillRoundRect(cx2 - S(8), hh + S(16), S(16), S(7), S(3), BLACK);
            t.fillRect(cx2 - S(6), hh + S(17), S(12), S(2), WHITE);
            t.fillRect(cx2 - S(6), hh + S(19), S(12), S(3), PINK);
        }
    }
    }   // end if (!hideFace)

    drawOutfit(t, cx2, hh, now, m, scale, outfitNow);

    // A tiny top hat, unlocked once he reaches Legend stage, seated on top of
    // whatever is highest on his head -- see topHatSeat(). After the costume,
    // so it sits on a cap or a hood or a pelt rather than under it.
    //
    // It used to hover five units above his tallest tuft on every outfit
    // alike, which put it floating over the tinfoil cone, over both caps,
    // outside the space helmet and half behind the captain's hat, and the
    // void eye, drawn with his face hidden, never got one at all.
    if (legendHat) topHat(hh - S(topHatSeat(outfitNow)));

    // The headset, in the head group so it rides every bob and squash with
    // him. The band arcs over his crown from cup to cup; the cups sit on his
    // ears; the mic boom curls round to the corner of his mouth.
    if (s_headset) {
        const uint16_t ink = BLACK, band = t.color565(182, 182, 170), cup = t.color565(73, 73, 85);
        const int bw = S(2) > 1 ? S(2) : 2;
        const int bcx = cx2, bcy = hh + S(12), br = S(16);
        for (uint8_t pass = 0; pass < 2; pass++) {
            float px = (float)(bcx - br), py = (float)bcy;
            for (uint8_t i = 1; i <= 12; i++) {
                const float a = 3.14159265f + 3.14159265f * (float)i / 12.0f;
                const float nx = (float)bcx + cosf(a) * (float)br, ny = (float)bcy + sinf(a) * (float)br;
                wideLine(t, px, py, nx, ny, pass == 0 ? bw + 2 : bw, pass == 0 ? ink : band);
                px = nx; py = ny;
            }
        }
        wideLine(t, cx2 - S(16), hh + S(16), cx2 - S(10), hh + S(21), bw + 2, ink);
        wideLine(t, cx2 - S(16), hh + S(16), cx2 - S(10), hh + S(21), bw, band);
        t.fillCircle(cx2 - S(9), hh + S(21), S(1) + 2, ink);
        t.fillCircle(cx2 - S(9), hh + S(21), S(1) + 1, cup);
        for (int8_t sg = -1; sg <= 1; sg += 2) {
            const int x = cx2 + sg * S(16) - S(3);
            t.fillRoundRect(x - 1, hh + S(8) - 1, S(6) + 2, S(10) + 2, S(2), ink);
            t.fillRoundRect(x, hh + S(8), S(6), S(10), S(2), cup);
            t.drawFastVLine(x + (sg < 0 ? 1 : S(6) - 2), hh + S(10), S(6), band);
        }
    }

    // ---- props ---------------------------------------------------------
    // Drawn last, so they sit in front of the costume as well as the body.
    if (s_binoc) {
        // Lenses over the shades. The pair sweeps with the arms above
        // while the head stays put -- he is panning the binoculars, not
        // his skull, which is also the only version of this that does
        // not drag every hat sideways along with it.
        const int sw = (int)(sinf((float)(now % 3200) / 3200.0f * 6.2831853f) * (4.0f * scale));
        for (int8_t sgn = -1; sgn <= 1; sgn += 2) {
            const int lx = cx2 + sgn * S(7) + sw;
            t.fillCircle(lx, hh + S(9), S(5) + kb, keyCol);
            t.fillCircle(lx, hh + S(9), S(5), blend(BLACK, WHITE, 45));
            t.fillCircle(lx, hh + S(9), S(3), blend(CYAN, BG, 150));
        }
    }

    if (m == Mood::GUM && s_gumStart != 0) {
        const uint32_t ge = now - s_gumStart;
        if (ge < GUM_GROW_MS + GUM_HOLD_MS) {
            // Square root, NOT squared. A squared curve is the physically
            // truthful one -- a bubble really does start slow -- but it
            // spent the whole first second under six pixels across and
            // only looked like a bubble for the last few frames before it
            // burst, so what anyone actually saw was a pop with nothing in
            // front of it. This is readable within about 300 ms and then
            // holds at full size for GUM_HOLD_MS so there is something to
            // look at before it goes.
            //
            // Scaled as one float expression, not through S(): that lambda
            // takes an int, so S(1.5f) and S(6.5f) silently truncated.
            const float gk = (ge < GUM_GROW_MS) ? (float)ge / (float)GUM_GROW_MS : 1.0f;
            const int r = (int)(scale * (2.0f + 8.0f * sqrtf(gk)
                                         + sinf((float)now / 120.0f) * gk * 0.8f));
            const int by = hh + S(21) + (int)(r * 0.75f);
            t.fillCircle(cx2, by, r + kb, keyCol);
            // Toward WHITE rather than toward BG: blending a pink down
            // into this background walks it to purple, and a purple
            // sphere on his chest reads as anything but bubblegum.
            t.fillCircle(cx2, by, r, blend(VAPOR_PINK, WHITE, 55));
            t.fillCircle(cx2 - r / 3, by - r / 3, r / 5 + 1, WHITE);
        } else if (ge < GUM_GROW_MS + GUM_HOLD_MS + GUM_POP_MS) {
            const float pk = (float)(ge - GUM_GROW_MS - GUM_HOLD_MS) / (float)GUM_POP_MS;
            const int d = (int)(pk * S(16));
            for (uint8_t i = 0; i < 6; i++) {
                const float ang = (float)i / 6.0f * 6.2831853f;
                t.fillCircle(cx2 + (int)(cosf(ang) * d), hh + S(25) + (int)(sinf(ang) * d),
                             (int)(S(2) * (1.0f - pk)) + 1, blend(VAPOR_PINK, WHITE, 55));
            }
        }
    }

    if (m == Mood::JUGGLE) {
        // Three packets on half-sine arcs 400 ms apart, alternating
        // which hand they land in. Coloured by the types actually in
        // the log, so what he is juggling is what he just caught.
        for (uint8_t i = 0; i < 3; i++) {
            const uint32_t ph = now + (uint32_t)i * 400u;
            const float u = (float)(ph % 1200u) / 1200.0f;
            const int8_t side = ((ph / 1200u) & 1u) ? 1 : -1;
            const int px = cx2 - side * (int)(S(19) * cosf(u * 3.14159265f));
            // Peaks at hy - S(14), level with the tip of his crest, so
            // the packets pass over his head rather than across his eyes.
            const int py = hy + S(26) - (int)(sinf(u * 3.14159265f) * S(40));
            t.fillRect(px - S(3) - kb, py - S(3) - kb, S(6) + 2 * kb, S(6) + 2 * kb, keyCol);
            t.fillRect(px - S(3), py - S(3), S(6), S(6), Theme::colorFor(s_recentTypes[i]));
        }
    }

    // The name sticker, last of all so it sits on top of whatever the costume
    // put on his chest. It is pinned to the torso, so it bobs, crouches and
    // bows with him. The small font does not scale, which means the sticker
    // is usually wider than the torso it is stuck to -- which is also how a
    // real HELLO MY NAME IS badge sits on a small child.
    if (s_nameTag) {
        t.setTextSize(1);
        t.setTextWrap(false);
        const int tw = t.textWidth(s_nameTag);
        const int bw = tw + 6, bh = 12;
        const int bx = cx2 - bw / 2;
        const int by = hy + S(32) - bh / 2;   // mid-chest, clear of the chin
        const uint16_t paper = t.color565(244, 242, 232);
        t.fillRoundRect(bx - 1, by - 1, bw + 2, bh + 2, 3, keyCol);
        t.fillRoundRect(bx, by, bw, bh, 2, paper);
        t.fillRect(bx, by, bw, 2, t.color565(220, 40, 40));   // the red band along the top
        t.setTextColor(BLACK, paper);
        t.setCursor(bx + 3, by + 3);
        t.print(s_nameTag);
    }
}

void drawWaving(TFT_eSPI& t, int cx, int baseY, uint32_t now, float scale, const char* line,
                bool talking, int wanderRangePx, bool waving, int bubbleGap, bool laughing,
                bool listening, bool bubbleTail, VisitPose pose) {
    // This cameo is placed by callers that have already reserved room, so
    // there is no region to clamp against.
    s_cameo = true;
    s_topLimit = -10000;
    // The cameo has no mood machine driving the pose channels, so clear
    // them rather than letting whatever CLEAR left behind leak into the
    // boot splash.
    s_headDrop = 0; s_shadowAdj = 0; s_shadowCov = 16; s_shadeDrop = 0; s_binoc = false;
    s_dangle = false;
    // Same idle bob as tick()'s WAVE mood, just without the quip/mood
    // state machine — a self-contained cameo for the boot splash.
    // Laughing is the same trick tick() plays for Mood::BOUNCE -- taller and
    // much faster -- because that is the whole visible difference between
    // standing there and cracking up, and this cameo has no mood machine to
    // ask for it. 7 rather than tick()'s 9: the guest is drawn small and the
    // full amplitude put his ear tips through the title bar.
    float bobAmt = (laughing ? 7.0f : 6.0f) * scale;
    uint32_t bobPeriod = laughing ? 240u : 900u;
    // A set piece's rhythm wins over a laugh: the dance keeps tick()'s DANCE
    // bob, so the two of them move to the same beat when they dance together.
    if (pose == VisitPose::DANCE)  { bobAmt = 6.0f * scale; bobPeriod = 300u; }
    if (pose == VisitPose::SLEEPY) { bobAmt = 1.5f * scale; bobPeriod = 2200u; }
    float bob = sinf((float)(now % bobPeriod) / (float)bobPeriod * 6.2831853f) * bobAmt;
    // Startled: up on his toes and shaking, the jolt tick()'s double-take
    // gives the host, cut down to what a pose can do without his timing.
    int jolt = 0;
    if (pose == VisitPose::STARTLED) {
        bob  = -4.0f * scale;
        jolt = (int)(sinf((float)(now % 140) / 140.0f * 6.2831853f) * 2.0f * scale);
    }
    // The same nod tick() gives the host, on the same curve and the same
    // period, so the two of them read as one pair of manners rather than as
    // two characters animated by different people.
    if (listening && !laughing) {
        const float nk  = (float)(now % 1700u) / 1700.0f;
        const float dip = sinf(nk * 3.14159265f);
        s_headDrop = (int)(dip * dip * 3.6f * scale);
    }
    int headTopY = baseY - (int)(58.0f * scale);
    // Deliberately the idle amplitude rather than this mood's own bobAmt.
    // Using the live value made the ear length constant within a mood but step
    // whenever the mood changed the bounce height, which is the same squash
    // just less often. Pinning it means the length never changes at all; the
    // cost is that at the apex of a BOUNCE the tips pass behind the title bar,
    // which reads as him bouncing up out of frame rather than as the costume
    // deforming.
    s_hyCeiling = headTopY - (int)(3.0f * scale);
    int hy = headTopY + (int)bob;

    // Continuous back-and-forth patrol, opted into by a caller that has
    // real width to spare (LOG's MORE INFO panel) -- reuses WALK_CYCLE_MS
    // for the same amble pace tick()'s own Mood::WALK uses, but runs
    // forever off a plain now%period instead of WALK's start/duration
    // window, since this cameo has no idle-mood scheduler ending it.
    int bodyCx = cx + jolt;
    if (wanderRangePx > 0) {
        float wt = (float)(now % WALK_CYCLE_MS) / (float)WALK_CYCLE_MS * 6.2831853f;
        bodyCx = cx + (int)(sinf(wt) * wanderRangePx);
    }

    // WAVE is the boot splash's pose and it never stops, which is right for
    // two seconds and wrong for a guest standing around for forty-five --
    // reported from hardware as "the visitor continuously waves". He waves
    // hello, then settles.
    // A cameo has no access to the global bubble, so its mouth is driven
    // entirely by what IT was handed: the caller's explicit talking flag, or
    // simply having a line to say. Before this, the boot splash's Squachy
    // held a line up without moving his mouth unless our own Squachy happened
    // to be mid-quip somewhere off-screen.
    const bool cameoTalking = talking || (line != nullptr);
    Mood cm = waving ? Mood::WAVE : Mood::IDLE;
    switch (pose) {
        case VisitPose::HIGH_FIVE: cm = Mood::HIGHFIVE; s_reachDir = -1; s_reachLevel = 0; break;
        case VisitPose::LOW_FIVE:  cm = Mood::HIGHFIVE; s_reachDir = -1; s_reachLevel = 1; break;
        case VisitPose::FIST:      cm = Mood::HIGHFIVE; s_reachDir = -1; s_reachLevel = 2; break;
        case VisitPose::PUMP:      cm = Mood::PUMP;     s_reachDir = -1; break;
        case VisitPose::SLEEPY:    cm = Mood::SLEEPY;   break;
        case VisitPose::STRETCH:   cm = Mood::STRETCH;  break;
        case VisitPose::DANCE:     cm = Mood::DANCE;    break;
        // SHOCKED picks its arms from the host's last detection, so the two
        // of them throw the same pose at the same scare -- which is the point.
        case VisitPose::STARTLED:  cm = Mood::SHOCKED;  break;
        // The emotes'. Three borrow a detection reaction with a pose of their
        // own; the rest are Mood::ACT, facing the host.
        case VisitPose::HANDS_UP:    cm = Mood::SHOCKED; s_actReact = (int8_t)ReactPose::HANDS_UP;    break;
        case VisitPose::COVER:       cm = Mood::SHOCKED; s_actReact = (int8_t)ReactPose::COVER_FACE;  break;
        case VisitPose::LOOK_AROUND: cm = Mood::SHOCKED; s_actReact = (int8_t)ReactPose::LOOK_AROUND; break;
        case VisitPose::NONE:
        case VisitPose::LAUGH:     break;           // a laugh is his bob, not a pose
        default:
            if ((uint8_t)pose >= (uint8_t)VisitPose::SALUTE) {
                cm = Mood::ACT; s_actPose = (uint8_t)pose; s_reachDir = -1;
            }
            break;
    }
    drawBody(t, bodyCx, hy, headTopY, now, cm, scale,
             cameoTalking, /*ownsBubble=*/false);
    s_reachDir = 1;
    s_actReact = -1;
    // Fixed above his (pre-bob, pre-wander) head, same as tick()'s
    // bubble row — it shouldn't bounce or chase him around. Pulled up
    // further than tick()'s gap (18px) specifically so it sits right
    // under the "TALKING SASQUACH" subtitle above, in the extra room
    // this bigger boot-splash scale leaves between his head and the
    // subtitle.
    // bubbleGap defaults to 34, which is the BOOT SPLASH's number -- it was
    // chosen to tuck under the "TALKING SASQUACH" subtitle in the room that
    // screen leaves above his head.
    //
    // On CLEAR with a visitor, 34 put his bubble at the very top of the
    // screen, in the same row tick() draws the HOST's bubble in. The two
    // then overlapped and painted over each other, which is what "it cannot
    // render two speech bubbles" looked like from outside. A visitor passes
    // a smaller gap so his bubble sits just above his own head and is
    // visibly his.
    if (line) drawBubble(t, cx, headTopY - bubbleGap, line, now, false,
                         bubbleTail ? bodyCx : NO_TAIL);
    s_cameo = false;
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

// Small radiating "ping" rings beside his head while a raw scan is
// running (see ui_rawscan.cpp's scanningFx) -- purely a function of
// `now`, no persistent state of its own, so the caller can turn it on
// and off between ticks with nothing to reset.
static void drawScanFx(TFT_eSPI& t, int cx, int headTopY, uint32_t now, float scale) {
    int px = cx + (int)(26 * scale);
    int py = headTopY + (int)(6 * scale);
    for (uint8_t i = 0; i < 3; i++) {
        float phase = (float)((now + i * 500) % 1500) / 1500.0f;
        int r = (int)(2 + phase * 16 * scale);
        uint16_t col = Theme::blend(Theme::CYAN, Theme::BG, (uint16_t)(phase * 220.0f));
        t.drawCircle(px, py, r, col);
    }
}

// The rare "party mode" flourish: a rainbow strobe wash across his
// whole allotted region plus falling confetti, drawn before he is so
// he stands out in front of it. Whichever theme is active supplies the
// colors (same runtime Theme:: variables everything else reads), so
// the light show re-tints with the palette instead of a fixed rainbow.
static void drawPartyFx(TFT_eSPI& t, uint32_t now, int topY, int availHeight, bool advance) {
    using namespace Theme;
    int w = t.width();
    static const uint16_t stops[6] = { RED, AMBER, GREEN, CYAN, VAPOR_PURPLE, PINK };
    for (int yy = topY; yy < topY + availHeight; yy += 4) {
        float huePos = fmodf((float)now / 260.0f + (float)(yy - topY) * 0.05f, 6.0f);
        int i0 = (int)huePos % 6, i1 = (i0 + 1) % 6;
        uint16_t col = blend(stops[i0], stops[i1], (uint16_t)((huePos - (int)huePos) * 255));
        t.drawFastHLine(0, yy, w, blend(BG, col, 110));
    }
    // Position update gated: called once per band per banded-render
    // board, and advancing it on every call would fall confetti at N
    // times real speed instead of drawing the same frame's positions
    // N times.
    if (advance) {
        for (uint8_t i = 0; i < CONFETTI_N; i++) {
            s_cfy[i] += s_cfvy[i];
            if (s_cfy[i] > topY + availHeight) s_cfy[i] = (float)topY;
        }
    }
    for (uint8_t i = 0; i < CONFETTI_N; i++) {
        t.fillRect((int)s_cfx[i], (int)s_cfy[i], 3, 3, stops[s_cfcol[i]]);
    }
}

void tick(TFT_eSPI& t, int cx, int topY, int availHeight, uint32_t now,
          bool advance, float minScale, bool scanningFx, int wanderRangePx,
          uint8_t sizePct, int topBandLeft) {
    s_topLimit = topY;
    // Assigned on EVERY call, band calls included, and never left over from a
    // previous one: a screen that puts nothing in that band passes 0 and gets
    // the rise it always had, and two band passes of one frame cannot disagree
    // about whether the bubble rose. See risenBubbleTop().
    s_topBandLeft = topBandLeft;
    // A property of the caller's screen, not of his mood -- see the arm
    // chain in drawBody(). Assigned on every call, band calls included,
    // so a banded board cannot paint one band holding binoculars and
    // the next band without them.
    s_binoc = scanningFx;

    // ---- SHOW OFF ---------------------------------------------------
    // Split deliberately: arming a step mutates state and is gated on
    // advance, but the continuous parts (the binocular flag, the forced
    // walk beat, where a carried Squachy is being held) are assigned on
    // every call, so a banded board cannot paint one band mid-pose and
    // the next one out of it.
    if (s_showOff) {
        const uint32_t se  = now - s_showStart;
        const uint8_t  idx = (uint8_t)(se / SHOW_STEP_MS);
        if (idx >= SHOW_N) {
            if (advance) stopShowOff();
        } else {
            const uint32_t within = se - (uint32_t)idx * SHOW_STEP_MS;
            if (advance && idx != s_showIdx) {
                s_showIdx = idx;
                // +300 so the mood cannot expire in the gap between the
                // end of a step and the arming of the next one.
                moodUntil  = now + SHOW_STEP_MS + 300u;
                nextIdleAt = now + SHOW_STEP_MS + 300u;
                switch (idx) {
                    case 0:  mood = Mood::IDLE;    say("IDLE + SQUASH", SHOW_STEP_MS); break;
                    case 1:  mood = Mood::WAVE;    say("WAVE", SHOW_STEP_MS); break;
                    case 2:  mood = Mood::BOUNCE;  say("BOUNCE", SHOW_STEP_MS); break;
                    case 3:  mood = Mood::WINK;    say("WINK", SHOW_STEP_MS); break;
                    case 4:  mood = Mood::STRETCH; s_stretchStart = now;
                             say("STRETCH + YAWN", SHOW_STEP_MS); break;
                    case 5:  mood = Mood::GUM;     s_gumStart = now;
                             say("GUM BUBBLE", SHOW_STEP_MS); break;
                    case 6:  mood = Mood::JUGGLE;
                             // Real types, so the packets are the colours
                             // they would be if these had just been caught.
                             s_recentTypes[0] = DetectionType::AIRTAG;
                             s_recentTypes[1] = DetectionType::FLOCK;
                             s_recentTypes[2] = DetectionType::CAMERA;
                             say("PACKET JUGGLE", SHOW_STEP_MS); break;
                    case 7:  mood = Mood::DANCE;   say("DANCE", SHOW_STEP_MS); break;
                    case 8:  mood = Mood::WALK; s_walkStart = now; s_walkDir = 1;
                             s_showWB = 1; say("WALK: SNIFF", SHOW_STEP_MS); break;
                    case 9:  s_showWB = 2; say("WALK: LOOK UP", SHOW_STEP_MS); break;
                    case 10: s_showWB = 3; say("WALK: SCRATCH", SHOW_STEP_MS); break;
                    case 11: s_showWB = -1; mood = Mood::SHOCKED;
                             s_reactType = DetectionType::AXON;
                             s_dtStart = now; s_recoilK = 1.0f;
                             say("DOUBLE-TAKE + RECOIL", SHOW_STEP_MS); break;
                    case 12: mood = Mood::IDLE; say("BINOCULARS", SHOW_STEP_MS); break;
                    case 13: mood = Mood::IDLE; s_duckCooldown = 0;
                             s_duckUntil = now + 900u;
                             say("TOASTER DUCK", SHOW_STEP_MS); break;
                    case 14: mood = Mood::IDLE; say("PICK UP + DROP", SHOW_STEP_MS); break;
                    case 15: mood = Mood::LEAN; s_leanStart = now; s_leanDir = 1;
                             say("LEAN + LISTEN", SHOW_STEP_MS); break;
                    case 16: mood = Mood::TRIP; s_dtStart = now; s_recoilK = 0.8f;
                             say("TRIP", SHOW_STEP_MS); break;
                    case 17: mood = Mood::IDLE; s_shakeStart = now + 200u;
                             say("SHAKE IT OFF", SHOW_STEP_MS); break;
                    case 18: mood = Mood::IDLE; s_backSteps = 3; s_backAt = now;
                             say("BACKING AWAY", SHOW_STEP_MS); break;
                    case 19: mood = Mood::ACT; s_hostAct = (uint8_t)VisitPose::CROUCH;
                             s_hostActUntil = now + SHOW_STEP_MS + 300u; s_backSteps = 0;
                             say("SIT DOWN", SHOW_STEP_MS); break;
                    case 20: mood = Mood::IDLE; s_flickStart = now; s_flickDir = 1;
                             say("FLICK", SHOW_STEP_MS); break;
                    default: mood = Mood::SLEEPY; say("NAP", SHOW_STEP_MS); break;
                }
            }
            if (s_showIdx == 12) s_binoc = true;
            if (s_showIdx == 14) {
                if (within < 1300u) {
                    // Carried, swinging gently, as if on a finger.
                    s_grabbed = true;
                    s_grabX = cx + (int)(sinf((float)within / 260.0f) * 26.0f);
                    s_grabY = topY + 46;
                } else if (advance && s_grabbed) {
                    release();          // and let him fall
                }
            }
        }
    }
    // Everything in this block mutates mood/timers/particle state —
    // gated to run once per logical frame (see the header comment on
    // tick()) regardless of how many physical bands call this. The
    // actual drawing further down runs every call unconditionally so
    // each band still gets painted.
    if (advance) {
    // First-boot walkthrough: advance on its own if nobody's tapped the
    // bubble (onboardingTapAdvance handles the tap-driven case). Checked
    // independently of mood, and ahead of the idle-quip block below,
    // which it also suppresses entirely while active — his voice is
    // reserved for the script, not random chatter, until it's done.
    { static bool loaded = false; if (!loaded) { setTempo(Settings::mascotTempoPct()); loaded = true; } }
    if (s_onboardActive && now >= bubbleUntil) {
        advanceOnboarding();
    }

    // Activity heat decays back to 0 on its own regardless of whether
    // an idle quip actually fires this tick — see s_activityHeat and
    // the line-pool bias below.
    if (s_activityHeat > 0.0f && now - s_lastHeatDecayAt > 2000) {
        s_activityHeat -= 4.0f;
        if (s_activityHeat < 0.0f) s_activityHeat = 0.0f;
        s_lastHeatDecayAt = now;
    }

    // Expire a triggered mood back to idle
    if (mood != Mood::IDLE && now > moodUntil) mood = Mood::IDLE;
    if (s_legendary && now >= s_legendaryUntil) s_legendary = false;

    // Random idle fun: bounce/wave + a quip, only when nothing else
    // triggered a reaction recently, and never while the walkthrough
    // above is running.
    // The thirty-second reassurance. Checked BEFORE the idle roll below,
    // because that roll re-arms itself anywhere from 9 to 32 seconds out --
    // a beat that has to be dependable cannot be one of its outcomes.
    // Skipped while he is mid-anything (onboarding, showing off, asleep,
    // already talking) rather than interrupting: a scripted line landing on
    // top of a nap reads as a bug, and the next beat is only 30s away.
    if (!s_onboardActive && !s_showOff && mood == Mood::IDLE &&
#if SQUACH_MESH
        !s_visiting &&                      // he is in the middle of a chat
#endif
        now >= s_nextWatchAt && now >= bubbleUntil) {
        say(pick(WATCHING_LINES, WATCHING_N), 4000);
        s_nextWatchAt = now + WATCH_EVERY_MS;
        // Hold the random idle roll off so the two do not stack into one
        // bubble replacing another mid-read.
        if (nextIdleAt < now + 8000) nextIdleAt = now + tempo(8000);
    }

    // BANTER scales the gap the roll set for itself: LESS waits two and a
    // half times as long, MORE half. Measured from the last roll, so a gap
    // set by something else (a catch, the watch line) scales the same way.
    static uint32_t s_idleRolledAt = 0;
    const uint32_t  idleGap  = nextIdleAt > s_idleRolledAt ? nextIdleAt - s_idleRolledAt : 0;
    const uint32_t  idleDue  = s_idleRolledAt + (uint32_t)(idleGap * Settings::banterScale());
    if (!s_onboardActive && !s_showOff && mood == Mood::IDLE &&
#if SQUACH_MESH
        !s_visiting &&
#endif
        now >= idleDue) {
        s_idleRolledAt = now;
        s_idleRoll = true;
        uint32_t idleFor = now - lastInteraction;
        bool longIdle  = idleFor > 90000;
        // idleFor only ever grows while nothing happens, so without the
        // cooldown+cap this would stay true (and keep re-triggering the
        // nap below) forever once tripped -- see NAP_DURATION_MS.
        bool verySleepy = idleFor > SLEEPY_AFTER_MS && now >= s_napCooldownUntil
                          && (s_napStart == 0 || now - s_napStart < NAP_DURATION_MS);
        if (verySleepy) {
            // A nap, not just another quip — persists for a while and
            // re-enters itself below rather than popping in and out
            // every idle cycle, but only up to NAP_DURATION_MS total.
            if (s_napStart == 0) s_napStart = now;
            say(pick(SLEEPY_LINES, 4), 6000);
            mood = Mood::SLEEPY;
            moodUntil = now + tempo(8000);
            nextIdleAt = now + tempo(8000);
        } else if (s_napStart != 0) {
            // Just woke up from a capped nap -- back at it, and held
            // off from immediately napping again for a while even
            // though idleFor is still well past SLEEPY_AFTER_MS.
            s_napStart = 0;
            s_napCooldownUntil = now + SLEEPY_AFTER_MS;
            // Waking up used to snap straight to a wave -- the one
            // transition in his whole state machine with no transition
            // at all. STRETCH gives it an exit.
            say(pick(STRETCH_LINES, 4), MIN_BUBBLE_MS);
            mood = Mood::STRETCH;
            s_stretchStart = now;
            moodUntil = now + STRETCH_MS;
            nextIdleAt = now + tempo(8000);
        } else if (random(0, 250) == 0) {
            // Rare shimmering flourish — see drawBody's fur-color swap
            // — escalated into a full rainbow-wash-and-confetti moment.
            say(pick(PARTY_LINES, 4), 5000);
            mood = Mood::BOUNCE;
            moodUntil = now + tempo(2000);
            s_legendary = true;
            s_legendaryUntil = now + 5000;
            int w = t.width();
            for (uint8_t i = 0; i < CONFETTI_N; i++) {
                s_cfx[i]   = (float)random(0, w);
                s_cfy[i]   = (float)(topY + random(0, availHeight));
                s_cfvy[i]  = 0.8f + (float)random(0, 100) / 100.0f * 1.4f;
                s_cfcol[i] = (uint8_t)random(0, 6);
            }
            nextIdleAt = now + tempo(9000) + random(0, 13000);
        } else if (s_haveLastDetection && (now - s_lastDetectionAt) < 120000u
                   && s_recentTypes[0] != DetectionType::UNKNOWN
                   && random(0, 3) == 0) {
            // Gated on "caught something in the last two minutes" rather
            // than on activity heat. Heat could not actually reach this:
            // a detection adds 30 against a 50 floor and it bleeds off at
            // 2 a second, so one catch never qualified and two qualified
            // for about five seconds -- inside a window this chain only
            // samples every 12 to 30 seconds. It was unreachable in
            // practice, which is not the same as rare.
            // Showing off the last three catches. Gated on real recent
            // activity so it can only appear when there is genuinely
            // something to show -- the one flourish here that carries
            // information rather than just character.
            say(pick(JUGGLE_LINES, 4), 3600);
            mood = Mood::JUGGLE;
            moodUntil = now + tempo(3600);
            nextIdleAt = now + tempo(14000) + random(0, 18000);
        } else if (random(0, 7) == 0) {
            // Gum. Means nothing, which is the argument for it: every
            // other thing he does is a reaction to the radio.
            say(pick(GUM_LINES, 4), 3000);
            mood = Mood::GUM;
            s_gumStart = now;
            moodUntil = now + GUM_GROW_MS + GUM_HOLD_MS + GUM_POP_MS + 400;
            nextIdleAt = now + tempo(14000) + random(0, 18000);
        } else if (random(0, 9) == 0) {
            // A stretch on its own, not only on the way out of a nap.
            // Nap exit was the only route in, and a nap needs
            // SLEEPY_AFTER_MS -- ten full minutes of being ignored --
            // before it will even start, so the pose was effectively
            // unreachable on a device anyone was actually looking at.
            say(pick(STRETCH_LINES, 4), 3000);
            mood = Mood::STRETCH;
            s_stretchStart = now;
            moodUntil = now + STRETCH_MS;
            nextIdleAt = now + tempo(12000) + random(0, 16000);
        } else if (s_lastTouchAt && !s_ignoredSaid && now - s_lastTouchAt > 86400000u) {
            s_ignoredSaid = true;
            say(pick(IGNORED_LINES, 3), 6000);
            mood = Mood::WAVE;
            moodUntil = now + tempo(1200);
            nextIdleAt = now + tempo(9000) + random(0, 13000);
        } else if (s_idleProvider && random(0, 3) == 0 && (s_noticeLine = s_idleProvider()) != nullptr) {
            // Something the board knows and he does not: the DEX, the black
            // box, the calendar. See notices.h. Asked once: it rolls its own
            // dice, so a second ask would be a different line, or none.
            say(s_noticeLine, 6000);
            mood = random(0, 2) ? Mood::WAVE : Mood::BOUNCE;
            moodUntil = now + tempo(1200);
            nextIdleAt = now + tempo(9000) + random(0, 13000);
        } else if (longIdle && random(0, 5) == 0) {
            // Nothing for a minute and a half: he sits down. His crouch pose,
            // held, with his hands on his knees; a catch stands him up.
            say(pick(SIT_LINES, 4), MIN_BUBBLE_MS);
            mood = Mood::ACT;
            s_hostAct = (uint8_t)VisitPose::CROUCH;
            moodUntil = now + tempo(9000);
            s_hostActUntil = moodUntil;
            nextIdleAt = now + tempo(9000) + 8000 + random(0, 12000);
        } else if (random(0, 9) == 0) {
            // He heard something. Leans toward one side to listen, then
            // comes back -- see the LEAN block in the body maths.
            say(pick(LEAN_LINES, 4), MIN_BUBBLE_MS);
            mood = Mood::LEAN;
            s_leanStart = now;
            s_leanDir   = random(0, 2) ? 1 : -1;
            moodUntil = now + tempo(LEAN_MS);
            nextIdleAt = now + tempo(9000) + random(0, 13000);
        } else if (random(0, 30) == 0) {
            // Trips over nothing. The double-take's stumble, with no cause and
            // no shades slip, and a look around afterwards.
            say(pick(TRIP_LINES, 4), MIN_BUBBLE_MS);
            mood = Mood::TRIP;
            s_dtStart = now;
            s_recoilK = 0.8f;
            moodUntil = now + tempo(1600);
            nextIdleAt = now + tempo(9000) + random(0, 13000);
        } else if (random(0, 8) == 0) {
            // A little dance break -- see drawBody()'s DANCE arm case.
            say(pick(DANCE_LINES, 4), 3200);
            mood = Mood::DANCE;
            moodUntil = now + tempo(2400);
            nextIdleAt = now + tempo(9000) + random(0, 13000);
        } else if (random(0, 6) == 0) {
            // A little wander away from center and back — see the
            // bodyCx computation below and the leg-cycle in drawBody().
            say(pick(WALK_LINES, 4), MIN_BUBBLE_MS);
            mood = Mood::WALK;
            moodUntil = now + WALK_DURATION_MS;
            s_walkStart = now;
            s_walkDir = random(0, 2) ? 1 : -1;
            nextIdleAt = now + WALK_DURATION_MS + 12000 + random(0, 18000);
        } else if (random(0, 12) == 0) {
            // A brief fourth-wall wink -- see drawBody()'s Mood::WINK
            // branch for the actual pose (one shut lens + a sparkle).
            say(pick(WINK_LINES, 4), MIN_BUBBLE_MS);
            mood = Mood::WINK;
            moodUntil = now + tempo(1800);
            nextIdleAt = now + tempo(9000) + random(0, 13000);
        } else {
            // Recent real activity (or a long stretch of none) biases
            // which pool this pulls from, so idle chatter reads as
            // connected to what's actually been happening instead of
            // generic filler regardless. Falls through to the original
            // bored/encourage/idle mix the rest of the time.
            bool haveHistory = s_cachedLifetimeTotal > 0;
            if (s_activityHeat >= 50.0f && random(0, 2) == 0) {
                say(pick(ALERT_MOOD_LINES, 4), MIN_BUBBLE_MS);
            } else if (s_activityHeat < 15.0f && longIdle && random(0, 2) == 0) {
                say(pick(RELAXED_MOOD_LINES, 4), MIN_BUBBLE_MS);
            } else if (Clock::trusted() && random(0, 3) == 0) {
                // The hour, the day of the week: what a clock is for.
                say(pickTimeLine(), MIN_BUBBLE_MS);
            } else if (haveHistory && random(0, 6) == 0) {
                say(buildStatLine(), MIN_BUBBLE_MS);
            } else if (random(0, hintAvailable() ? 2 : 6) == 0) {
                // Three times as often on a background with something still to
                // find in it -- see pickBackgroundLine().
                say(pickBackgroundLine(), MIN_BUBBLE_MS);
            } else if (longIdle && random(0, 3) == 0) {
                say(pick(BORED_LINES, 4), MIN_BUBBLE_MS);
            } else if (random(0, 5) == 0) {
                say(pick(NOIR_LINES, NOIR_LINES_N), MIN_BUBBLE_MS);
            } else if (random(0, 4) == 0) {
                say(pick(ENCOURAGE_LINES, 8), MIN_BUBBLE_MS);
            } else {
                say(pick(IDLE_LINES, 18), MIN_BUBBLE_MS);
            }
            mood = random(0, 2) ? Mood::WAVE : Mood::BOUNCE;
            moodUntil = now + tempo(1200);
            nextIdleAt = now + tempo(9000) + random(0, 13000);
        }
        s_idleRoll = false;
    }
    } // if (advance)

    // Maximize Squachy's size to whatever vertical room the caller says
    // is free (title bar to status line), after reserving a row for the
    // speech bubble. Clamped to keep his proportions from getting
    // blocky-huge or unreadably tiny on extreme screen sizes. The
    // walkthrough's bubble is much taller than the usual one-liner, so
    // it reserves more of that room and he renders correspondingly
    // smaller for the duration — reading the explanation matters more
    // than his size right then.
    // One row, not sixteen, and the bubble is why.
    //
    // This reserves space BELOW topY for a speech bubble to sit in. It was
    // sixteen because that is what a one-liner needs. But the one-liner
    // rises now (see risenBubbleTop): it climbs into the rows the title bar
    // used to own and lands at rows 1..15, which ends one row ABOVE topY.
    // It stopped needing anything down here and nobody moved this, so
    // sixteen rows sat empty under it and Squachy was pushed down past
    // them. One row of air is all that is left to keep.
    //
    // The walkthrough is the exception and keeps its full reserve. That
    // bubble is 52 tall and never rises -- it is far too wide to clear the
    // corner buttons -- so it genuinely does sit below topY, and he
    // genuinely does have to stand under it.
    //
    // The honest cost: a one-liner that CANNOT rise still draws at topY,
    // and now overlaps the top few rows of his skull instead of stopping
    // above them. That is the wrapped bubbles and the ones beside a corner
    // button, drawn after him either way, over a part of his head with
    // nothing on it -- his face sits well below.
    static const int BUBBLE_ROW_RISEN = 1;
    const int bubbleRowH = s_onboardActive ? ONBOARD_BUBBLE_H : BUBBLE_ROW_RISEN;
    // The floor below normally keeps him from going below scale 1.0 --
    // minScale lets a specific call site (the mini-scan-screen cameo)
    // opt into a smaller floor without changing anyone else's default.
    // Deriving charAvail's own floor from minScale (rather than a flat
    // 40) keeps this a no-op for every existing caller: at minScale's
    // default of 1.0 that floor becomes BASE_HEIGHT itself, which the
    // scale clamp just below was already forcing the same end result
    // through regardless (any charAvail under BASE_HEIGHT still landed
    // on scale 1.0), so nothing about today's on-screen sizes changes.
    // Everyone sits a little lower than the bubble row alone would put them.
    // The drop comes out of charAvail too, so he loses the same few pixels
    // off his height and his feet stay exactly where the caller put them --
    // which is the property the whole of this block depends on.
    static const int BASE_DROP = 5;
    int headroom = BASE_DROP;

    const int baseH = SQUACHY_SHADOW ? BASE_HEIGHT : BASE_HEIGHT_NOSHADOW;

    // The smallest drop that keeps his crest on screen, in closed form
    // rather than as a loop that nudges and re-checks.
    //
    // Both sides move when headroom does, which is why this is worth writing
    // out: a pixel of drop pushes his head down a pixel AND shrinks him,
    // which pulls it down again by CREST_REACH/baseH more. Solving
    //     (topY + bubbleRowH + h) - CREST_REACH * (A - h) / baseH >= TOP_MARGIN
    // for h gives the line below. Rounded UP, because one row short here is
    // a flat-topped head.
    //
    // It only ever raises headroom, never lowers it, so the per-outfit drops
    // above still win where they are larger, and every screen whose band is
    // too small for this to bite is left exactly as it was.
    // Neither guard runs with company on screen -- see setCompany().
    if (!s_company) {
        const int A    = availHeight - bubbleRowH;
        const int Cy   = topY + bubbleRowH;
        // A Legend's top hat is held to the same line as his crest -- all of
        // it on screen, not the tenth a costume may run past the edge. A hat
        // clipped at the top is a brim and a pink band, which is what it was
        // on every Legend until this; he draws a little smaller instead.
        int crestR = CREST_REACH;
        // ...and not when it has been taken off, so he gets his size back.
        if (currentStage() == GrowthStage::LEGEND && Settings::topHatShown()) {
            const int hatR = topHatSeat(currentOutfit()) + TOP_HAT_H;
            if (hatR > crestR) crestR = hatR;
        }
        const int num  = baseH * (TOP_MARGIN - Cy) + crestR * A;
        const int den  = baseH + crestR;
        if (num > 0) {
            const int need = (num + den - 1) / den;
            if (headroom < need) headroom = need;
        }
    }

    // And the same again for whatever he is WEARING, against a looser line:
    // the costume may run past the top edge by OVERFLOW_PCT of his height
    // rather than having to stay under it.
    //
    // Same shape of solve as above, with the allowance itself depending on h
    // because his height does. Writing the condition out --
    //     Cy + h - R*(A-h)/baseH >= -(A-h)*PCT/100
    // -- and clearing the denominators gives
    //     h * ((100-PCT)*baseH + 100*R) >= A*(100*R - PCT*baseH) - 100*baseH*Cy
    // which is the line below, rounded up for the same reason as before.
    //
    // This only ever raises headroom, and headroom is the one thing that
    // moves his top WITHOUT moving his feet: it pushes his head down and
    // takes the same pixels off his height, so his soles stay on the band
    // bottom. A costume too tall for the screen therefore shrinks from the
    // top and stays anchored on the detection bar, which is the only way
    // this could be done without him bouncing up and down as outfits change.
    if (!s_company) {
        const int R    = outfitReach(currentOutfit());
        const int A    = availHeight - bubbleRowH;
        const int Cy   = topY + bubbleRowH;
        const int num  = A * (100 * R - OVERFLOW_PCT * baseH) - 100 * baseH * Cy;
        const int den  = (100 - OVERFLOW_PCT) * baseH + 100 * R;
        if (num > 0 && den > 0) {
            const int need = (num + den - 1) / den;
            if (headroom < need) headroom = need;
        }
    }

    int charAvailFloor = (int)(baseH * minScale);
    if (charAvailFloor < 8) charAvailFloor = 8;   // keep the division sane at extreme minScale
    int charAvail = availHeight - bubbleRowH - headroom;
    if (charAvail < charAvailFloor) charAvail = charAvailFloor;
    // The ceiling on how big he may be drawn, and there are two of them.
    //
    // He is sized from the HEIGHT of the band he is handed, and a band can be
    // far taller than it is wide: the 3.5" in portrait gives him about 340
    // rows across 320 columns, so he came out as wide as the panel with his
    // crest clipped off the top. A body is 56 wide in scale units; the 8 is
    // air, so two of them do not touch. Wide landscape panels never reach it
    // -- 472/56 is 8.4 -- so it binds only where it was actually broken.
    //
    // With company he gets half the panel, because there are two of him on it.
    // ...on the 3.5" only. Honouring the ceiling would shrink him about a
    // tenth on the 2.8" in portrait, where the uncapped branch below has been
    // drawing him at 3.32 for as long as the SIZE setting has existed -- and
    // that board is not to move. The panel is identified by its LONG side, so
    // this follows the 3.5" through both rotations and no 320-or-smaller
    // panel reaches it in either.
    const bool bigPanel = (t.width() >= 400 || t.height() >= 400);
    float scaleMax = 3.0f;
    if (bigPanel) {
        const float span = (float)t.width() / (s_company ? 2.0f : 1.0f) - 8.0f;
        scaleMax = span / 56.0f;
        if (scaleMax > 3.0f) scaleMax = 3.0f;
        if (scaleMax < minScale) scaleMax = minScale;
    }

    float scale = (float)charAvail / (float)baseH;
    if (scale < minScale) scale = minScale;
    if (scale > scaleMax) scale = scaleMax;   // 3.0 everywhere; narrower on a wide panel with company

    int headTopY = topY + bubbleRowH + headroom;

    // The SIZE row in Settings, applied last, and only when it is not 100.
    //
    // Anchored from the BOTTOM rather than the top, which is the whole
    // trick. Every other line in this block works downward from topY and
    // relies on headTopY + charAvail landing exactly on the band floor;
    // shrinking him without re-deriving the anchor would leave his soles
    // hanging in the air above the counters, which is the one thing this
    // must not do. Subtracting the new height from the floor keeps his
    // feet planted and takes the difference off the top, which is also
    // what makes it safe:
    //
    // both guards above -- the crest one and the per-costume one -- are
    // closed-form solutions for the FULL size, and every value here is
    // smaller than what they solved for, so their guarantees hold with
    // room to spare rather than needing to be recomputed. That is why the
    // setting only goes down.
    if (sizePct < 100) {
        int shrunk = (charAvail * (int)sizePct) / 100;
        if (shrunk < charAvailFloor) shrunk = charAvailFloor;
        float s2 = (float)shrunk / (float)baseH;
        if (s2 < minScale) s2 = minScale;
        // The same ceiling the branch above gets. This one had a floor and no
        // roof at all, and it is the path a visit takes: drawVisit() passes
        // 70% to make room for the guest, but 70% of a 340-row band is still
        // scale 3.5 -- past the 3.0 the other path stops at -- so the host and
        // the guest both came out at full size, standing on top of each other.
        // shrunk comes back down with it, or his feet stop meeting the floor.
        if (bigPanel && s2 > scaleMax) { s2 = scaleMax; shrunk = (int)(s2 * (float)baseH); }
        scale    = s2;
        headTopY = topY + availHeight - shrunk;
    }

    // A little wander away from center during Mood::WALK. bodyCx (not
    // cx) drives everything about where he's actually drawn; the
    // bubble further down stays at the original cx regardless — a
    // speech bubble chasing him around a small low-res screen would
    // hurt legibility more than the movement adds charm.
    int bodyCx = cx;
    if (mood == Mood::WALK) {
        float walkT = (float)(now - s_walkStart) / (float)WALK_DURATION_MS;
        if (walkT > 1.0f) walkT = 1.0f;
        // Same half-width margin hitTest() assumes for his footprint,
        // so the range never pushes him somewhere he'd clip off the
        // edge or stand past his own hit box -- this is the actual
        // screen edge, not a token wander distance.
        //
        // Measured from where he STANDS, each way separately. It used to be
        // half the screen either side of him, which is the edge only when he
        // stands in the middle -- and with a visitor he stands in the left
        // quarter, so every sweep took him clean off the screen.
        //
        // He is allowed to lean a third of himself past the edge at the far
        // end of a sweep -- peeking out of frame is funny, vanishing is not.
        int halfW = (int)(24 * scale);
        float roomL = (float)(cx - halfW / 3);
        float roomR = (float)(t.width() - cx - halfW / 3);
        if (roomL < 0) roomL = 0;
        if (roomR < 0) roomR = 0;
        // WALK_CYCLES full sine cycles instead of one: 0 at the start,
        // out to one full edge, back through center, out to the other
        // full edge, then back to 0 -- repeated WALK_CYCLES times -- an
        // actual edge-to-edge patrol that still starts and ends exactly
        // at center, so there's no teleport when WALK expires back to
        // idle.
        const float sweep = sinf(walkT * 6.2831853f * WALK_CYCLES) * (float)s_walkDir;
        bodyCx = cx + (int)(sweep * (sweep < 0.0f ? roomL : roomR));

        // One beat per sweep, at the far end of it. cf is how far through
        // the current cycle he is; 0.25 and 0.75 are the two extremes,
        // where |sin| is 1 and he has effectively stopped. Parking a
        // pause there means he is not sliding sideways while he does it.
        const float cyc = walkT * (float)WALK_CYCLES;
        const int   ci  = (int)cyc;
        const float cf  = cyc - (float)ci;
        if (cf > 0.18f && cf < 0.36f) {
            // Kind is a hash of which sweep this is, so a given patrol
            // does a different sequence each time but stays consistent
            // within itself -- rolling per frame would flicker between
            // poses several times a second.
            uint32_t hb = ((uint32_t)ci + 1u) * 2654435761u ^ s_walkStart;
            hb ^= hb >> 15; hb *= 2246822519u; hb ^= hb >> 13;
            s_walkBeat = (uint8_t)(hb % 4u);      // 0 = just keep walking
        } else {
            s_walkBeat = 0;
        }
        // SHOW OFF asks for a specific beat; the hash above cannot be
        // told which one to pick, so it is overridden here instead.
        if (s_showWB >= 0) s_walkBeat = (uint8_t)s_showWB;
        if (s_walkBeat == 1)      s_headDrop += (int)(4.0f * scale);   // nose down
        else if (s_walkBeat == 2) s_headDrop -= (int)(3.0f * scale);   // looking up
    } else {
        s_walkBeat = 0;
    }
    if ((mood == Mood::SHOCKED || mood == Mood::TRIP) && s_dtStart != 0 && now - s_dtStart < DT_TOTAL_MS) {
        // Double-take. The head snaps the WRONG way first, holds a
        // beat, then whips back and settles -- the classic "wait, what
        // was that" read, and a much better fit for a detector than
        // going straight to a startle.
        //
        // Whole-body rather than head-only, deliberately: several
        // outfits hang off the torso as well as the skull, and turning
        // just the head would slide a hat or a pelt off him.
        //
        // 200 ms on the whip rather than the 130 that felt right on a
        // 60 fps mockup. At the 22 fps this board actually runs, 130 ms
        // is under three frames, and a movement that brief reads as a
        // teleport rather than as speed.
        const uint32_t e = now - s_dtStart;
        // Both amplitudes scale with how strong the signal was, so this
        // one curve covers everything from a twitch at the noise floor
        // to a full stumble at point-blank range. The floor is
        // deliberately not zero -- a detection he does not react to at
        // all would read as a bug.
        const float A =  (2.0f + 7.0f  * s_recoilK) * scale;
        const float B = -(3.0f + 10.0f * s_recoilK) * scale;
        float o;
        if (e < 140u)      o = A * (float)e / 140.0f;
        else if (e < 380u) o = A;
        else if (e < 580u) o = A + (B - A) * ((float)(e - 380u) / 200.0f);
        else {
            const float k = (float)(e - 580u) / (float)(DT_TOTAL_MS - 580u);
            o = B * (1.0f - k) * cosf(k * 6.0f);   // settle, with a wobble
        }
        bodyCx = cx + (int)o;
        // Shades slip once the whip starts, not before -- they are the
        // reaction, not the setup.
        // Only a strong hit knocks the shades down his nose. On a weak
        // one they stay put, which is most of what separates the two
        // reactions at a glance.
        s_shadeDrop = (mood == Mood::SHOCKED && e > 380u && s_recoilK > 0.45f) ? (uint8_t)(2.0f * scale) : 0;
    } else if (mood == Mood::SHOCKED && wanderRangePx >= 0) {
        s_shadeDrop = 0;
        // Panicked dart, opted into by a caller via wanderRangePx (see
        // its comment in squachy.h). Same per-cycle pace as WALK's own
        // amble (WALK_CYCLE_MS) rather than a separately-tuned speed --
        // a wider range at the same period just means a slower, calmer
        // sweep across more ground, not a faster one; the previous
        // fixed 900ms period read as "impossibly fast" once the range
        // grew from a small corner dart to nearly the full screen.
        float jT = (float)(now % WALK_CYCLE_MS) / (float)WALK_CYCLE_MS * 6.2831853f;
        bodyCx = cx + (int)(sinf(jT) * wanderRangePx);
    }

    // ---- the idle-life pack: lean, shake, back away, flick, trip ----
    // Lean: out to one side over 300 ms, held listening, back in over the
    // last 400. s_leanDir 0 is the head-pat version: no slide, just the
    // lean-in (head drop) below.
    if (mood == Mood::LEAN || (s_leanDir == 0 && s_leanStart && now - s_leanStart < 900u)) {
        const uint32_t le = now - s_leanStart;
        const uint32_t total = mood == Mood::LEAN ? tempo(LEAN_MS) : 900u;
        float k;
        if (le < 300u)               k = (float)le / 300.0f;
        else if (le + 400u < total)  k = 1.0f;
        else if (le < total)         k = (float)(total - le) / 400.0f;
        else                         k = 0.0f;
        k = k * k * (3.0f - 2.0f * k);   // ease
        if (s_leanDir != 0) {
            bodyCx += (int)(k * 14.0f * scale * (float)s_leanDir);
            s_headDrop -= (int)(k * 2.0f * scale);           // up on his toes
        } else {
            s_headDrop += (int)(k * 3.0f * scale);           // leaning into the hand
        }
    }
    // Trip: a small dip as he catches himself, on top of the stumble curve.
    if (mood == Mood::TRIP && s_dtStart != 0) {
        const uint32_t te = now - s_dtStart;
        if (te > 380u && te < 700u) s_headDrop += (int)(5.0f * scale * sinf((float)(te - 380u) / 320.0f * 3.14159f));
    }
    // Shake it off: a quick side-to-side shudder that dies out.
    if (s_shakeStart && now >= s_shakeStart && now - s_shakeStart < SHAKE_MS) {
        const float k = 1.0f - (float)(now - s_shakeStart) / (float)SHAKE_MS;
        bodyCx += (int)(sinf((float)(now - s_shakeStart) / 28.0f) * 4.0f * scale * k);
        s_headDrop += (int)(k * 2.0f * scale);
    } else if (s_shakeStart && now - s_shakeStart >= SHAKE_MS) {
        if (advance) { s_shakeStart = 0; say(pick(SHAKE_LINES, 3), MIN_BUBBLE_MS); }
    }
    // Backing away: each step is a few pixels further from centre, away from
    // whichever side he was reaching toward. Steps fade after 20 s of quiet.
    if (s_backSteps > 0) {
        if (now - s_backAt > 20000u) { if (advance) s_backSteps--; s_backAt = now; }
        bodyCx -= (int)(s_backSteps * 9.0f * scale);
    }
    // Flick: out to the wall (ease-out), a bump and a wobble, then back over a
    // second so it reads as a walk rather than a rubber band.
    if (s_flickStart) {
        const uint32_t fe = now - s_flickStart;
        const int wall = s_flickDir > 0 ? t.width() - cx - (int)(22.0f * scale) : -(cx - (int)(22.0f * scale));
        if (fe < FLICK_OUT_MS) {
            const float k = (float)fe / (float)FLICK_OUT_MS;
            bodyCx += (int)((float)wall * (1.0f - (1.0f - k) * (1.0f - k)));
        } else if (fe < FLICK_OUT_MS + FLICK_HOLD_MS) {
            const float k = (float)(fe - FLICK_OUT_MS) / (float)FLICK_HOLD_MS;
            bodyCx += wall;
            s_headDrop += (int)(6.0f * scale * (1.0f - k));                       // the bump
            bodyCx -= (int)(sinf(k * 12.0f) * 3.0f * scale * (1.0f - k) * (float)s_flickDir);   // the wobble
        } else if (fe < FLICK_OUT_MS + FLICK_HOLD_MS + FLICK_BACK_MS) {
            const float k = (float)(fe - FLICK_OUT_MS - FLICK_HOLD_MS) / (float)FLICK_BACK_MS;
            bodyCx += (int)((float)wall * (1.0f - k));
        } else if (advance) {
            s_flickStart = 0;
        }
    }

    // Idle bob runs noticeably quicker than a resting breathing rate —
    // he should read as lively even when nothing's happening. Bob
    // amplitude scales with him so it stays proportional when he's big.
    if (mood != Mood::SHOCKED) s_shadeDrop = 0;

    float bobAmt   = (mood == Mood::BOUNCE) ? 9.0f : (mood == Mood::SLEEPY ? 1.5f : (mood == Mood::DANCE ? 6.0f : 3.0f));
    bobAmt *= scale;
    float bobSpeed = (mood == Mood::BOUNCE) ? 220.0f : (mood == Mood::SLEEPY ? 2200.0f : (mood == Mood::DANCE ? 300.0f : 1100.0f));
    float bob = sinf((float)(now % (uint32_t)bobSpeed) / bobSpeed * 6.2831853f) * bobAmt;
    s_hyCeiling = headTopY - (int)bobAmt;
    if (mood == Mood::BOUNCE) bob = -fabsf(bob); // hop upward only
    int hy = headTopY + (int)bob;

    // ---- squash and stretch ----------------------------------------
    // The bob on its own translates a rigid drawing, which is the one
    // thing that most reads as a sprite being moved rather than a
    // character moving. These two channels fix that without adding a
    // single shape: both are derived from the bob that already exists.
    //
    // u is how high he is through the current bob -- 0 at the bottom,
    // 1 at the top. BOUNCE only ever goes up from the floor (bob is
    // forced negative above) so its u never goes below 0, which is why
    // it gets its own, much stronger curve: a hop has a real landing to
    // absorb, and a breathing idle does not.
    const float u = (bobAmt > 0.01f) ? (-bob / bobAmt) : 0.0f;
    if (mood == Mood::BOUNCE) {
        s_headDrop  = (int)((0.35f - u) * 0.40f * bobAmt);
        s_shadowAdj = (int)(bob * 0.32f);
    } else {
        s_headDrop  = (int)(-u * 0.16f * bobAmt);
        s_shadowAdj = (int)(bob * 0.20f);
    }
    // Coverage rides the same number. u is already how high he is through
    // the bob, and it is clamped here rather than at its source because the
    // idle bob swings BELOW the rest line too, where u goes negative and the
    // shadow should simply stay solid rather than overshoot past full.
    {
        float ua = (u < 0.0f) ? 0.0f : (u > 1.0f ? 1.0f : u);
        s_shadowCov = (uint8_t)(16.0f - ua * 6.0f);
    }

    // Listening: a slow nod on top of whatever the bob is already doing.
    //
    // Squared so it goes down and comes back rather than swaying both ways --
    // a nod has a direction, and a sine through the rest line reads as
    // swaying, which is what somebody bored does. Only while the mood is
    // IDLE, so it never fights the laugh: a Squachy nodding politely and
    // cracking up at the same time is neither.
    if (s_listening && mood == Mood::IDLE) {
        const float nk  = (float)(now % 1700u) / 1700.0f;
        const float dip = sinf(nk * 3.14159265f);
        s_headDrop += (int)(dip * dip * 3.6f * scale);
    }

    // ---- carry and drop --------------------------------------------
    // A finger holding him overrides every other position: the mood
    // machine keeps running underneath (he can be shocked while being
    // held) but where he actually is comes from the touch.
    // A finger that stops reporting is a finger that has gone: a carry whose
    // release was never delivered (a touch lost at the panel's edge, a screen
    // change under the gesture) used to leave him hanging in a corner forever.
    if (s_grabbed && advance && now - s_grabAt > 500u) release();
    if (s_grabbed) {
        const int loY = topY + bubbleRowH;
        const int hiY = topY + availHeight - (int)(46.0f * scale);
        bodyCx = s_grabX;
        hy = s_grabY - (int)(14.0f * scale);
        if (hy < loY) hy = loY;
        if (hy > hiY) hy = hiY;
        const int halfW = (int)(24.0f * scale);
        if (bodyCx < halfW) bodyCx = halfW;
        if (bodyCx > t.width() - halfW) bodyCx = t.width() - halfW;
        s_dangle = true;
    } else if (s_dropStart != 0) {
        const uint32_t de = now - s_dropStart;
        const int restY = headTopY + (int)bob;
        if (de < DROP_MS) {
            // Squared, so he accelerates into the floor rather than
            // sliding back to rest at a constant speed.
            const float k = (float)de / (float)DROP_MS;
            bodyCx = s_dropX + (int)((float)(cx - s_dropX) * k);
            hy     = s_dropY + (int)((float)(restY - s_dropY) * k * k);
            s_dangle = true;
        } else if (de < DROP_MS + LAND_MS) {
            // Landing squash, on the same channels the hop already uses.
            const float k = 1.0f - (float)(de - DROP_MS) / (float)LAND_MS;
            s_headDrop  = (int)(6.0f * scale * k);
            s_shadowAdj = (int)(5.0f * scale * k);
            s_dangle = false;
        } else {
            if (advance) s_dropStart = 0;
            s_dangle = false;
        }
    } else {
        s_dangle = false;
    }

    // Off the ground, so the shadow stops pretending he is standing on it.
    //
    // s_dangle is true exactly while a finger holds him or he is falling
    // back, which is the whole of "not in contact" -- and it is false during
    // the landing squash, so this never fights the spread that fires there.
    //
    // Distance is measured from the REST head line in either direction. The
    // carry clamp lets him be dragged about 46px below his resting position
    // and only a few above it, and down is the direction that actually looks
    // wrong today: his feet end up well past a shadow still sitting at the
    // line he left. Shrinking and thinning it with the gap gets it out of
    // the way instead of leaving a decal parked under his knees.
    if (s_dangle) {
        const int away = (hy > headTopY) ? (hy - headTopY) : (headTopY - hy);
        float af = (float)away / (26.0f * scale);
        if (af > 1.0f) af = 1.0f;
        s_shadowAdj -= (int)(af * 9.0f * scale);
        s_shadowCov  = (uint8_t)((float)s_shadowCov * (1.0f - 0.60f * af));
    }

    // A duck is a whole-body crouch, not just an arm pose -- see the
    // arm chain in drawBody() for the other half of it.
    if (!s_dangle && now < s_duckUntil && s_duckUntil - now > 380u) {
        s_headDrop += (int)(7.0f * scale);
        hy         += (int)(6.0f * scale);
    }

    // Party mode draws first — a full wash across his region — so his
    // body and the hearts below land on top of it, not under it.
    if (s_legendary) drawPartyFx(t, now, topY, availHeight, advance);

    // No erase-then-redraw here: ui_clear.cpp's background draw call
    // (digital rain / starfield / toasters / lava lamp) runs immediately
    // before this every frame and already fully repaints this entire
    // region, including wherever he stood last frame. Erasing his
    // footprint to flat BG on top of that would just punch a static
    // black hole in the animation right behind him — drawing his
    // opaque shapes straight onto the fresh background is enough, and
    // the negative space around his silhouette shows the animation
    // through instead of a box. (Party mode is the one exception —
    // when it's active the wash above already repaints this whole
    // region every frame, same guarantee, just with extra flair.)
    // Whatever moved him -- a patrol, a double-take, a dart -- at least half
    // of him stays on the screen. Hanging off an edge is allowed; gone is not.
    if (bodyCx < 0)         bodyCx = 0;
    if (bodyCx > t.width()) bodyCx = t.width();
    s_reachDir = 1;                   // he stands on the left; see s_reachDir
    s_reachLevel = s_hostReachLevel;
    s_actPose    = s_hostAct;
    s_actReact   = (mood == Mood::SHOCKED && (int32_t)(s_hostActUntil - now) > 0) ? s_hostReact : -1;
    drawBody(t, bodyCx, hy, headTopY, now, mood, scale);
    s_actReact   = -1;
    if (now < s_petFxUntil) drawHeartFx(t, bodyCx, headTopY, now);
    if (scanningFx) drawScanFx(t, bodyCx, headTopY, now, scale);

    // Remember where/how big he actually was this frame — hitTest()
    // (tap-to-pet) checks against this, not a fixed region, since he
    // moves and rescales with the screen (and, now, wanders during
    // Mood::WALK).
    s_lastCx = bodyCx;
    s_lastHeadTopY = headTopY;
    s_lastScale = scale;

    // The bubble does need clearing (its width/height changes with the
    // text, and with which of the two draw functions drew it), but only
    // its own footprint — erase the previous frame's exact rectangle,
    // not a fixed-size strip across the whole row. Covers "bubble went
    // away", "bubble changed to a shorter one", and the walkthrough's
    // last frame handing back off to the compact one-liner bubble.
    //
    // HIS rectangle, kept apart from lastBubble*: a visitor's bubble drawn
    // through drawWaving() writes those too, and in a crowd he is drawn
    // after the visitor -- so this erase used to land on the visitor's
    // bubble from this very frame and black it out.
    static int16_t ownX = 0, ownY = 0, ownW = 0, ownH = 0;
    bool showBubble = bubbleText && now < bubbleUntil;
    if (hadBubble) {
        t.fillRect(ownX, ownY, ownW, ownH, Theme::BG);
    }
    if (showBubble) {
        lastBubbleW = 0; lastBubbleH = 0;
        if (s_onboardActive) drawOnboardBubble(t, cx, topY, bubbleText, s_onboardStep, ONBOARD_N);
        // Pointed at the BODY, not at cx: the two differ while he wanders,
        // and a tail that stays put while he walks out from under it is
        // worse than no tail. Only while there is a second Squachy to be
        // confused with.
        else                 drawBubble(t, cx, topY, bubbleText, now, true,
                                        s_visiting ? bodyCx : NO_TAIL);
    }
    // Only what this call itself drew. A bubble that is held, or still
    // popping in, returns before recording anything, and the rectangle
    // left in lastBubble* is then somebody else's.
    {
        const bool drew = showBubble && !s_bubbleHeld;
        if (drew) { ownX = (int16_t)lastBubbleX; ownY = (int16_t)lastBubbleY;
                    ownW = (int16_t)lastBubbleW; ownH = (int16_t)lastBubbleH; }
        else      { ownW = 0; ownH = 0; }
    }
    // Gated: hadBubble tracks "did we draw a bubble last FRAME" for the
    // erase above. drawBubble()/drawOnboardBubble() compute identical
    // bounds from the same (cx, topY, bubbleText) on every band call so
    // redrawing is harmless, but flipping hadBubble on band 0 would
    // make band 1's erase-check see this frame's state instead of the
    // real previous frame's.
    if (advance) hadBubble = showBubble;
}

} // namespace Squachy
