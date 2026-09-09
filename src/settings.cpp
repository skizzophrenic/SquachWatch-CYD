// SquachWatch-CYD — persisted user settings implementation
#include "settings.h"
#include "theme.h"
#include <Preferences.h>

namespace Settings {

static Preferences s_prefs;
static uint8_t     s_palette    = 0;
static Background  s_background = Background::DIGITAL;
static bool        s_inverted   = false;
static bool        s_rgbSwapped = false;
static bool        s_colorChecked = false;
static bool        s_infoPrimerShown = false;
static bool        s_rotationLocked = false;
static uint8_t     s_rotation = 1;
static bool        s_backgroundLocked = false;
// Bit N = DetectionType N enabled. UNKNOWN (0) is never included -- see
// Types that arrive switched OFF, on a fresh install and on upgrade alike.
//
// Only IBEACON so far, and it is not a judgement about how interesting they
// are -- it is about how MANY. Proximity beacons are bolted to shelves in
// their dozens; one shop can put more of them in range than this device
// would otherwise see all week, and the ALERT screen is gated on confidence
// rather than on type, so with an exact-match signature every one of them
// would take over the display. Off by default, one tap away in DETECTION
// FILTER, and everything about the detection itself is honest either way.
static const uint32_t DEFAULT_OFF = (1u << (uint8_t)DetectionType::IBEACON);

// typeEnabled()'s comment. Default has bits 1..(COUNT-1) set (every real
// type on), computed once at namespace-init time rather than a hand-
// maintained literal so it can never drift out of sync with COUNT.
// 32-bit, not 16. DetectionType::COUNT reached 17 when IBEACON was added
// (and 18 with HACKER),
// and bit 16 does not exist in a uint16_t -- the shift is undefined and the
// last type silently loses its switch. NVS has always stored this through
// putUInt/getUInt, so the saved format is unchanged and nothing migrates.
static uint32_t    s_typeMask = 0;
// MEDIUM (85%). Only ever consulted on a board that has never been told
// otherwise -- anyone who has touched the SIZE row has a stored value and
// keeps it, which is why changing this default is safe.
static uint8_t     s_sqSizeIx = 1;
static uint8_t     s_brightness = 255;
static Confidence  s_minConf    = Confidence::LOW_CONF;
static bool        s_boringMode = false;

// ---- power saver ---------------------------------------------------------
// Indices into the tables below rather than raw values, so the menu can
// cycle them without knowing what the steps are and a saved index stays
// valid if a step is ever inserted.
static const uint16_t SCREEN_TIMEOUTS[] = { 0, 15, 30, 60, 120, 300 };
static const uint8_t  SCREEN_TIMEOUTS_N = sizeof(SCREEN_TIMEOUTS) / sizeof(SCREEN_TIMEOUTS[0]);
static const uint8_t  IDLE_FPS[]        = { 0, 20, 12, 8, 5 };
static const uint8_t  IDLE_FPS_N        = sizeof(IDLE_FPS) / sizeof(IDLE_FPS[0]);
static const uint16_t IDLE_AFTER[]      = { 5, 10, 20, 30, 60 };
static const uint8_t  IDLE_AFTER_N      = sizeof(IDLE_AFTER) / sizeof(IDLE_AFTER[0]);
static const uint16_t CPU_MHZ[]         = { 240, 160, 80 };
static const uint8_t  CPU_MHZ_N         = sizeof(CPU_MHZ) / sizeof(CPU_MHZ[0]);

static bool     s_powerSaver   = false;
static uint8_t  s_scrTimeoutIx = 2;    // 30 s
static uint8_t  s_dimLevel     = 16;   // ~6%, dim but not off
static uint8_t  s_idleFpsIx    = 2;    // 12 fps
static uint8_t  s_idleAfterIx  = 1;    // 10 s
static uint8_t  s_cpuIx        = 0;    // 240 MHz, the stock clock
static bool     s_wakeOnAlert  = true;

const char* backgroundName(Background b) {
    switch (b) {
        case Background::DIGITAL:    return "DIGITAL RAIN";
        case Background::STARFIELD: return "STARFIELD";
        case Background::TOASTERS:  return "FLYING TOASTERS";
        case Background::AQUARIUM:  return "AQUARIUM";
        case Background::TERMINAL:  return "TERMINAL LOG";
        case Background::FIREFLIES: return "FIREFLIES";
        case Background::FIRE:      return "FIRE";
        case Background::SNOWFALL:  return "SNOWFALL";
        case Background::SPECTRUM:  return "THE GIBSON";
        case Background::TUNNEL:    return "WIREFRAME TUNNEL";
        case Background::SYNTHWAVE: return "SYNTHWAVE";
        case Background::BLACK:     return "BLACK";
        default:                    return "?";
    }
}

// Every getter reports the STOCK value when the master switch is off, so
// the rest of the firmware never has to ask twice: one call tells it both
// whether the feature is on and what to do.
bool     powerSaver()       { return s_powerSaver; }
uint16_t screenTimeoutSec() { return s_powerSaver ? SCREEN_TIMEOUTS[s_scrTimeoutIx] : 0; }
uint8_t  dimLevel()         { return s_dimLevel; }
uint8_t  idleFps()          { return s_powerSaver ? IDLE_FPS[s_idleFpsIx] : 0; }
uint16_t idleAfterSec()     { return IDLE_AFTER[s_idleAfterIx]; }
uint16_t cpuMhz()           { return s_powerSaver ? CPU_MHZ[s_cpuIx] : 240; }
bool     wakeOnAlert()      { return s_wakeOnAlert; }

uint16_t screenTimeoutSecRaw() { return SCREEN_TIMEOUTS[s_scrTimeoutIx]; }
uint8_t  idleFpsRaw()          { return IDLE_FPS[s_idleFpsIx]; }
uint16_t cpuMhzRaw()           { return CPU_MHZ[s_cpuIx]; }

void togglePowerSaver() {
    s_powerSaver = !s_powerSaver;
    s_prefs.putBool("pwrOn", s_powerSaver);
}
void cycleScreenTimeout() {
    s_scrTimeoutIx = (uint8_t)((s_scrTimeoutIx + 1) % SCREEN_TIMEOUTS_N);
    s_prefs.putUChar("pwrScrnT", s_scrTimeoutIx);
}
void adjustDimLevel(int8_t delta) {
    int v = (int)s_dimLevel + delta;
    if (v < 0)   v = 0;
    if (v > 128) v = 128;          // past half brightness it is not dim any more
    s_dimLevel = (uint8_t)v;
    s_prefs.putUChar("pwrDim", s_dimLevel);
}
void cycleIdleFps() {
    s_idleFpsIx = (uint8_t)((s_idleFpsIx + 1) % IDLE_FPS_N);
    s_prefs.putUChar("pwrFps", s_idleFpsIx);
}
void cycleIdleAfter() {
    s_idleAfterIx = (uint8_t)((s_idleAfterIx + 1) % IDLE_AFTER_N);
    s_prefs.putUChar("pwrIdleT", s_idleAfterIx);
}
void cycleCpuMhz() {
    s_cpuIx = (uint8_t)((s_cpuIx + 1) % CPU_MHZ_N);
    s_prefs.putUChar("pwrCpu", s_cpuIx);
}
void toggleWakeOnAlert() {
    s_wakeOnAlert = !s_wakeOnAlert;
    s_prefs.putBool("pwrWake", s_wakeOnAlert);
}

void load() {
    s_prefs.begin("settings", false);
    s_palette    = (uint8_t)s_prefs.getUChar("pal", 0);
    if (s_palette >= Theme::PALETTE_COUNT) s_palette = 0;
    // SYNTHWAVE is the default a fresh device comes up on -- it is the
    // scene the boot splash already uses, so first power-on flows from
    // the splash into the same sunset instead of switching to something
    // else the moment onboarding ends. Only affects installs with no
    // saved value; anyone who has ever picked a background keeps theirs.
    s_background = (Background)s_prefs.getUChar("bg", (uint8_t)Background::SYNTHWAVE);
    if ((uint8_t)s_background >= BACKGROUND_COUNT) s_background = Background::DIGITAL;
    s_inverted   = s_prefs.getBool("inv", false);
    s_rgbSwapped = s_prefs.getBool("rgbswap", false);
    s_colorChecked = s_prefs.getBool("colorchk", false);
    s_infoPrimerShown = s_prefs.getBool("infoprimer", false);
    s_rotationLocked = s_prefs.getBool("rotlock", false);
    s_rotation = s_prefs.getUChar("rot", 1);
    if (s_rotation > 3) s_rotation = 1;
    s_backgroundLocked = s_prefs.getBool("bglock", false);
    s_brightness = s_prefs.getUChar("bri", 255);
    if (s_brightness < 32) s_brightness = 32;
    s_minConf    = (Confidence)s_prefs.getUChar("conf", (uint8_t)Confidence::LOW_CONF);
    if ((uint8_t)s_minConf > (uint8_t)Confidence::HIGH_CONF) s_minConf = Confidence::LOW_CONF;
    s_boringMode = s_prefs.getBool("boring", false);
    // BLACK only exists while boring mode does. A board that saved it and
    // then had boring mode turned off -- or one restored from someone
    // else's settings -- would otherwise boot to a flat screen with the
    // option missing from the ring, and no way to cycle out of it.
    if (!s_boringMode && s_background == Background::BLACK) {
        s_background = Background::DIGITAL;
    }
    s_powerSaver   = s_prefs.getBool("pwrOn", false);
    s_scrTimeoutIx = s_prefs.getUChar("pwrScrnT", 2);
    s_dimLevel     = s_prefs.getUChar("pwrDim", 16);
    s_idleFpsIx    = s_prefs.getUChar("pwrFps", 2);
    s_idleAfterIx  = s_prefs.getUChar("pwrIdleT", 1);
    s_cpuIx        = s_prefs.getUChar("pwrCpu", 0);
    s_wakeOnAlert  = s_prefs.getBool("pwrWake", true);
    // A saved index from a build with more steps than this one must not walk
    // off the end of the table.
    if (s_scrTimeoutIx >= SCREEN_TIMEOUTS_N) s_scrTimeoutIx = 2;
    if (s_idleFpsIx    >= IDLE_FPS_N)        s_idleFpsIx    = 2;
    if (s_idleAfterIx  >= IDLE_AFTER_N)      s_idleAfterIx  = 1;
    if (s_cpuIx        >= CPU_MHZ_N)         s_cpuIx        = 0;
    if (s_dimLevel     > 128)                s_dimLevel     = 16;

    // Every real type defaults ON except the ones in DEFAULT_OFF below.
    uint32_t allTypesOn = 0;
    for (uint8_t t = 1; t < (uint8_t)DetectionType::COUNT; t++) allTypesOn |= (1u << t);
    allTypesOn &= ~DEFAULT_OFF;
    s_typeMask = s_prefs.getUInt("typemask", allTypesOn);
    // A mask saved by an older build only has bits for the types that
    // existed then, so every type added since would come back OFF for
    // anyone who had ever touched the TYPE FILTER screen -- a new
    // detection silently disabled on upgrade, which is the worst way
    // for it to fail. "typecount" records how many types the saved mask
    // was written against; anything above that is a type the user has
    // never had the chance to express an opinion about, so it defaults
    // on like it would for a fresh install.
    s_sqSizeIx = (uint8_t)s_prefs.getUInt("sqsize", 1);
    if (s_sqSizeIx > 2) s_sqSizeIx = 2;

    uint8_t savedCount = (uint8_t)s_prefs.getUInt("typecount", 0);
    if (savedCount && savedCount < (uint8_t)DetectionType::COUNT) {
        for (uint8_t t = savedCount; t < (uint8_t)DetectionType::COUNT; t++) {
            if (DEFAULT_OFF & (1u << t)) continue;   // arrives off, like a fresh install
            s_typeMask |= (1u << t);
        }
        s_prefs.putUInt("typemask", s_typeMask);
        s_prefs.putUInt("typecount", (uint32_t)DetectionType::COUNT);
    }

    Theme::applyPalette(s_palette);
}

uint8_t paletteIndex() { return s_palette; }

void cyclePalette() {
    s_palette = (uint8_t)((s_palette + 1) % Theme::PALETTE_COUNT);
    s_prefs.putUChar("pal", s_palette);
    Theme::applyPalette(s_palette);
}

Background background() { return s_background; }

bool backgroundSelectable(Background b) {
    // BLACK is the only conditional one, and it is gated on boring mode
    // rather than hidden behind a second setting: somebody who has already
    // turned the mascot off is exactly the person who wants the option, and
    // nobody else would go looking for it.
    return (b != Background::BLACK) || s_boringMode;
}

// Both directions skip anything not currently selectable, so BLACK simply
// is not in the ring until boring mode puts it there. Bounded by
// BACKGROUND_COUNT rather than looping until it finds one: if a future
// change ever made everything unselectable this would spin forever, and a
// hung UI is a worse failure than a background that will not change.
void cycleBackground() {
    for (uint8_t i = 0; i < BACKGROUND_COUNT; i++) {
        s_background = (Background)(((uint8_t)s_background + 1) % BACKGROUND_COUNT);
        if (backgroundSelectable(s_background)) break;
    }
    s_prefs.putUChar("bg", (uint8_t)s_background);
}

void cyclePrevBackground() {
    for (uint8_t i = 0; i < BACKGROUND_COUNT; i++) {
        s_background = (Background)(((uint8_t)s_background + BACKGROUND_COUNT - 1) % BACKGROUND_COUNT);
        if (backgroundSelectable(s_background)) break;
    }
    s_prefs.putUChar("bg", (uint8_t)s_background);
}

bool inverted() { return s_inverted; }

void toggleInvert() {
    s_inverted = !s_inverted;
    s_prefs.putBool("inv", s_inverted);
}

bool rgbSwapped() { return s_rgbSwapped; }

void toggleRgbSwap() {
    s_rgbSwapped = !s_rgbSwapped;
    s_prefs.putBool("rgbswap", s_rgbSwapped);
}

bool colorChecked() { return s_colorChecked; }

void markColorChecked() {
    s_colorChecked = true;
    s_prefs.putBool("colorchk", true);
}

bool infoPrimerShown() { return s_infoPrimerShown; }

void markInfoPrimerShown() {
    s_infoPrimerShown = true;
    s_prefs.putBool("infoprimer", true);
}

bool rotationLocked() { return s_rotationLocked; }

void toggleRotationLock() {
    s_rotationLocked = !s_rotationLocked;
    s_prefs.putBool("rotlock", s_rotationLocked);
}

uint8_t rotation() { return s_rotation; }

void saveRotation(uint8_t r) {
    s_rotation = r;
    s_prefs.putUChar("rot", r);
}

bool backgroundLocked() { return s_backgroundLocked; }

void toggleBackgroundLocked() {
    s_backgroundLocked = !s_backgroundLocked;
    s_prefs.putBool("bglock", s_backgroundLocked);
}

bool boringMode() { return s_boringMode; }

void toggleBoringMode() {
    s_boringMode = !s_boringMode;
    s_prefs.putBool("boring", s_boringMode);
    // Turning boring mode off strands anyone sitting on BLACK: the option
    // is gone from the ring, and without this they would be looking at a
    // flat screen with no way to cycle out of it.
    if (!s_boringMode && s_background == Background::BLACK) {
        s_background = Background::DIGITAL;
        s_prefs.putUChar("bg", (uint8_t)s_background);
    }
}

uint8_t brightness() { return s_brightness; }

void adjustBrightness(int8_t delta) {
    int16_t v = (int16_t)s_brightness + delta;
    if (v < 32) v = 32;
    if (v > 255) v = 255;
    s_brightness = (uint8_t)v;
    s_prefs.putUChar("bri", s_brightness);
}

Confidence minConfidence() { return s_minConf; }

void cycleMinConfidence() {
    uint8_t next = (uint8_t)s_minConf + 1;
    if (next > (uint8_t)Confidence::HIGH_CONF) next = 0;
    s_minConf = (Confidence)next;
    s_prefs.putUChar("conf", next);
}

const char* minConfidenceLabel() {
    switch (s_minConf) {
        case Confidence::LOW_CONF:  return "ALL";
        case Confidence::MED_CONF:  return "MED+";
        case Confidence::HIGH_CONF: return "HIGH ONLY";
        default:                    return "?";
    }
}

// SMALL / MEDIUM / LARGE. LARGE is 100 and is the default, so a board that
// has never been told otherwise draws him exactly as it always did.
static const uint8_t     SQ_SIZE_PCT[3]   = { 70, 85, 100 };
static const char* const SQ_SIZE_LABEL[3] = { "SMALL", "MEDIUM", "LARGE" };
static const uint8_t     SQ_SIZE_N        = 3;

uint8_t squachySizePct() {
    return SQ_SIZE_PCT[s_sqSizeIx < SQ_SIZE_N ? s_sqSizeIx : (uint8_t)(SQ_SIZE_N - 1)];
}
const char* squachySizeLabel() {
    return SQ_SIZE_LABEL[s_sqSizeIx < SQ_SIZE_N ? s_sqSizeIx : (uint8_t)(SQ_SIZE_N - 1)];
}
void cycleSquachySize() {
    s_sqSizeIx = (uint8_t)((s_sqSizeIx + 1) % SQ_SIZE_N);
    s_prefs.putUInt("sqsize", s_sqSizeIx);
}

bool typeEnabled(DetectionType t) {
    uint8_t idx = (uint8_t)t;
    if (idx == 0 || idx >= (uint8_t)DetectionType::COUNT) return true;  // UNKNOWN, or out of range -- never gated
    return (s_typeMask & (1u << idx)) != 0;
}

void toggleType(DetectionType t) {
    uint8_t idx = (uint8_t)t;
    if (idx == 0 || idx >= (uint8_t)DetectionType::COUNT) return;
    s_typeMask ^= (1u << idx);
    s_prefs.putUInt("typemask", s_typeMask);
    // Stamped alongside the mask so a later firmware can tell which
    // types this mask was written against -- see load()'s upgrade path.
    s_prefs.putUInt("typecount", (uint32_t)DetectionType::COUNT);
}

uint8_t enabledTypeCount() {
    uint8_t n = 0;
    for (uint8_t t = 1; t < (uint8_t)DetectionType::COUNT; t++) {
        if (s_typeMask & (1u << t)) n++;
    }
    return n;
}

}
