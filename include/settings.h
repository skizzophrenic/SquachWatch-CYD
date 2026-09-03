// SquachWatch-CYD — persisted user settings (theme, background style,
// display invert/brightness, alert confidence filter). Everything here
// is a plain value store: it owns NVS persistence and, where it makes
// sense (palette), applies the change itself. Hardware side effects
// that need the TFT/backlight objects (tft.invertDisplay, ledcWrite)
// stay in main.cpp, which reads these getters after a change.
#pragma once
#include <stdint.h>
#include "signatures.h"   // Confidence

namespace Settings {
    enum class Background : uint8_t {
        MATRIX = 0, STARFIELD = 1, TOASTERS = 2,
        AQUARIUM = 3, TERMINAL = 4, FIREFLIES = 5,
        FIRE = 6, SNOWFALL = 7, SPECTRUM = 8, TUNNEL = 9
    };
    static const uint8_t BACKGROUND_COUNT = 10;
    const char* backgroundName(Background b);

    // Reads all fields from NVS (namespace "settings"), falling back to
    // defaults for anything never saved. Call once at boot, before
    // Theme::applyPalette()/tft.invertDisplay()/backlight setup so the
    // very first frame already reflects a saved choice.
    void load();

    uint8_t    paletteIndex();
    void       cyclePalette();       // advances+wraps, persists, applies to Theme

    Background background();
    void       cycleBackground();    // advances+wraps, persists
    void       cyclePrevBackground(); // same, but backward

    bool       inverted();
    void       toggleInvert();       // persists only — caller applies tft.invertDisplay()

    // "Wrong RGB/BGR order" fix for CYD boards whose panel batch
    // disagrees with the header's TFT_RGB_ORDER guess (colors read
    // swapped -- red shows as blue, etc.) -- unrelated to inverted()
    // above, which flips light/dark, not color channels. Persists
    // only — caller reissues the panel's MADCTL byte (see main.cpp's
    // applyColorOrder()), same XOR-against-a-compile-time-baseline
    // convention toggleInvert() already uses.
    bool       rgbSwapped();
    void       toggleRgbSwap();

    // Whether the first-boot color-check screen (see ui_colorcheck.h)
    // has ever been completed -- gates whether it auto-shows right
    // after the boot splash. Settings' own "CHECK COLORS" row can
    // re-enter that screen on demand afterward regardless of this.
    bool       colorChecked();
    void       markColorChecked();

    // Whether the RSSI/confidence primer (see DetectionInfo::
    // rssiConfidencePrimer()) has ever been shown -- gates it to
    // appear exactly once, the first time anyone taps MORE INFO on a
    // LOG entry, before that entry's own explanation.
    bool       infoPrimerShown();
    void       markInfoPrimerShown();

    // Disables the title-bar rotate button (and the ROTATED gesture it
    // triggers) without touching its icon -- an accidental tap during
    // BLE/WiFi scanning restarts the frame buffer for the new shape,
    // which some users would rather just not risk once they've settled
    // on an orientation.
    bool       rotationLocked();
    void       toggleRotationLock();

    // Last rotation (0..3, TFT_eSPI's setRotation() values) the rotate
    // button left the screen on -- so it comes back up the same way
    // after a power cycle instead of always resetting to the board's
    // compile-time default. AWOK has no rotate button and never touches
    // this (see main.cpp's setup()/rotate handler, both guarded
    // #if !defined(AWOK)).
    uint8_t    rotation();
    void       saveRotation(uint8_t r);

    // "Boring mode" — all detection features stay exactly as they are,
    // this only turns off Squachy's on-screen presence (the CLEAR-
    // screen mascot/animations/speech bubbles and the boot-splash
    // cameo) for anyone who just wants a plain detector.
    bool       boringMode();
    void       toggleBoringMode();

    // 32..255 — floor keeps the backlight from going fully dark and
    // unreadable via the settings screen itself.
    uint8_t    brightness();
    void       adjustBrightness(int8_t delta);   // clamps, persists

    // Minimum confidence an alert needs to interrupt with the ALERT
    // screen. LOW_CONF = no filtering (every match alerts, the
    // original behavior).
    Confidence  minConfidence();
    void        cycleMinConfidence();
    const char* minConfidenceLabel();
}
