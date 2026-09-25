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
        FIRE = 6, SNOWFALL = 7, SPECTRUM = 8,
        // RETIRED. WIREFRAME TUNNEL was the one background that never came
        // good: a tunnel's whole picture is its vanishing point, and Squachy
        // stands exactly there, so it only ever showed its corners. Ten
        // reworks were built and rendered before it was dropped.
        //
        // The VALUE stays. It is saved to NVS as a raw byte, so renumbering
        // would move every board that has SYNTHWAVE or BLACK saved onto a
        // different background. backgroundSelectable() keeps it out of the
        // picker and load() moves anyone who was sitting on it.
        TUNNEL = 9,
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
    // For the bench: this session only, NOT saved, so the owner's own pick
    // is back at the next boot. Refuses a background the menu cannot reach.
    bool       previewBackground(Background b);

    // Desk mode keeps its own background, remembered separately ("deskBg"),
    // so the scene you leave beside the keyboard is not the scene you carry
    // in your pocket. Until one is picked it follows the main one. While
    // deskActive(true) is set, background() answers with the desk's, so
    // every drawing routine works unchanged.
    void       deskActive(bool on);   // also remembered, so a board switched off on the desk comes back to it
    bool       deskWanted();
    void       cycleDeskBackground();
    void       cyclePrevDeskBackground();
    // The desk's own pick, for the DESK MODE page's row -- background() only
    // answers with it while the desk is actually up.
    Background deskBackground();

    // The desk clock. FONT: 0 the segment digits, 1 Bangers. SIZE: 0 small,
    // 1 medium (the size it has always been), 2 large. BACKDROP: what plays
    // inside its plate -- 0 plain, 1 digital rain, 2 snow, 3 flying toasters,
    // 4 fire, 5 starfield, 6 fireflies.
    uint8_t     clockFont();
    const char* clockFontName();
    void        cycleClockFont();
    uint8_t     clockSize();
    const char* clockSizeName();
    void        cycleClockSize();
    uint8_t     clockBackdrop();
    const char* clockBackdropName();
    void        cycleClockBackdrop();
    void        cyclePrevClockBackdrop();
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

    // The Legend top hat: worn unless taken off on the APPEARANCE page.
    bool       topHatShown();
    void       toggleTopHat();

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

    // The watch's two battery knobs, both only while POWER SAVER is on.
    // BLE LISTEN: how much of each 100 ms the Bluetooth receiver is open,
    // 25, 50 or 75 percent. The receiver is the watch's whole battery story
    // (about 100 mA whenever it is open), and a tracker announcing itself
    // every second or two is still caught within seconds at 25. Off the
    // watch this reads 75, the window every board has always run.
    uint8_t    bleListen();
    uint8_t    bleListenRaw();
    void       cycleBleListen();
    // SLEEP CPU: the core clock while the screen is asleep, 80, 160 or 240.
    // The screen-on clock is CPU CLOCK above; this one never goes above it.
    uint16_t   idleCpuMhz();
    uint16_t   idleCpuMhzRaw();
    void       cycleIdleCpu();

    // Whether an alert pulls the backlight back up. On by default: a
    // detector that dims itself and then hides the alert it just found is
    // worse than useless.
    bool       wakeOnAlert();
    void       toggleWakeOnAlert();
    // The T-Watch's buzz on an alert (the DRV2605 haptic motor). Kept on
    // every board so the settings file reads the same; only the watch has
    // a motor to use it.
    // One row: OFF, HIGH, MED, LOW. buzz() is "not OFF"; buzzStrength() is
    // 0 LOW, 1 MED, 2 HIGH, which picks the motor driver's effect library
    // (1 is meant for a 1.3 V motor, 2 for a 3 V one -- the watch's is 3 V),
    // the patterns, and at HIGH lifts the drive clamp a little too.
    bool       buzz();
    uint8_t    buzzStrength();
    const char* buzzModeName();
    void       cycleBuzz();
    // STEADY POWER on the watch: the power chip's DC1, which feeds the
    // ESP32 and its radio, held in PWM instead of dropping to PFM at light
    // load. A test for the deaf radios: PFM ripple is the kind of noise a
    // receiver hears first. Off by default; costs a few mA.
    bool       steadyPower();
    void       toggleSteadyPower();

    // The watch's radio duty cycle; see RADIO_DUTY_NAMES in settings.cpp.
    // 0 when the saver is off. Watch only.
    uint8_t     radioDuty();
    uint8_t     radioDutyRaw();
    const char* radioDutyName(uint8_t ix);
    void        cycleRadioDuty();

    // ---- STATUS LIGHT ----------------------------------------------------
    // The RGB LED on the back of the 2.8" CYD. See status_light.h for the
    // rules it follows; these are only the knobs. Every one of them is
    // independent: LIGHT is the master switch and the others keep their
    // values while it is off, the same shape as the power saver.
    bool        lightOn();
    void        toggleLight();
    bool        lightAlerts();           // detection flashes
    void        toggleLightAlerts();
    bool        lightMessages();         // the unread blink, and squad visits
    void        toggleLightMessages();
    uint8_t     lightIdle();             // 0 OFF, 1 BREATHE, 2 SOLID
    void        cycleLightIdle();

    // BANTER, on the APPEARANCE page: how much Squachy talks when nothing is
    // happening. 0 IMPORTANT (idle chatter off; he still speaks for a catch,
    // a message, a release, the daily hello), 1 LESS, 2 NORMAL, 3 MORE.
    // banterScale() is the multiplier on the idle roll's gaps.
    uint8_t     banter();
    const char* banterName();
    void        cycleBanter();
    float       banterScale();
    const char* lightIdleName();
    uint8_t     lightColor();            // 0 THEME, 1..9 fixed colours, 10 BACKGROUND
    void        cycleLightColor();
    const char* lightColorName();
    uint8_t     lightBrightness();       // 1..5, caps everything
    void        cycleLightBrightness();

    // REMOTE UPDATE: whether a squad update nudge over SquachMesh may start
    // an update on this board. OFF by default -- a board in a pocket that
    // reboots on its own is a surprise nobody signed up for -- and on the
    // SECURITY screen, because it is about who can do things to this board.
    bool        remoteUpdate();
    void        toggleRemoteUpdate();

    // UPDATE CHECK: whether the board joins its saved WiFi for a few seconds
    // at boot to ask the site for a newer release. Tells, never installs.
    bool        updateCheck();
    void        toggleUpdateCheck();

    // TIME ZONE: an index into Clock's zone table, applied at load and on
    // every change. UTC until somebody picks one; timeZoneChosen() says
    // whether anybody has, so Squachy can ask once.
    uint8_t     timeZone();
    const char* timeZoneName();
    void        cycleTimeZone();
    void        stepTimeZone(int dir);      // the card's arrows: steps without choosing
    void        setTimeZone(uint8_t i);     // from the ZONE serial command
    void        markTimeZoneChosen();       // THIS IS RIGHT on the card
    bool        timeZoneChosen();

    // SHOW PHRASE: whether this board ever prints its five words. OFF makes
    // the squad invite-only from this board's side: nobody can read the
    // phrase off it, so the only way in is ADD TO SQUAD, in person. ON by
    // default, which is what every board did before the switch existed.
    bool        phraseShown();
    void        togglePhraseShown();

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

    // AUTO SNOOZE: how many times one device may interrupt with the full
    // ALERT screen before it has to earn the next one by coming CLOSER.
    // 0 is off. Nothing is actually silenced -- see Detection::quietBar --
    // which matters on a device whose job is telling you what is near you.
    // On the watch, POWER SAVER makes it five at most.
    uint8_t     autoQuietAfter();
    void        cycleAutoQuiet();
    const char* autoQuietLabel();

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
    // CROWD: how many SquachWatches may be on screen at once, roaming rather
    // than standing. 1 is the old behaviour -- one visitor, both of them on
    // the ground. Measured on hardware: eight at the size they shrink to cost
    // LESS to draw than the two at today's size do (17.6 ms against 29).
    //
    // Any number from one to eight, and EIGHT IS NOT ARBITRARY: the radio's
    // squad ring holds eight (SQUAD_N in mesh.cpp), so a ninth board in the
    // room evicts the first. Raising this without raising that would offer a
    // number the hardware cannot hear.
    //
    // Up to four share one row; past four they take two or three, because
    // five across reads as a queue and four across still reads as a group.
    uint8_t     meshCrowd();
    const char* meshCrowdLabel();
    void        cycleMeshCrowd();
    // The DESK MODE page's own squad settings, apart from the main screen's.
    //
    // deskSquad: whether the squad turns up under the clock at all. Off by
    // default: the desk is a clock, and somebody who wants a crowd on the
    // main screen does not necessarily want one under the time.
    // deskCrowd: HOW MANY for the desk, the same values as meshCrowd(). Until
    // it is set it follows the main screen's, which is what it used to share.
    // deskFullVisit: with one visitor, the main screen's whole visit (walk
    // in, high five, set pieces, emotes) rather than the two of them chatting
    // in place, bigger, with their words up on the clock.
    bool        deskSquad();
    void        toggleDeskSquad();
    uint8_t     deskCrowd();
    const char* deskCrowdLabel();
    void        cycleDeskCrowd();
    bool        deskFullVisit();
    void        toggleDeskFullVisit();
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

    // The mascot's pace, chosen by eye on a real board (PACE N / TEMPO P on
    // the console) and kept. Pace: milliseconds between his steps. Tempo: a
    // percentage on every duration of his -- moods, bubbles, idle gags, the
    // length of a drop or a stretch -- 100 as written, 70 slower.
    uint16_t    mascotPaceMs();
    void        setMascotPaceMs(uint16_t ms);
    uint8_t     mascotTempoPct();
    void        setMascotTempoPct(uint8_t pct);

    uint8_t     squachySizePct();
    const char* squachySizeLabel();
    void        cycleSquachySize();
    void     toggleType(DetectionType t);
    uint8_t  enabledTypeCount();   // for a Settings-row "12/14" summary

    // How far along an unfinished easter-egg hunt is, kept across restarts.
    // The unlocks themselves have always been saved; this is the PROGRESS
    // towards one, which used to live in memory only -- so a restart in the
    // middle of a hunt that takes minutes threw the hunt away.
    //
    // One entry, four bytes, one counter each. Only hunts that span minutes
    // belong here: the lodge knocks and the moon taps are drum rolls with a
    // 2.5 second window between taps, so they cannot survive a restart by
    // design and are deliberately NOT in it.
    enum class Hunt : uint8_t { EYE_STREAK = 0, COUNT };
    uint8_t  huntProgress(Hunt h);
    void     setHuntProgress(Hunt h, uint8_t v);   // writes only on a change
}
