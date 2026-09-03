// SquachWatch-CYD — Squachy implementation
#include "squachy.h"
#include "theme.h"
#include "signatures.h"
#include "settings.h"
#include <Arduino.h>
#include <Preferences.h>

namespace Squachy {

enum class Mood : uint8_t { IDLE, WAVE, SHOCKED, BOUNCE, SLEEPY, WALK, DANCE, WINK };

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
    "ALL CLEAR means nothing's around. It flips to a big flashing ALERT the second something matches.",
    "Down there: SCAN rescans, LOG shows history, CLR wipes it.",
#if defined(AWOK)
    "Up top left: Settings. The far left/right edges of the screen swap backgrounds, one swap per tap.",
#else
    "Up top: left icon is Settings, right one rotates. Far left/right edges of the screen swap backgrounds.",
#endif
    "Tap me for a pet, hold me for a beat longer, or stroke me for the good stuff.",
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
static const uint32_t WALK_CYCLE_MS   = 9000;
static const uint8_t  WALK_CYCLES     = 5;
static const uint32_t WALK_DURATION_MS = WALK_CYCLE_MS * WALK_CYCLES;
static uint32_t       s_walkStart = 0;
static int8_t         s_walkDir   = 1;
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
static uint8_t  s_nickIdx          = 0;
static uint8_t  s_outfitIdx        = 0;
static bool     s_allOutfitsUnlocked = false;  // hidden button-sequence easter egg

// Runtime-only — reset every boot, not persisted.
static uint32_t s_cachedLifetimeTotal = 0;  // from the last DETECTION trigger (or BOOTED)
static uint32_t s_sessionDetections   = 0;  // this boot's count, vs. s_bestSessionCount
static uint32_t s_lastDetectionAt     = 0;  // millis() of the last catch, for the live streak
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
static int   s_lastCx = -10000, s_lastHeadTopY = 0;
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

static void say(const char* line, uint32_t ms) {
    bubbleText  = line;
    bubbleUntil = millis() + ms;
}

// Every bubble stays up at least this long, no matter which line fires.
static const uint32_t MIN_BUBBLE_MS = 5000;

// Curated, cycle-through options rather than free-text entry — there's
// no keyboard UI on this device worth building just for a nickname.
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
    COUNT
};

struct OutfitDef { const char* name; uint32_t threshold; };
static const OutfitDef OUTFITS[] = {
    { "NONE",           0 },
    { "TANOOKI SQUACH", 0 },
    { "UNICORN",        0 },
    { "TINFOIL SQUACH", 5 },
    { "SHADOW SQUACH",  15 },
    { "PLUMBER BRO",    25 },
    { "TALL BRO",       40 },
    { "SPACE SQUACH",   60 },
    { "BLUE BLUR",      100 },
    { "CAPTAIN SQUACH", 150 },
};
static const uint8_t OUTFITS_N = sizeof(OUTFITS) / sizeof(OUTFITS[0]);
static_assert(OUTFITS_N == (uint8_t)OutfitId::COUNT, "OUTFITS must match OutfitId");

static uint8_t unlockedOutfitCountInternal() {
    if (s_allOutfitsUnlocked) return OUTFITS_N;
    uint8_t n = 0;
    for (uint8_t i = 0; i < OUTFITS_N; i++) {
        if (s_cachedLifetimeTotal < OUTFITS[i].threshold) break;  // ascending -- first miss ends it
        n++;
    }
    return n;
}

// Clamps s_outfitIdx back to NONE if it's pointing past what's
// currently unlocked -- the only way that happens is RESET STATS
// zeroing lifetimeTotal out from under a costume that needed it.
// Assumes prefs are already loaded, same as the rest of drawBody()'s
// direct s_shadeIdx/etc. reads -- safe because trigger(BOOTED) loads
// them at boot, before any tick() ever runs.
static OutfitId currentOutfit() {
    if (s_outfitIdx >= unlockedOutfitCountInternal()) s_outfitIdx = 0;
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
    /* MATRIX    */ { "Matrix rain again. Very hacker of me.", "Green code, brown fur. Bold combo.", "I could read this if I tried. I won't." },
    /* STARFIELD */ { "Starfield's up. Feeling cosmic.", "Somewhere out there, a bigger cryptid.", "Space is just the woods, but darker." },
    /* TOASTERS  */ { "Flying toasters. A classic.", "Nobody needs that much toast airborne.", "After Dark energy today." },
    /* AQUARIUM  */ { "Aquarium mode. Very zen.", "Fish don't do opsec. Rookies.", "I'd get a tank but I'm camera-shy." },
    /* TERMINAL  */ { "Terminal log background. Very my speed.", "Green text, brown fur, good times.", "Looks official. It's mostly vibes though." },
    /* FIREFLIES */ { "Fireflies out tonight. Nice.", "Little lights, big ambiance.", "They're not surveillance. I checked." },
    /* FIRE      */ { "Fire background. Cozy, not concerning.", "Warm vibes, zero smoke alarms.", "Nothing's actually burning. Probably." },
    /* SNOWFALL  */ { "Snowing again. Big feet, better traction.", "Perfect weather for leaving mysterious tracks.", "Cold out. I'm built for this." },
    /* SPECTRUM  */ { "RF spectrum's live. That's the real stuff.", "This is actual signal data. Neat, right?", "Watching the airwaves. Very on-brand." },
    /* TUNNEL    */ { "Wireframe tunnel. Very retro-future.", "Feels like we're going somewhere. We're not.", "80s sci-fi vibes today." },
};
static_assert(sizeof(BG_LINES) / sizeof(BG_LINES[0]) == Settings::BACKGROUND_COUNT,
              "BG_LINES must have exactly one row per Settings::Background value -- "
              "a row count mismatch here means some background is silently reading "
              "another one's lines (see the incident this assert was added after).");

