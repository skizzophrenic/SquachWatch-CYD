// SquachWatch-CYD — persisted user settings implementation
#include "settings.h"
#include "theme.h"
#include <Preferences.h>

namespace Settings {

static Preferences s_prefs;
static uint8_t     s_palette    = 0;
static Background  s_background = Background::MATRIX;
static bool        s_inverted   = false;
static bool        s_rgbSwapped = false;
static bool        s_colorChecked = false;
static bool        s_infoPrimerShown = false;
static bool        s_rotationLocked = false;
static uint8_t     s_rotation = 1;
static bool        s_backgroundLocked = false;
// Bit N = DetectionType N enabled. UNKNOWN (0) is never included -- see
// typeEnabled()'s comment. Default has bits 1..(COUNT-1) set (every real
// type on), computed once at namespace-init time rather than a hand-
// maintained literal so it can never drift out of sync with COUNT.
static uint16_t    s_typeMask = 0;
static uint8_t     s_brightness = 255;
static Confidence  s_minConf    = Confidence::LOW_CONF;
static bool        s_boringMode = false;

const char* backgroundName(Background b) {
    switch (b) {
        case Background::MATRIX:    return "MATRIX RAIN";
        case Background::STARFIELD: return "STARFIELD";
        case Background::TOASTERS:  return "FLYING TOASTERS";
        case Background::AQUARIUM:  return "AQUARIUM";
        case Background::TERMINAL:  return "TERMINAL LOG";
        case Background::FIREFLIES: return "FIREFLIES";
        case Background::FIRE:      return "FIRE";
        case Background::SNOWFALL:  return "SNOWFALL";
        case Background::SPECTRUM:  return "RF SPECTRUM";
        case Background::TUNNEL:    return "WIREFRAME TUNNEL";
        case Background::SYNTHWAVE: return "SYNTHWAVE";
        default:                    return "?";
    }
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
    if ((uint8_t)s_background >= BACKGROUND_COUNT) s_background = Background::MATRIX;
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

    uint16_t allTypesOn = 0;
    for (uint8_t t = 1; t < (uint8_t)DetectionType::COUNT; t++) allTypesOn |= (uint16_t)(1u << t);
    s_typeMask = (uint16_t)s_prefs.getUInt("typemask", allTypesOn);
    // A mask saved by an older build only has bits for the types that
    // existed then, so every type added since would come back OFF for
    // anyone who had ever touched the TYPE FILTER screen -- a new
    // detection silently disabled on upgrade, which is the worst way
    // for it to fail. "typecount" records how many types the saved mask
    // was written against; anything above that is a type the user has
    // never had the chance to express an opinion about, so it defaults
    // on like it would for a fresh install.
    uint8_t savedCount = (uint8_t)s_prefs.getUInt("typecount", 0);
    if (savedCount && savedCount < (uint8_t)DetectionType::COUNT) {
        for (uint8_t t = savedCount; t < (uint8_t)DetectionType::COUNT; t++) {
            s_typeMask |= (uint16_t)(1u << t);
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

void cycleBackground() {
    s_background = (Background)(((uint8_t)s_background + 1) % BACKGROUND_COUNT);
    s_prefs.putUChar("bg", (uint8_t)s_background);
}

void cyclePrevBackground() {
    s_background = (Background)(((uint8_t)s_background + BACKGROUND_COUNT - 1) % BACKGROUND_COUNT);
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

bool typeEnabled(DetectionType t) {
    uint8_t idx = (uint8_t)t;
    if (idx == 0 || idx >= (uint8_t)DetectionType::COUNT) return true;  // UNKNOWN, or out of range -- never gated
    return (s_typeMask & (uint16_t)(1u << idx)) != 0;
}

void toggleType(DetectionType t) {
    uint8_t idx = (uint8_t)t;
    if (idx == 0 || idx >= (uint8_t)DetectionType::COUNT) return;
    s_typeMask ^= (uint16_t)(1u << idx);
    s_prefs.putUInt("typemask", s_typeMask);
    // Stamped alongside the mask so a later firmware can tell which
    // types this mask was written against -- see load()'s upgrade path.
    s_prefs.putUInt("typecount", (uint32_t)DetectionType::COUNT);
}

uint8_t enabledTypeCount() {
    uint8_t n = 0;
    for (uint8_t t = 1; t < (uint8_t)DetectionType::COUNT; t++) {
        if (s_typeMask & (uint16_t)(1u << t)) n++;
    }
    return n;
}

}
