// SquachWatch-CYD — persisted user settings implementation
#include "settings.h"
#include "theme.h"
#include <Preferences.h>

namespace Settings {

static Preferences s_prefs;
static uint8_t     s_palette    = 0;
static Background  s_background = Background::MATRIX;
static bool        s_inverted   = false;
static uint8_t     s_brightness = 255;
static Confidence  s_minConf    = Confidence::LOW_CONF;
static bool        s_boringMode = false;

const char* backgroundName(Background b) {
    switch (b) {
        case Background::MATRIX:    return "MATRIX RAIN";
        case Background::STARFIELD: return "STARFIELD";
        case Background::TOASTERS:  return "FLYING TOASTERS";
        case Background::LAVALAMP:  return "LAVA LAMP";
        case Background::AQUARIUM:  return "AQUARIUM";
        case Background::TERMINAL:  return "TERMINAL LOG";
        case Background::FIREFLIES: return "FIREFLIES";
        case Background::FIRE:      return "FIRE";
        case Background::SNOWFALL:  return "SNOWFALL";
        case Background::SPECTRUM:  return "RF SPECTRUM";
        case Background::TUNNEL:    return "WIREFRAME TUNNEL";
        default:                    return "?";
    }
}

void load() {
    s_prefs.begin("settings", false);
    s_palette    = (uint8_t)s_prefs.getUChar("pal", 0);
    if (s_palette >= Theme::PALETTE_COUNT) s_palette = 0;
    s_background = (Background)s_prefs.getUChar("bg", (uint8_t)Background::MATRIX);
    if ((uint8_t)s_background >= BACKGROUND_COUNT) s_background = Background::MATRIX;
    s_inverted   = s_prefs.getBool("inv", false);
    s_brightness = s_prefs.getUChar("bri", 255);
    if (s_brightness < 32) s_brightness = 32;
    s_minConf    = (Confidence)s_prefs.getUChar("conf", (uint8_t)Confidence::LOW_CONF);
    if ((uint8_t)s_minConf > (uint8_t)Confidence::HIGH_CONF) s_minConf = Confidence::LOW_CONF;
    s_boringMode = s_prefs.getBool("boring", false);

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

bool inverted() { return s_inverted; }

void toggleInvert() {
    s_inverted = !s_inverted;
    s_prefs.putBool("inv", s_inverted);
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

}
