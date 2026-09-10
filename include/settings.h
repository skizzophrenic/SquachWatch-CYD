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
        DIGITAL = 0, STARFIELD = 1, TOASTERS = 2,
        AQUARIUM = 3, TERMINAL = 4, FIREFLIES = 5,
        FIRE = 6, SNOWFALL = 7, SPECTRUM = 8, TUNNEL = 9,
        SYNTHWAVE = 10,
        // No animation at all -- the band is filled flat and nothing moves.
        // Only reachable while BORING MODE is on, which is the mode that
        // already strips Squachy, the pet and the idle flourishes. It did
        // not strip the backdrop, so "boring" still meant a screen full of
        // falling glyphs; this is the rest of that thought.
        BLACK = 11
    };
    static const uint8_t BACKGROUND_COUNT = 12;

    // Whether a background can be reached from the BACKGROUND row right
    // now. Everything except BLACK always can.
    bool backgroundSelectable(Background b);
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

    // Disables the two CLEAR-screen gestures that cycle background
    // (the left/right edge-zone tap, and boring mode's "tap anywhere"
    // stand-in for it) without touching the Settings screen's own
    // BACKGROUND row -- once you've found a favorite, an accidental
    // edge tap during normal use shouldn't change it, but deliberately
    // opening Settings and picking a new one always should. Same
    // "disable the accidental-tap surface, not the deliberate menu
    // path" reasoning as rotationLocked(), just split across two call
    // sites instead of rotation's one (see main.cpp's CLEAR case).
    bool       backgroundLocked();
    void       toggleBackgroundLocked();

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

    // ---- POWER SAVER -----------------------------------------------------
    // Every one of these is an independent, manually chosen setting; the
    // master switch below only gates whether any of them are acted on, so
    // turning it off restores stock behaviour without losing the choices.
    //
    // Nothing here changes the display's SPI clock. That was the obvious
    // idea and it is the wrong one: the panel push is a blocking, polled
    // transfer, so halving the clock does not idle the chip, it keeps the
    // core awake twice as long for the same frame. The savings that are
    // real are the backlight, the idle frame rate, and the core clock.
    bool       powerSaver();
    void       togglePowerSaver();

    // Seconds of no touch before the backlight drops to dimLevel(). 0 = never.
    uint16_t   screenTimeoutSec();
    void       cycleScreenTimeout();

    // Duty the backlight falls to when it times out, 0..255. Allowed to go
    // to 0 (fully off) unlike brightness(), which has a floor of 32: this
    // one always comes back on the next touch, so it cannot strand anyone
    // in front of a black screen the way a dark brightness() could.
    uint8_t    dimLevel();
    void       adjustDimLevel(int8_t delta);

    // Frames per second the main loop is held to once idle. 0 = uncapped.
    uint8_t    idleFps();
    void       cycleIdleFps();

    // Seconds of no touch before the idle frame cap applies. Kept separate
    // from screenTimeoutSec() on purpose -- slowing the animation down is a
    // much smaller imposition than dimming the screen, so most people will
    // want it to happen sooner.
    uint16_t   idleAfterSec();
    void       cycleIdleAfter();

    // Core clock in MHz: 240, 160 or 80. Never below 80 -- the radio needs
    // an 80 MHz APB clock, and the display's SPI divisor and the UART's
    // baud divisor are both derived from it, so dropping under that would
    // take out scanning, the panel and the console together.
    uint16_t   cpuMhz();
    void       cycleCpuMhz();

    // Whether an alert pulls the backlight back up. On by default: a
    // detector that dims itself and then hides the alert it just found is
    // worse than useless.
    bool       wakeOnAlert();
    void       toggleWakeOnAlert();

    // What is CONFIGURED, ignoring the master switch. Only the power menu
    // wants these: it has to show you what you have chosen while the feature
    // is still switched off, which is the order most people will set it up in.
    uint16_t   screenTimeoutSecRaw();
    uint8_t    idleFpsRaw();
    uint16_t   cpuMhzRaw();

    // Minimum confidence an alert needs to interrupt with the ALERT
    // screen. LOW_CONF = no filtering (every match alerts, the
    // original behavior).
    Confidence  minConfidence();
    void        cycleMinConfidence();
    const char* minConfidenceLabel();

    // Per-type detection on/off (Settings > DETECTION FILTER). A
    // disabled type is dropped at the point it's first classified --
    // never logged, counted, or alerted on -- not just hidden after
    // the fact, and it isn't a scan-side filter (the radios can't be
    // told "ignore AirTags"; every advertisement is still received,
    // it's just discarded once matched against a disabled type). Off
    // by default nothing changes: every type starts enabled, so a
    // fresh install behaves exactly as it always has. UNKNOWN isn't
    // included -- it's the "matched a signature but not a specific
    // brand" fallback, not a type someone would want to blanket-mute.
    bool     typeEnabled(DetectionType t);

    // How big Squachy is drawn, as a percentage of the size the layout
    // would otherwise give him. SMALL 70, MEDIUM 85, LARGE 100.
    //
    // Only ever at or below 100. The two guards that size him -- one
    // keeping his crest on screen, one keeping a tall costume's overflow
    // inside a tenth of his height -- are closed-form solutions for the
    // full-size case, and every value below it is strictly more
    // conservative than what they solved for. Above 100 would invalidate
    // both, which is a different and much larger job.
#if SQUACH_MESH
    // The two halves of SquachMesh, separately switchable, because they are
    // genuinely different things to consent to.
    //
    // TRANSMIT is the privacy-relevant one and defaults OFF. This device
    // otherwise never transmits, which is written up as a feature; turning
    // it on makes it visible to anybody else's scanner and gives it an
    // identity that follows it around. That should be chosen, not inherited.
    //
    // DETECT also defaults off, so the feature as a whole does nothing until
    // it is asked to. One earlier attempt gated only transmit and left
    // detect always on, which was defensible and still wrong: a setting
    // whose label says off while Squachys keep arriving is a setting that
    // lies. Splitting them is the honest version of that argument -- somebody
    // who wants to watch without being seen can now say so.
    bool        meshDetect();
    // FALSE until the warning screen has been accepted, whatever the stored
    // TRANSMIT flag says. The masking lives in the getter rather than at the
    // call sites so there is exactly one place that can be wrong, and so a
    // preference left behind by an older build cannot start a radio the
    // owner of this one never agreed to.
    bool        meshTransmit();
    bool        meshConsent();
    void        setMeshConsent(bool v);
    const char* meshDetectLabel();
    const char* meshTransmitLabel();
    void        cycleMeshDetect();
    void        cycleMeshTransmit();
    // For the one-line summary on the Settings row that opens the menu.
    const char* meshSummary();
    // Which board the payphone screen shows. The keypad is the default and
    // the point; QWERTY is the bailout for people who hate multi-tap, and it
    // is remembered because somebody who hates it hates it every time.
    bool        phoneQwerty();
    void        togglePhoneQwerty();
    // Encrypted messages between SquachWatches that share a phrase. Off until
    // asked for. Reading one needs DETECT; sending one needs TRANSMIT, and so
    // sits behind the same consent gate.
    bool        messagesOn();
    void        toggleMessages();
    // Whether the messages tutorial has run. Set when it STARTS, so a
    // skipped tutorial counts as seen; "?" on the message screen replays it.
    bool        meshTutorSeen();
    void        setMeshTutorSeen();
#endif

    uint8_t     squachySizePct();
    const char* squachySizeLabel();
    void        cycleSquachySize();
    void     toggleType(DetectionType t);
    uint8_t  enabledTypeCount();   // for a Settings-row "12/14" summary
}