static const char* pickBackgroundLine() {
    uint8_t idx = (uint8_t)Settings::background();
    if (idx >= Settings::BACKGROUND_COUNT) idx = 0;
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
    s_nickIdx         = s_petPrefs.getUChar("nick", 0);
    s_outfitIdx       = s_petPrefs.getUChar("outfitIdx", 0);
    s_allOutfitsUnlocked = s_petPrefs.getBool("allOutfits", false);
    s_petPrefsLoaded  = true;
}

// Defined further down (needs drawOnboardBubble's constants) — forward
// declared so trigger()'s BOOTED case can start the walkthrough on a
// device's very first boot.
static void startOnboardingInternal();

void trigger(Event evt, DetectionType dt, uint32_t lifetimeTotal, uint32_t hitCount) {
    uint32_t now = millis();
    lastInteraction = now;
    switch (evt) {
        case Event::DETECTION: {
            mood = Mood::SHOCKED;
            moodUntil = now + 1400;
            s_reactType = dt;
            ensurePrefsLoaded();
            s_cachedLifetimeTotal = lifetimeTotal;

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

            if (hit > 0) {
                s_lastMilestone = hit;
                snprintf(s_milestoneBuf, sizeof(s_milestoneBuf),
                         "Detection #%lu! Milestone.", (unsigned long)hit);
                say(s_milestoneBuf, 5500);
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
            ensurePrefsLoaded();
            s_cachedLifetimeTotal = lifetimeTotal;
            s_bootCount++;
            s_petPrefs.putUInt("boots", s_bootCount);
            if (!s_petPrefs.getBool("onboarded", false)) {
                startOnboardingInternal();
            } else {
                say(pick(BOOT_LINES, 2), MIN_BUBBLE_MS);
            }
            break;
        case Event::PETTED: {
            mood = Mood::BOUNCE;
            moodUntil = now + 1200;
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
            } else {
                say(pick(PET_LINES, 20), MIN_BUBBLE_MS);
            }
            break;
        }
        case Event::HELD: {
            mood = Mood::BOUNCE;
            moodUntil = now + 1500;
            s_petFxStart = now;
            s_petFxUntil = now + 2600;
            say(pick(HELD_LINES, 6), MIN_BUBBLE_MS);
            break;
        }
        case Event::PETTING: {
            mood = Mood::BOUNCE;
            moodUntil = now + 900;
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
// Tracks the previous frame's bubble footprint (whichever of the two
// draw functions below drew it) so we can erase exactly that
// rectangle when the bubble changes or goes away — the rest of the
// row stays untouched, so matrix rain shows through whenever Squachy
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

static void drawBubble(TFT_eSPI& t, int cx, int topY, const char* text) {
    t.setTextSize(1);
    t.setTextWrap(false);
    int screenW = t.width();
    int maxW = screenW - 16;   // widest a wrapped bubble is allowed to get
    int tw = t.textWidth(text);

    if (tw <= maxW - 10) {
        // Fits on one line -- the original compact box.
        int bw = tw + 10;
        int bh = 14;
        int bx = cx - bw / 2;
        if (bx + bw > screenW - 2) bx = screenW - 2 - bw;
        if (bx < 2) bx = 2;
        t.fillRoundRect(bx, topY, bw, bh, 3, Theme::BG);
        t.drawRoundRect(bx, topY, bw, bh, 3, Theme::VAPOR_PINK);
        t.setTextColor(Theme::WHITE, Theme::BG);
        t.setCursor(bx + 5, topY + 3);
        t.print(text);
        lastBubbleX = bx;
        lastBubbleY = topY;
        lastBubbleW = bw;
        lastBubbleH = bh;
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

    const int lineH = 11;
    int bh = 6 + (int)n * lineH + 3;

    t.fillRoundRect(bx, topY, bw, bh, 3, Theme::BG);
    t.drawRoundRect(bx, topY, bw, bh, 3, Theme::VAPOR_PINK);
    t.setTextColor(Theme::WHITE, Theme::BG);
    for (uint8_t i = 0; i < n; i++) {
        int lw = t.textWidth(lines[i]);
        t.setCursor(bx + (bw - lw) / 2, topY + 3 + i * lineH);
        t.print(lines[i]);
    }
    lastBubbleX = bx;
    lastBubbleY = topY;
    lastBubbleW = bw;
    lastBubbleH = bh;
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
    nextIdleAt = millis() + 6000; // a short pause feels better than an instant quip right after
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
    uint8_t unlocked = unlockedOutfitCountInternal();
    s_outfitIdx = (s_outfitIdx + 1) % unlocked;
    s_petPrefs.putUChar("outfitIdx", s_outfitIdx);
}

void cyclePrevOutfit() {
    ensurePrefsLoaded();
    uint8_t unlocked = unlockedOutfitCountInternal();
    s_outfitIdx = (uint8_t)((s_outfitIdx + unlocked - 1) % unlocked);
    s_petPrefs.putUChar("outfitIdx", s_outfitIdx);
}

uint8_t unlockedOutfitCount() {
    ensurePrefsLoaded();
    return unlockedOutfitCountInternal();
}

uint8_t outfitCount() {
    return OUTFITS_N;
}

void unlockAllOutfits() {
    ensurePrefsLoaded();
    s_allOutfitsUnlocked = true;
    s_petPrefs.putBool("allOutfits", true);

    // A visible tell that the hidden sequence actually landed, reusing
    // the same rainbow-wash-and-confetti flourish the rare idle party
    // moment uses (see tick()) rather than a silent state flip. Exact
    // confetti seed positions don't need real screen geometry here --
    // drawPartyFx()'s own fall-and-wrap logic self-corrects them onto
    // whatever topY/availHeight the very next real tick() call passes.
    uint32_t now = millis();
    mood      = Mood::BOUNCE;
    moodUntil = now + 2500;
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
            moodUntil = now + 800;
            // Mood still reacts (visual feedback that something was
            // found); only the bubble text is held back so it can't
            // cut the hint off early.
            if (now >= s_scanHintUntil) say(pick(SCAN_HIT_LINES, 3), 2200);
            break;
        case ScanMoment::DONE_EMPTY:
            mood = Mood::SLEEPY;
            moodUntil = now + 2000;
            if (now >= s_scanHintUntil) say(pick(SCAN_EMPTY_LINES, 3), 4000);
            break;
        case ScanMoment::DONE_FOUND:
            mood = Mood::BOUNCE;
            moodUntil = now + 2000;
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
            moodUntil = now + 700;
            if (now >= s_huntHintUntil) say(pick(HUNT_FIRST_SIGNAL_LINES, 3), 2200);
            break;
        case HuntMoment::WARMER:
            mood = Mood::BOUNCE;
            moodUntil = now + 800;
            if (now >= s_huntHintUntil) say(pick(HUNT_WARMER_LINES, 4), 2200);
            break;
        case HuntMoment::COLDER:
            // Borrows LOOK_AROUND (normally a tracker's "something's
            // following me" pose) reinterpreted as "searching for the
            // right direction" -- reads better than a droopy SLEEPY for
            // an actively-wrong-way cue, and costs no new pose code.
            s_reactType = DetectionType::AIRTAG;
            mood = Mood::SHOCKED;
            moodUntil = now + 900;
            if (now >= s_huntHintUntil) say(pick(HUNT_COLDER_LINES, 4), 2200);
            break;
        case HuntMoment::HOT:
            // BOUNCE, not SHOCKED -- matches the same "found something
            // great" convention scanReaction()'s big-haul DONE_FOUND
            // uses, rather than an alarmed startle.
            mood = Mood::BOUNCE;
            moodUntil = now + 1500;
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
            moodUntil = now + 2000;
            if (now >= s_huntHintUntil) say(pick(HUNT_STALLED_LINES, 3), 4000);
            break;
    }
}

void watchAlertReaction() {
    mood = Mood::SHOCKED;
    moodUntil = millis() + 1200;
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
static void drawOutfit(TFT_eSPI& t, int cx2, int hy, uint32_t now, Mood m, float scale, OutfitId outfit) {
    if (outfit == OutfitId::NONE) return;
    auto S = [scale](int v) { return (int)(v * scale); };
    (void)m;
    using namespace Theme;

    switch (outfit) {
        case OutfitId::TANOOKI: {
            // No snout/nose shape at all -- it kept reading badly no
            // matter how it was drawn. Just the mask, framing the
            // shades from above and below each lens rather than
            // covering them so his eyes stay visible, plus the tail.
            uint16_t maskCol = blend(BLACK, FUR_DARK, 130);
            t.fillRoundRect(cx2 - S(13), hy + S(4),  S(9), S(3), 1, maskCol);
            t.fillRoundRect(cx2 + S(4),  hy + S(4),  S(9), S(3), 1, maskCol);
            t.fillRoundRect(cx2 - S(13), hy + S(13), S(9), S(3), 1, maskCol);
            t.fillRoundRect(cx2 + S(4),  hy + S(13), S(9), S(3), 1, maskCol);
            // Ringed tail, off to the side so it never overlaps the body
            uint16_t ringDark = blend(FUR_DARK, BLACK, 80);
            t.fillCircle(cx2 + S(18), hy + S(44), S(5), FUR_LIGHT);
            t.fillCircle(cx2 + S(22), hy + S(40), S(5), ringDark);
            t.fillCircle(cx2 + S(25), hy + S(35), S(4), FUR_LIGHT);
            t.fillCircle(cx2 + S(27), hy + S(30), S(4), ringDark);
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
                t.drawWideLine(cx2 - w0 / 2 - S(1), y0, cx2 + w0 / 2 + S(1), y0 - S(3), S(2), bands[i]);
            }
            break;
        }
        case OutfitId::TINFOIL: {
            uint16_t foil   = blend(WHITE, BLACK, 90);
            uint16_t foilHi = blend(WHITE, BLACK, 40);
            t.fillTriangle(cx2 - S(14), hy + S(2), cx2 + S(14), hy + S(2), cx2, hy - S(16), foil);
            t.fillRoundRect(cx2 - S(15), hy, S(30), S(4), 2, foil);
            t.drawLine(cx2 - S(6), hy - S(2), cx2 - S(2), hy - S(9), foilHi);
            t.drawLine(cx2 + S(2), hy - S(3), cx2 + S(6), hy - S(10), foilHi);
            break;
        }
        case OutfitId::SHADOW: {
            t.fillRect(cx2 - S(15), hy + S(1), S(30), S(4), BLACK);
            t.fillRect(cx2 + S(13), hy + S(2), S(2), S(3), RED);
            t.drawLine(cx2 + S(15), hy + S(4), cx2 + S(22), hy + S(10), BLACK);
            t.drawLine(cx2 + S(17), hy + S(4), cx2 + S(24), hy + S(9),  BLACK);
            // Lower-face mask, kept below the shade line so his eyes
            // stay visible -- covers mouth/jaw, not the lenses.
            t.fillRoundRect(cx2 - S(11), hy + S(14), S(22), S(10), S(4), BLACK);
            // Red belt across the torso -- his fur is recolored
            // near-black for this outfit up in drawBody()'s
            // furMain/furLight block, so the belt is the one splash of
            // color against it.
            t.fillRect(cx2 - S(15), hy + S(32), S(30), S(4), RED);
            break;
        }
        case OutfitId::PLUMBER: {
            uint16_t cap = RED;
            t.fillRoundRect(cx2 - S(15), hy - S(4), S(30), S(9), S(4), cap);
            t.fillRoundRect(cx2 - S(4),  hy - S(2), S(16), S(6), S(3), cap);
            t.fillCircle(cx2 - S(2), hy - S(1), S(3), WHITE);
            t.fillRoundRect(cx2 - S(9), hy + S(14), S(18), S(4), S(2), blend(BLACK, FUR_DARK, 80));
            // Full bib front, not just two thin straps -- a much
            // stronger "overalls" silhouette.
            t.fillRoundRect(cx2 - S(9), hy + S(28), S(18), S(9), S(2), VAPOR_BLUE);
            t.fillRect(cx2 - S(10), hy + S(23), S(3), S(14), VAPOR_BLUE);
            t.fillRect(cx2 + S(7),  hy + S(23), S(3), S(14), VAPOR_BLUE);
            t.fillCircle(cx2 - S(9), hy + S(25), S(1), AMBER);
            t.fillCircle(cx2 + S(9), hy + S(25), S(1), AMBER);
            break;
        }
        case OutfitId::TALLBRO: {
            uint16_t cap = GREEN;
            t.fillRoundRect(cx2 - S(15), hy - S(6), S(30), S(11), S(4), cap);
            t.fillRoundRect(cx2 - S(4),  hy - S(3), S(16), S(6), S(3), cap);
            t.fillCircle(cx2 - S(2), hy - S(1), S(3), WHITE);
            t.fillRoundRect(cx2 - S(10), hy + S(13), S(20), S(5), S(2), blend(BLACK, FUR_DARK, 80));
            t.fillRoundRect(cx2 - S(9), hy + S(28), S(18), S(9), S(2), GREEN);
            t.fillRect(cx2 - S(10), hy + S(23), S(3), S(14), GREEN);
            t.fillRect(cx2 + S(7),  hy + S(23), S(3), S(14), GREEN);
            t.fillCircle(cx2 - S(9), hy + S(25), S(1), AMBER);
            t.fillCircle(cx2 + S(9), hy + S(25), S(1), AMBER);
            break;
        }
        case OutfitId::SPACE: {
            t.drawCircle(cx2, hy + S(10), S(19), WHITE);
            t.drawCircle(cx2, hy + S(10), S(18), blend(BG, VAPOR_BLUE, 60));
            t.drawLine(cx2 - S(10), hy - S(4), cx2 - S(4), hy - S(9), WHITE);
            t.fillRoundRect(cx2 - S(16), hy + S(23), S(32), S(4), S(2), WHITE);
            break;
        }
        case OutfitId::BLUEBLUR: {
            uint16_t blue = VAPOR_BLUE;
            t.fillTriangle(cx2 - S(4), hy - S(2), cx2 - S(16), hy - S(6), cx2 - S(6), hy + S(6), blue);
            t.fillTriangle(cx2 + S(2), hy - S(4), cx2 + S(4),  hy - S(18), cx2 + S(10), hy - S(4), blue);
            t.fillTriangle(cx2 + S(6), hy - S(2), cx2 + S(20), hy - S(4),  cx2 + S(10), hy + S(6), blue);
            // White gloves -- a core, consistent trait across every
            // era of the design this homages, and the one thing this
            // outfit was missing that actually sells the reference.
            t.fillCircle(cx2 - S(14), hy + S(42), S(4), WHITE);
            t.fillCircle(cx2 + S(14), hy + S(42), S(4), WHITE);
            t.fillEllipse(cx2 - S(6), hy + S(50), S(6), S(3), RED);
            t.fillEllipse(cx2 + S(6), hy + S(50), S(6), S(3), RED);
            break;
        }
        case OutfitId::CAPTAIN: {
            t.fillTriangle(cx2 - S(17), hy - S(2), cx2, hy - S(16), cx2 - S(2), hy - S(2), BLACK);
            t.fillTriangle(cx2 + S(2),  hy - S(2), cx2, hy - S(16), cx2 + S(17), hy - S(2), BLACK);
            t.fillRect(cx2 - S(16), hy - S(4), S(32), S(4), BLACK);
            t.fillCircle(cx2, hy - S(4), S(2), AMBER);
            // Eye patch strap only, not a filled patch, so the shades
            // underneath stay visible.
            t.drawWideLine(cx2 - S(11), hy + S(5), cx2 + S(14), hy + S(3), S(2), BLACK);
            break;
        }
        default: break;
    }
}

// Squachy's base design is ~68px tall (crest to shadow) at scale 1.0.
static const int BASE_HEIGHT = 68;

// Draws Squachy at an already-animated anchor (hy = head-top Y for this
// exact frame). Bob is computed once in tick() so it can also drive the
// dirty-rect clear that runs before this is called.
static void drawBody(TFT_eSPI& t, int cx, int hy, int headTopY, uint32_t now, Mood m, float scale,
                     bool forceTalking = false) {
    auto S = [scale](int v) { return (int)(v * scale); };
    int cx2 = cx;

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
    } else if (outfitNow == OutfitId::SHADOW) {
        // Mostly black -- furLight stays a touch lighter than pure
        // black purely so edges/highlights (ears, arm outlines) don't
        // vanish into a single flat silhouette.
        furMain  = blend(BLACK, WHITE, 12);
        furLight = blend(BLACK, WHITE, 32);
    }
    if (s_legendary && now < s_legendaryUntil) {
        float ph = (float)(now % 900) / 900.0f;
        furMain  = blend(CYAN, VAPOR_PINK, (uint16_t)(ph * 256.0f));
        furLight = blend(VAPOR_PINK, VAPOR_PURPLE, (uint16_t)(ph * 256.0f));
    }

    // Shadow (fixed, doesn't bob)
    t.fillEllipse(cx2, headTopY + S(62), S(18), S(4), blend(BG, FUR_DARK, 70));

    // Legs + big bigfoot feet — a simple alternating step lift while
    // walking (TFT_eSPI has no canvas-style transforms to pivot a real
    // leg swing on, so this just varies each leg's vertical offset in
    // opposition, which reads fine at this size). Static otherwise.
    if (m == Mood::WALK) {
        float legPhase = (float)(now % 400) / 400.0f * 6.2831853f;
        int legL = (int)(sinf(legPhase) * S(3));
        int legR = (int)(sinf(legPhase + 3.14159265f) * S(3));
        t.fillRect(cx2 - S(10), hy + S(40) + legL, S(8), S(10) - legL, furMain);
        t.fillRect(cx2 + S(2),  hy + S(40) + legR, S(8), S(10) - legR, furMain);
        t.fillRoundRect(cx2 - S(13), hy + S(49) + legL, S(12), S(6), 2, furLight);
        t.fillRoundRect(cx2 + S(1),  hy + S(49) + legR, S(12), S(6), 2, furLight);
    } else {
        t.fillRect(cx2 - S(10), hy + S(40), S(8), S(10), furMain);
        t.fillRect(cx2 + S(2),  hy + S(40), S(8), S(10), furMain);
        t.fillRoundRect(cx2 - S(13), hy + S(49), S(12), S(6), 2, furLight);
        t.fillRoundRect(cx2 + S(1),  hy + S(49), S(12), S(6), 2, furLight);
    }

    // Body — broad, stocky torso instead of a slim rounded rect.
    t.fillRoundRect(cx2 - S(15), hy + S(23), S(30), S(18), S(5), furMain);
    t.fillRect(cx2 - S(15), hy + S(23), S(5), S(18), furLight);
    t.fillRect(cx2 + S(10), hy + S(23), S(5), S(18), furLight);

    // Arms — long, ape-like, hanging past the waist. Depend on mood; a
    // SHOCKED reaction further varies pose by what triggered it. Static
    // hanging arms used to be the default for every mood except WAVE/
    // SHOCKED (IDLE, BOUNCE, WALK, SLEEPY, and standing still generally
    // all drew the exact same frozen pose) -- everything below except
    // SLEEPY now has some motion of its own so standing still never
    // reads as a paused animation.
    if (m == Mood::WAVE) {
        float wa = -1.0f + sinf((float)(now % 400) / 400.0f * 6.2831853f) * 0.5f;
        float ex = cx2 + S(13) + cosf(wa) * (18.0f * scale);
        float ey = hy + S(28) + sinf(wa) * (18.0f * scale);
        t.drawWideLine(cx2 + S(11), hy + S(28), ex, ey, S(7), furLight);
        t.fillRoundRect(cx2 - S(18), hy + S(22), S(8), S(22), S(3), furLight);
    } else if (m == Mood::SHOCKED) {
        // Fast small shake layered onto whichever pose reactPoseFor()
        // picks, so a flail reads as "can't hold still" instead of a
        // single frozen frame -- separate from the (slower, bigger)
        // panicked bodyCx dart above, which only kicks in for a caller
        // that opted into wanderRangePx.
        float shakeT = (float)(now % 140) / 140.0f * 6.2831853f;
        int shake = (int)(sinf(shakeT) * S(2));
        switch (reactPoseFor(s_reactType)) {
            case ReactPose::HANDS_UP:
                t.drawWideLine(cx2 - S(11), hy + S(26), cx2 - S(14) + shake, hy - S(10), S(7), furLight);
                t.drawWideLine(cx2 + S(11), hy + S(26), cx2 + S(14) + shake, hy - S(10), S(7), furLight);
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
                t.drawWideLine(cx2 - S(11), hy + S(26), cx2 - S(23) + shake, hy + S(10), S(7), furLight);
                t.drawWideLine(cx2 + S(11), hy + S(26), cx2 + S(23) + shake, hy + S(10), S(7), furLight);
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
        t.drawWideLine(cx2 - S(14), hy + S(20), cx2 - S(14) + armX, hy + S(44), S(8), furLight);
        t.drawWideLine(cx2 + S(14), hy + S(20), cx2 + S(14) + armX, hy + S(44), S(8), furLight);
    } else if (m == Mood::WALK) {
        // Opposite-arm-opposite-leg swing, same phase the legs above
        // already use (recomputed here rather than threaded through --
        // it's a pure function of `now`, so a duplicate one-liner costs
        // nothing and needs no shared state).
        float legPhase = (float)(now % 400) / 400.0f * 6.2831853f;
        int armL = (int)(sinf(legPhase + 3.14159265f) * S(4));
        int armR = (int)(sinf(legPhase) * S(4));
        t.fillRoundRect(cx2 - S(18), hy + S(22) + armL, S(8), S(22), S(3), furLight);
        t.fillRoundRect(cx2 + S(10), hy + S(22) + armR, S(8), S(22), S(3), furLight);
    } else if (m == Mood::SLEEPY) {
        // Static/droopy on purpose -- motion here would fight the
        // "tired" read the rest of this pose is going for.
        t.fillRoundRect(cx2 - S(18), hy + S(22), S(8), S(22), S(3), furLight);
        t.fillRoundRect(cx2 + S(10), hy + S(22), S(8), S(22), S(3), furLight);
    } else {
        // IDLE/BOUNCE -- gentle continuous opposing sway so just
        // standing there never reads as a frozen frame.
        float armPhase = (float)(now % 1800) / 1800.0f * 6.2831853f;
        int armSwingL = (int)(sinf(armPhase) * S(3));
        int armSwingR = (int)(sinf(armPhase + 3.14159265f) * S(3));
        t.fillRoundRect(cx2 - S(18), hy + S(22) + armSwingL, S(8), S(22), S(3), furLight);
        t.fillRoundRect(cx2 + S(10), hy + S(22) + armSwingR, S(8), S(22), S(3), furLight);
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

    // A tiny top hat, unlocked once he reaches Legend stage — perched
    // just above the crest peak (hy - S(14)). Skipped for the Unicorn
    // outfit specifically: its horn already occupies that exact spot,
    // and the two stacked together read as clutter rather than two
    // readable accessories.
    if (currentStage() == GrowthStage::LEGEND && currentOutfit() != OutfitId::UNICORN) {
        t.fillRoundRect(cx2 - S(9), hy - S(22), S(18), S(3), 1, BLACK);
        t.fillRect(cx2 - S(5), hy - S(30), S(10), S(9), BLACK);
        t.fillRect(cx2 - S(5), hy - S(24), S(10), S(2), VAPOR_PINK);
    }

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
        // Lens tint is a player-chosen cosmetic (Settings > SHADES
        // COLOR) rather than always cyan — see cycleShadesColor().
        static const uint16_t SHADE_TINTS[4] = { CYAN, VAPOR_PINK, GREEN, VAPOR_PURPLE };
        uint16_t shadeTint = SHADE_TINTS[s_shadeIdx % 4];
        bool blink = ((now / 2200) % 40) < 3;
        // Mood::WINK forces the left lens shut on its own, independent
        // of the normal both-eyes blink cycle -- a wink is one eye,
        // not two.
        bool winking = (m == Mood::WINK);
        uint16_t openLens = blend(BG, shadeTint, 60);
        uint16_t lensL = (blink || winking) ? BLACK : openLens;
        uint16_t lensR = blink ? BLACK : openLens;
        t.fillRoundRect(cx2 - S(12), hy + S(6), S(10), S(7), 2, BLACK);
        t.fillRoundRect(cx2 + S(2),  hy + S(6), S(10), S(7), 2, BLACK);
        t.fillRect(cx2 - S(2), hy + S(8), S(4), S(2), BLACK);
        t.fillRoundRect(cx2 - S(11), hy + S(7), S(8), S(5), 1, lensL);
        t.fillRoundRect(cx2 + S(3),  hy + S(7), S(8), S(5), 1, lensR);

        // A glint sweeps across each open lens (skipped while shut) so
        // the shades read as reflective glass instead of a flat fill.
        float sweep = (float)(now % 2400) / 2400.0f;
        int gx = (int)(sweep * 6.0f);
        if (lensL != BLACK) t.drawFastVLine(cx2 - S(11) + S(1 + gx), hy + S(7), S(4), WHITE);
        if (lensR != BLACK) t.drawFastVLine(cx2 + S(3)  + S(1 + gx), hy + S(7), S(4), WHITE);

        // A little cartoon "wink sparkle" beside the shut lens --
        // purely additive on top of the pose above rather than
        // touching its geometry, so it can never misalign with the
        // frame.
        if (winking) {
            int sx = cx2 - S(16), sy = hy + S(3);
            t.drawLine(sx - S(2), sy, sx + S(2), sy, WHITE);
            t.drawLine(sx, sy - S(2), sx, sy + S(2), WHITE);
        }

        // Mouth: resting smile most of the time, or an open/close
        // "talking" flap while a speech bubble is actually up.
        bool talking = forceTalking || (bubbleText && now < bubbleUntil);
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

    drawOutfit(t, cx2, hy, now, m, scale, outfitNow);
}

void drawWaving(TFT_eSPI& t, int cx, int baseY, uint32_t now, float scale, const char* line,
                bool talking, int wanderRangePx) {
    // Same idle bob as tick()'s WAVE mood, just without the quip/mood
    // state machine — a self-contained cameo for the boot splash.
    float bobAmt = 6.0f * scale;
    float bob = sinf((float)(now % 900) / 900.0f * 6.2831853f) * bobAmt;
    int headTopY = baseY - (int)(58.0f * scale);
    int hy = headTopY + (int)bob;

    // Continuous back-and-forth patrol, opted into by a caller that has
    // real width to spare (LOG's MORE INFO panel) -- reuses WALK_CYCLE_MS
    // for the same amble pace tick()'s own Mood::WALK uses, but runs
    // forever off a plain now%period instead of WALK's start/duration
    // window, since this cameo has no idle-mood scheduler ending it.
    int bodyCx = cx;
    if (wanderRangePx > 0) {
        float wt = (float)(now % WALK_CYCLE_MS) / (float)WALK_CYCLE_MS * 6.2831853f;
        bodyCx = cx + (int)(sinf(wt) * wanderRangePx);
    }

    drawBody(t, bodyCx, hy, headTopY, now, Mood::WAVE, scale, talking);
    // Fixed above his (pre-bob, pre-wander) head, same as tick()'s
    // bubble row — it shouldn't bounce or chase him around. Pulled up
    // further than tick()'s gap (18px) specifically so it sits right
    // under the "TALKING SASQUACH" subtitle above, in the extra room
    // this bigger boot-splash scale leaves between his head and the
    // subtitle.
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
          bool advance, float minScale, bool scanningFx, int wanderRangePx) {
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
    if (!s_onboardActive && mood == Mood::IDLE && now >= nextIdleAt) {
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
            moodUntil = now + 8000;
            nextIdleAt = now + 8000;
        } else if (s_napStart != 0) {
            // Just woke up from a capped nap -- back at it, and held
            // off from immediately napping again for a while even
            // though idleFor is still well past SLEEPY_AFTER_MS.
            s_napStart = 0;
            s_napCooldownUntil = now + SLEEPY_AFTER_MS;
            say(pick(BORED_LINES, 4), MIN_BUBBLE_MS);
            mood = random(0, 2) ? Mood::WAVE : Mood::BOUNCE;
            moodUntil = now + 1200;
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
        } else if (random(0, 8) == 0) {
            // A little dance break -- see drawBody()'s DANCE arm case.
            say(pick(DANCE_LINES, 4), 3200);
            mood = Mood::DANCE;
            moodUntil = now + 2400;
            nextIdleAt = now + 12000 + random(0, 18000);
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
            moodUntil = now + 1800;
            nextIdleAt = now + 12000 + random(0, 18000);
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
            } else if (haveHistory && random(0, 6) == 0) {
                say(buildStatLine(), MIN_BUBBLE_MS);
            } else if (random(0, 6) == 0) {
                say(pickBackgroundLine(), MIN_BUBBLE_MS);
            } else if (longIdle && random(0, 3) == 0) {
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
    } // if (advance)

    // Maximize Squachy's size to whatever vertical room the caller says
    // is free (title bar to status line), after reserving a row for the
    // speech bubble. Clamped to keep his proportions from getting
    // blocky-huge or unreadably tiny on extreme screen sizes. The
    // walkthrough's bubble is much taller than the usual one-liner, so
    // it reserves more of that room and he renders correspondingly
    // smaller for the duration — reading the explanation matters more
    // than his size right then.
    const int bubbleRowH = s_onboardActive ? ONBOARD_BUBBLE_H : 16;
    // The floor below normally keeps him from going below scale 1.0 --
    // minScale lets a specific call site (the mini-scan-screen cameo)
    // opt into a smaller floor without changing anyone else's default.
    // Deriving charAvail's own floor from minScale (rather than a flat
    // 40) keeps this a no-op for every existing caller: at minScale's
    // default of 1.0 that floor becomes BASE_HEIGHT itself, which the
    // scale clamp just below was already forcing the same end result
    // through regardless (any charAvail under BASE_HEIGHT still landed
    // on scale 1.0), so nothing about today's on-screen sizes changes.
    int charAvailFloor = (int)(BASE_HEIGHT * minScale);
    if (charAvailFloor < 8) charAvailFloor = 8;   // keep the division sane at extreme minScale
    int charAvail = availHeight - bubbleRowH;
    if (charAvail < charAvailFloor) charAvail = charAvailFloor;
    float scale = (float)charAvail / (float)BASE_HEIGHT;
    if (scale < minScale) scale = minScale;
    if (scale > 3.0f) scale = 3.0f;

    int headTopY = topY + bubbleRowH;

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
        int halfW = (int)(24 * scale);
        float maxRange = (float)(t.width() / 2 - halfW - 4);
        if (maxRange < 0) maxRange = 0;
        // WALK_CYCLES full sine cycles instead of one: 0 at the start,
        // out to one full edge, back through center, out to the other
        // full edge, then back to 0 -- repeated WALK_CYCLES times -- an
        // actual edge-to-edge patrol that still starts and ends exactly
        // at center, so there's no teleport when WALK expires back to
        // idle.
        bodyCx = cx + (int)(sinf(walkT * 6.2831853f * WALK_CYCLES) * maxRange * s_walkDir);
    } else if (mood == Mood::SHOCKED && wanderRangePx >= 0) {
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

    // Idle bob runs noticeably quicker than a resting breathing rate —
    // he should read as lively even when nothing's happening. Bob
    // amplitude scales with him so it stays proportional when he's big.
    float bobAmt   = (mood == Mood::BOUNCE) ? 9.0f : (mood == Mood::SLEEPY ? 1.5f : (mood == Mood::DANCE ? 6.0f : 3.0f));
    bobAmt *= scale;
    float bobSpeed = (mood == Mood::BOUNCE) ? 220.0f : (mood == Mood::SLEEPY ? 2200.0f : (mood == Mood::DANCE ? 300.0f : 1100.0f));
    float bob = sinf((float)(now % (uint32_t)bobSpeed) / bobSpeed * 6.2831853f) * bobAmt;
    if (mood == Mood::BOUNCE) bob = -fabsf(bob); // hop upward only
    int hy = headTopY + (int)bob;

    // Party mode draws first — a full wash across his region — so his
    // body and the hearts below land on top of it, not under it.
    if (s_legendary) drawPartyFx(t, now, topY, availHeight, advance);

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
    drawBody(t, bodyCx, hy, headTopY, now, mood, scale);
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
    bool showBubble = bubbleText && now < bubbleUntil;
    if (hadBubble) {
        t.fillRect(lastBubbleX, lastBubbleY, lastBubbleW, lastBubbleH, Theme::BG);
    }
    if (showBubble) {
        if (s_onboardActive) drawOnboardBubble(t, cx, topY, bubbleText, s_onboardStep, ONBOARD_N);
        else                 drawBubble(t, cx, topY, bubbleText);
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
