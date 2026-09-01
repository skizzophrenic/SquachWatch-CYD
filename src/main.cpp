// SquachWatch-CYD — main firmware
// Wires the state machine (DESIGN.md §9) across the UI modules
// and the DetectionEngine.

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <Preferences.h>  // AWOK's own per-rotation touch-cal storage; see the AWOK block below pollTouch()'s globals
#include "state.h"
#include "theme.h"
#include "detection.h"
#include "ui_boot.h"
#include "ui_clear.h"
#include "ui_alert.h"
#include "ui_log.h"
#include "ui_settings.h"
#include "ui_diary.h"
#include "ui_outfit.h"
#include "squachy.h"
#include "cap_touch.h"
#include "touch_cal.h"
#include "settings.h"
#include "signatures.h"

// Two CYD board variants are supported from this one firmware:
//   - jczn_2432s028r (original): resistive XPT2046 touch on its own
//     dedicated SPI bus, backlight on GPIO21. Confirmed against
//     Espressif's official board-variant file for this exact board
//     (arduino-esp32 variants/jczn_2432s028r/pins_arduino.h):
//       display: DC=2 MISO=12 MOSI=13 SCK=14 CS=15 BL=21  (VSPI, via TFT_eSPI)
//       touch:   CS=33 IRQ=36 SCK=25  MOSI=32 MISO=39      (independent bus)
//   - JC2432W328C: capacitive CST816/CST820 touch over I2C, backlight
//     on GPIO27. Confirmed empirically against a physical unit: same
//     display driver/pins as above, touch chip answers at I2C 0x15 on
//     SDA=33/SCL=32 with a reset pulse on GPIO25.
// Both variants route their touch controller through the same
// GPIO25/32/33 trio (SPI vs I2C), so probing for the I2C chip at boot
// tells us which board this is — see setup().
//   - ESP32-3248S035R (3.5", built separately as env:cyd35): resistive
//     XPT2046 again, CS=33 sharing the *display's* SPI bus (SCK/MOSI/
//     MISO = 14/13/12) instead of getting a dedicated peripheral —
//     this board has no capacitive-touch chip at all, so the I2C probe
//     below is skipped entirely rather than just failing. Driven
//     through TFT_eSPI's own calibrateTouch/setTouch/getTouch path,
//     same as AWOK below, not the standalone XPT2046_Touchscreen
//     library — a separate SPIClass on the same physical bus as
//     TFT_eSPI produced constant garbage reads and a free-running IRQ
//     when that was tried. Unlike AWOK this board keeps its rotate
//     button, so it keeps a 4-slot (one per rotation) calibration
//     cache instead of AWOK's single blob — see the CYD35 branches in
//     setup()/pollTouch() and cyd35EnsureCal()'s comment.
//   - AWOK 2.4" (Marauder V6.1, built separately as env:awok): resistive
//     XPT2046 sharing the display's VSPI bus like cyd35, but driven
//     entirely through TFT_eSPI's own calibrateTouch/setTouch/getTouch
//     path rather than the XPT2046_Touchscreen library — see the AWOK
//     branches in setup()/pollTouch(). The constructor still gets
//     built below regardless of board (it costs nothing unused), but
//     neither AWOK nor cyd35 ever calls touch.begin() or
//     touchSPI.begin() on it.
#if defined(CYD35)
    #define TOUCH_SCK  TFT_SCLK
    #define TOUCH_MOSI TFT_MOSI
    #define TOUCH_MISO TFT_MISO
#elif defined(AWOK)
    // No dedicated touch bus on AWOK — TFT_eSPI drives touch on the
    // display's own VSPI. Values below are placeholders so the compile
    // still works; the AWOK branches skip touchSPI.begin() entirely.
    #define TOUCH_SCK  TFT_SCLK
    #define TOUCH_MOSI TFT_MOSI
    #define TOUCH_MISO TFT_MISO
#else
    #define TOUCH_SCK  25
    #define TOUCH_MOSI 32
    #define TOUCH_MISO 39
#endif
// AWOK's user setup already #defines TOUCH_CS=21 (pulled in via the
// -include in platformio.ini) — awok_user_setup.h's TFT_eSPI touch
// path needs that value armed, so this can't unconditionally redefine
// it to 33 the way the other two boards share.
#ifndef TOUCH_CS
#define TOUCH_CS   33
#endif
#define TOUCH_IRQ  36
#define CAP_SDA    33
#define CAP_SCL    32
#define CAP_RST    25
// Backlight brightness (Settings menu): all three boards' backlight
// pins are driven at boot regardless of which one is actually wired
// (see the digitalWrite(HIGH) comment in setup() — same reasoning
// applies here), each on its own LEDC channel so ledcWrite can dim
// whichever one is real without needing to know which board this is.
// AWOK's BL sits on GPIO32; the other boards' pins (21, 27) are simply
// unused GPIOs on AWOK, so driving all three is harmless.
#define BL_PIN_ORIG 21
#define BL_PIN_CAP  27
#define BL_PIN_AWOK 32
#define BL_CH_ORIG  0
#define BL_CH_CAP   1
#define BL_CH_AWOK  2

// invertDisplay() sets an ABSOLUTE panel state -- it doesn't toggle
// relative to whatever TFT_INVERSION_ON/OFF a board's user-setup header
// set at compile time, it just overwrites it. That header define is
// therefore dead code for actually choosing a board's polarity -- a
// real bug found on real hardware (AWOK), several rounds of flipping
// the header setting with zero visible effect, before finding this is
// the actual control point. Settings::inverted() is a cosmetic
// per-user theme toggle, unrelated to a given panel's actual required
// polarity, so every invertDisplay() call XORs the two: this constant
// is the panel's own baseline, and the user's cosmetic toggle flips
// relative to it. File-scope (not local to setup()) so the Settings >
// INVERT row handler in loop() can use the same XOR instead of
// clobbering this baseline with an absolute call.
#if defined(CYD35)
// UNCONFIRMED on real hardware post-fix: the original port's "true"
// guess predates discovering the override bug above, so whatever
// testing produced that value was toggling a header define that does
// nothing -- not a real confirmation. Trying false first, same as
// AWOK's ILI9341 (this panel's ST7796 may differ; adjust from live
// observation).
constexpr bool PANEL_NEEDS_INVERSION = false;
#elif defined(AWOK)
// Confirmed on real hardware: true visibly changed something (proving
// this, not the dead awok_user_setup.h define, is the actual control
// point) but looked inverted/wrong. false is the ILI9341's normal
// (non-inverted) polarity, matching the original CYD board this panel
// shares a driver with.
constexpr bool PANEL_NEEDS_INVERSION = false;
#else
constexpr bool PANEL_NEEDS_INVERSION = false;
#endif

// TFT_eSprite::createSprite() no-ops (returns the existing buffer
// untouched) if the sprite is already created, so the only way to
// reuse an already-allocated buffer at a new width/height is to poke
// its own bookkeeping fields directly -- createSprite() itself does
// nothing more than this plus the calloc. Both are protected in
// TFT_eSPI/TFT_eSprite, reachable from a subclass. This only produces
// a *valid* buffer when the new w*h matches what was actually
// allocated -- true here because every rotation of a rectangular
// panel needs the same total pixel count (320x240 and 240x320 are
// both 76800 pixels), so one boot-time allocation covers every
// orientation forever and rotate never needs to free/realloc again.
class ResizableSprite : public TFT_eSprite {
public:
    explicit ResizableSprite(TFT_eSPI* tft) : TFT_eSprite(tft) {}
    void resizeInPlace(int16_t w, int16_t h) {
        if (!_created) return;
        _iwidth = _dwidth = _bitwidth = w;
        _iheight = _dheight = h;
        cursor_x = 0;
        cursor_y = 0;
        _sx = 0;
        _sy = 0;
        _sw = w;
        _sh = h;
        rotation = 0;
        setViewport(0, 0, _dwidth, _dheight);
        setPivot(_iwidth / 2, _iheight / 2);
    }
};

// ---- Globals ----
TFT_eSPI            tft = TFT_eSPI();
// All screens draw into this off-screen buffer, pushed to the physical
// display in one shot at the end of each loop(). Without it, every
// screen's erase-then-redraw sequence is briefly visible on real
// hardware — most noticeable as flickering text.
//
// cyd35 exception: at 320x480 this sprite needs ~150KB contiguous heap
// (8-bit), and that board's largest free block measured ~110KB on real
// hardware (no PSRAM, and WiFi/BLE fragment the heap before setup()
// even runs) -- createSprite() reliably fails there. Rather than a
// banded/partial-height rewrite of every draw call, cyd35 skips the
// double buffer entirely and draws straight to the panel via `canvas`
// below, accepting the erase/redraw flicker the buffer normally hides.
ResizableSprite     frame = ResizableSprite(&tft);
// A pointer, not a reference: confirmed on real hardware that
// re-createSprite()'ing `frame` after a rotation can fail even with
// generous total free heap (NimBLE/WiFi churn fragments it -- see the
// rotate handler in loop()). When that happens we permanently fail
// over to drawing straight into `tft` for the rest of the session,
// same tradeoff cyd35 already accepts by default -- which needs
// `canvas` to be reseatable at runtime, not bound once at startup.
#if defined(CYD35)
    TFT_eSPI*        canvas = &tft;
#else
    TFT_eSPI*        canvas = &frame;
#endif
XPT2046_Touchscreen touch(TOUCH_CS, TOUCH_IRQ);
// cyd35's touch bus IS the display's bus, shared via CS rather than a
// separate peripheral — see the CYD35 touch-init branch in setup(),
// which uses tft.getSPIinstance() instead of this object. A second,
// independent SPIClass(VSPI) pointed at the same pins TFT_eSPI already
// owns fights it at the GPIO-matrix level rather than sharing cleanly;
// touchSPI here is only ever used on the original (HSPI) board.
SPIClass         touchSPI(HSPI);
// Set once in setup() by probing for the capacitive controller —
// decides which branch pollTouch() takes for the rest of the run.
bool                usingCapTouch = false;
// Set false if a post-boot frame.createSprite() ever fails (rotate —
// see loop()). cyd35's CLEAR screen checks this to fall back from its
// banded render to direct-to-tft; the other board's `canvas` pointer
// gets reseated to &tft directly at the point of failure instead.
bool                frameBufferOk = true;
DetectionEngine     engine;
AppState            state     = AppState::BOOT;
uint32_t            bootStart = 0;
uint32_t            alertStart= 0;
uint32_t            lastTouch = 0;
bool                prevTouchValid = false; // last frame's tp.valid, for true press/release edge detection (see loop())
DetectionType       lastAlertType = DetectionType::UNKNOWN;
uint32_t            lastAlertHits = 1; // times this exact MAC+type has ever matched — see Squachy's "seen before" reaction
const uint16_t      TOUCH_DEBOUNCE_MS = 200;

// Hidden "unlock every Squachy outfit" gesture: CLR x9, then SCAN x1,
// then CLR x1 (11 taps) on the button bar, tracked globally since CLR/
// SCAN both appear on the CLEAR and LOG screens. Any tap that breaks
// the sequence resets progress, except when the breaking tap happens
// to be a fresh CLR -- that still counts as step 1 of a new attempt
// instead of forcing a full restart on the very next correct tap.
static const ButtonId EASTER_EGG_SEQUENCE[] = {
    ButtonId::CLR, ButtonId::CLR, ButtonId::CLR, ButtonId::CLR, ButtonId::CLR,
    ButtonId::CLR, ButtonId::CLR, ButtonId::CLR, ButtonId::CLR,
    ButtonId::SCAN,
    ButtonId::CLR,
};
static const uint8_t EASTER_EGG_LEN = sizeof(EASTER_EGG_SEQUENCE) / sizeof(EASTER_EGG_SEQUENCE[0]);
static uint8_t       s_easterEggProgress = 0;

static void trackOutfitEasterEgg(ButtonId b) {
    if (b == ButtonId::NONE) return;
    if (b == EASTER_EGG_SEQUENCE[s_easterEggProgress]) {
        s_easterEggProgress++;
        if (s_easterEggProgress >= EASTER_EGG_LEN) {
            s_easterEggProgress = 0;
            Squachy::unlockAllOutfits();
        }
    } else {
        s_easterEggProgress = (b == EASTER_EGG_SEQUENCE[0]) ? 1 : 0;
    }
}
// The ALERT screen carries real information (type, confidence, MAC,
// RSSI) — tapping it away is the expected dismiss, but the automatic
// fallback still needs to actually clear itself in a reasonable time
// if nobody's there to tap it.
const uint32_t      ALERT_AUTO_DISMISS_MS = 10000;
// TFT_eSPI rotation: all four orientations are supported (0/2 portrait,
// 1/3 landscape), cycled in order by the rotate button in the title
// bar -- except AWOK, which has no rotate button (see loop()) and
// stays fixed at its case's one physical orientation, confirmed on
// real hardware to be portrait/rotation 0.
#if defined(AWOK)
uint8_t             screenRotation = 0;
#else
uint8_t             screenRotation = 1;
#endif
// Timestamp of the last screen/rotation change — drives a brief CRT
// tear/glitch overlay on the new frame so transitions have some punch
// instead of just snapping straight to the next screen.
uint32_t            transitionStart = 0;
static const uint32_t TRANSITION_MS = 220;

// ---- Touch helpers ----
struct TouchPoint { bool valid; int x; int y; };

// CST816/CST820 native coordinate range — factory defaults measured
// against one specific JC2432W328C unit's 4 corners in landscape
// (rotation 1). Not const: overwritten at boot if a saved calibration
// exists (see loadOrDefaultCal()/TouchCal), and by the long-press
// calibration flow (see checkCalibrationTrigger()).
static uint16_t CAP_NX_MIN = 32,  CAP_NX_MAX = 166;
static uint16_t CAP_NY_MIN = 10,  CAP_NY_MAX = 308;
// Resistive XPT2046 raw ADC range — same idea, factory default was a
// flat 200-3800 for both axes; not const for the same reason.
static uint16_t RAW_X_MIN = 200, RAW_X_MAX = 3800;
static uint16_t RAW_Y_MIN = 200, RAW_Y_MAX = 3800;

#if defined(AWOK)
// ---- AWOK: TFT_eSPI-native touch calibration ----
// AWOK's XPT2046 sits on the display's own shared VSPI bus, so it goes
// through TFT_eSPI's own calibrateTouch()/setTouch()/getTouch() path
// instead of the raw-ADC + map() approach the other two boards use --
// AWOK's XPT2046's raw axes don't align the same way the CYD's do, so
// the CYD's rotation math doesn't carry over here.
//
// This board has no rotate button at all (confirmed on real hardware:
// the case only holds the panel in one orientation, portrait, so
// rotation was pure unused complexity) -- screenRotation is fixed at
// AWOK_ROTATION for the life of the program, never changed by a tap,
// so there's only ever one calibration to keep, not one per rotation
// the way an earlier version of this did.
static uint16_t awokTouchCal[5];
static bool     awokTouchCalibrated = false;

static const char* AWOK_TOUCH_NS  = "awoktouch";
static const char* AWOK_TOUCH_KEY = "cal5";

static bool awokLoadTouchCal() {
    Preferences p;
    p.begin(AWOK_TOUCH_NS, true);
    bool has = p.isKey(AWOK_TOUCH_KEY);
    if (has) {
        p.getBytes(AWOK_TOUCH_KEY, awokTouchCal, sizeof(awokTouchCal));
        awokTouchCalibrated = true;
    }
    p.end();
    return has;
}

static void awokSaveTouchCal() {
    Preferences p;
    p.begin(AWOK_TOUCH_NS, false);
    p.putBytes(AWOK_TOUCH_KEY, awokTouchCal, sizeof(awokTouchCal));
    p.end();
}

// The recovery path for a bad calibration — mirrors TouchCal::reset()
// for the other boards' NVS namespace, but this board's blob lives in
// a separate one ("awoktouch") that TouchCal::reset() never touches,
// so it needs its own clear here or the boot-time "hold to reset"
// gesture would be a silent no-op on this board.
static void awokResetTouchCal() {
    Preferences p;
    p.begin(AWOK_TOUCH_NS, false);
    p.clear();
    p.end();
    awokTouchCalibrated = false;
}

// Runs TFT_eSPI's own interactive 4-corner calibration and persists
// the result — called from the same two places the other boards call
// TouchCal::runInteractive() (the title-bar long-press and Settings >
// CALIBRATE TOUCH), plus automatically on first boot if nothing's
// saved yet (see awokEnsureCal() below).
static void awokRunCalibration() {
    tft.fillScreen(Theme::BG);
    tft.calibrateTouch(awokTouchCal, Theme::VAPOR_PINK, Theme::BG, 15);
    tft.setTouch(awokTouchCal);
    awokTouchCalibrated = true;
    awokSaveTouchCal();
}

// Called once at boot: re-arms the saved calibration if one exists, or
// runs the interactive calibration once if this is a fresh board.
static void awokEnsureCal() {
    if (awokLoadTouchCal()) {
        tft.setTouch(awokTouchCal);
        Serial.println("Loaded AWOK touch cal from NVS.");
        return;
    }
    Serial.println("AWOK: no saved cal, running interactive calibration.");
    awokRunCalibration();
}
#endif  // AWOK

#if defined(CYD35)
// ---- cyd35: TFT_eSPI-native touch calibration, per rotation ----
// A prior pass here used a hand-rolled raw-SPI reader (bypassing
// TFT_eSPI's own touch code entirely) after the native path looked
// completely dead in the real app. Root-cause turned out to be
// unrelated to the touch code at all: sd_log.cpp's SD.begin() was
// silently re-attaching the shared VSPI bus to the wrong GPIO pins
// (see its comment) -- once that was fixed, the native path was never
// re-tested. An independently-verified working config for this exact
// panel (a friend's QDtech E32R35T port) confirms TFT_eSPI's native
// calibrateTouch()/setTouch()/getTouch() is in fact the right approach
// here, same as AWOK -- so back to that, now that the real bug is
// fixed underneath it.
//
// Unlike AWOK, this board keeps its rotate button, and TFT_eSPI's
// calibration blob is baked relative to whichever rotation was active
// when calibrateTouch() ran (the raw axis-swap/invert decision is
// fixed at calibration time, only the current width/height scale
// afterward), so one blob does not carry over correctly to a different
// rotation. Four independent blobs, one per rotation, calibrated on
// first use of each.
static uint16_t cyd35TouchCal[4][5];
static bool     cyd35TouchCalibrated[4] = { false, false, false, false };

static const char* CYD35_TOUCH_NS = "cyd35touch";

static bool cyd35LoadTouchCal(uint8_t rot) {
    Preferences p;
    p.begin(CYD35_TOUCH_NS, true);
    char key[8];
    snprintf(key, sizeof(key), "cal5_%u", rot);
    bool has = p.isKey(key);
    if (has) {
        p.getBytes(key, cyd35TouchCal[rot], sizeof(cyd35TouchCal[rot]));
        cyd35TouchCalibrated[rot] = true;
    }
    p.end();
    return has;
}

static void cyd35SaveTouchCal(uint8_t rot) {
    Preferences p;
    p.begin(CYD35_TOUCH_NS, false);
    char key[8];
    snprintf(key, sizeof(key), "cal5_%u", rot);
    p.putBytes(key, cyd35TouchCal[rot], sizeof(cyd35TouchCal[rot]));
    p.end();
}

// The recovery path for a bad calibration -- same reasoning as
// awokResetTouchCal(): this board's blobs live in their own NVS
// namespace that TouchCal::reset() never touches.
static void cyd35ResetTouchCal() {
    Preferences p;
    p.begin(CYD35_TOUCH_NS, false);
    p.clear();
    p.end();
    for (uint8_t i = 0; i < 4; i++) cyd35TouchCalibrated[i] = false;
}

// Runs TFT_eSPI's own interactive 4-corner calibration for whichever
// rotation is currently active and persists it under that rotation's
// own key -- called from the title-bar long-press, Settings >
// CALIBRATE TOUCH, and automatically the first time a given rotation
// is used (see cyd35EnsureCal()).
static void cyd35RunCalibration() {
    uint8_t rot = screenRotation;
    tft.fillScreen(Theme::BG);
    tft.calibrateTouch(cyd35TouchCal[rot], Theme::VAPOR_PINK, Theme::BG, 15);
    tft.setTouch(cyd35TouchCal[rot]);
    cyd35TouchCalibrated[rot] = true;
    cyd35SaveTouchCal(rot);
}

// Called at boot and every time the rotate button changes
// screenRotation: re-arms that rotation's saved calibration if one
// exists, or runs the interactive calibration once if this is the
// first time this particular rotation has ever been used.
static void cyd35EnsureCal(uint8_t rot) {
    if (cyd35TouchCalibrated[rot]) {
        tft.setTouch(cyd35TouchCal[rot]);
        return;
    }
    if (cyd35LoadTouchCal(rot)) {
        tft.setTouch(cyd35TouchCal[rot]);
        Serial.printf("Loaded cyd35 touch cal for rotation %u from NVS.\n", rot);
        return;
    }
    Serial.printf("cyd35: no saved cal for rotation %u, running interactive calibration.\n", rot);
    cyd35RunCalibration();
}
#endif  // CYD35

static TouchPoint pollTouch() {
    TouchPoint tp = { false, 0, 0 };
    int w = tft.width(), h = tft.height();
    bool landscape = (screenRotation % 2) == 1;
    bool flipped   = screenRotation >= 2;

    if (usingCapTouch) {
        uint16_t nx, ny;
        if (!CapTouch::read(nx, ny)) return tp;
        // Same "native portrait frame, rotate per TFT_eSPI rotation"
        // structure as the XPT2046 path below — nx/ny stand in for
        // the XPT2046's raw p.x/p.y, just with the CST816's own
        // measured range instead of raw ADC counts. Only rotation 1
        // (landscape) is confirmed against real taps on this unit;
        // 0/2/3 are derived by the same symmetry that held for
        // XPT2046 and are worth spot-checking in portrait.
        if (!landscape) {
            tp.x = flipped ? map(nx, CAP_NX_MIN, CAP_NX_MAX, w, 0) : map(nx, CAP_NX_MIN, CAP_NX_MAX, 0, w);
            tp.y = flipped ? map(ny, CAP_NY_MIN, CAP_NY_MAX, h, 0) : map(ny, CAP_NY_MIN, CAP_NY_MAX, 0, h);
        } else {
            tp.x = flipped ? map(ny, CAP_NY_MIN, CAP_NY_MAX, w, 0) : map(ny, CAP_NY_MIN, CAP_NY_MAX, 0, w);
            tp.y = flipped ? map(nx, CAP_NX_MIN, CAP_NX_MAX, 0, h) : map(nx, CAP_NX_MIN, CAP_NX_MAX, h, 0);
        }
        tp.valid = (tp.x >= 0 && tp.x < w && tp.y >= 0 && tp.y < h);
        return tp;
    }

#if defined(CYD35)
    // TFT_eSPI-native touch path, same shape as AWOK's below -- see
    // cyd35EnsureCal()'s comment for why this needs the per-rotation
    // cache instead of one shared blob. Nothing to read until the
    // current rotation's calibration has run at least once.
    if (!cyd35TouchCalibrated[screenRotation]) return tp;
    {
        uint16_t sx, sy;
        if (!tft.getTouch(&sx, &sy)) return tp;
        tp.x = (int)sx;
        tp.y = (int)sy;
        tp.valid = (tp.x >= 0 && tp.x < w && tp.y >= 0 && tp.y < h);
    }
    return tp;
#elif defined(AWOK)
    // TFT_eSPI-native touch path. getTouch() returns already-
    // calibrated, already-rotated screen coordinates directly -- no
    // map()/raw-ADC axis math needed, unlike the other two boards.
    // Nothing to read until calibration has run once (see
    // awokEnsureCal(), called from setup() -- there's no rotate
    // button on this board, so this only ever needs to happen once).
    if (!awokTouchCalibrated) return tp;
    {
        uint16_t sx, sy;
        if (!tft.getTouch(&sx, &sy)) return tp;
        tp.x = (int)sx;
        tp.y = (int)sy;
        tp.valid = (tp.x >= 0 && tp.x < w && tp.y >= 0 && tp.y < h);
    }
    return tp;
#endif

    // Resistive XPT2046 path (original jczn_2432s028r board) —
    // unchanged from the earlier single-board firmware.
    if (!touch.tirqTouched()) return tp;
    if (touch.touched()) {
        TS_Point p = touch.getPoint();
        if (!landscape) {
            tp.x = flipped ? map(p.x, RAW_X_MIN, RAW_X_MAX, w, 0) : map(p.x, RAW_X_MIN, RAW_X_MAX, 0, w);
            tp.y = flipped ? map(p.y, RAW_Y_MIN, RAW_Y_MAX, h, 0) : map(p.y, RAW_Y_MIN, RAW_Y_MAX, 0, h);
        } else {
            tp.x = flipped ? map(p.y, RAW_Y_MIN, RAW_Y_MAX, w, 0) : map(p.y, RAW_Y_MIN, RAW_Y_MAX, 0, w);
            tp.y = flipped ? map(p.x, RAW_X_MIN, RAW_X_MAX, 0, h) : map(p.x, RAW_X_MIN, RAW_X_MAX, h, 0);
        }
        tp.valid = (tp.x >= 0 && tp.x < w && tp.y >= 0 && tp.y < h);
    }
    return tp;
}

// ---- Calibration ----
static bool rawReadCap(int16_t& a, int16_t& b) {
    uint16_t nx, ny;
    if (!CapTouch::read(nx, ny)) return false;
    a = (int16_t)nx;
    b = (int16_t)ny;
    return true;
}

static bool rawReadResistive(int16_t& a, int16_t& b) {
#if defined(AWOK) || defined(CYD35)
    // Neither board's `touch` (XPT2046_Touchscreen) object is ever
    // begin()'d — both drive touch natively through TFT_eSPI instead
    // (see their setup() branches) — so this goes through TFT_eSPI's
    // own raw-touch accessors instead. Only used by the boot-time
    // "hold to reset calibration" window; each board's own calibration
    // flow (awokRunCalibration()/cyd35RunCalibration()) doesn't route
    // through this at all.
    // getTouchRaw() alone always returns true in TFT_eSPI 2.5.43, so
    // the actual "is a finger down" gate is the pressure threshold.
    if (tft.getTouchRawZ() < 350) return false;
    uint16_t rx, ry;
    tft.getTouchRaw(&rx, &ry);
    a = (int16_t)rx;
    b = (int16_t)ry;
    return true;
#else
    if (!touch.tirqTouched() || !touch.touched()) return false;
    TS_Point p = touch.getPoint();
    a = (int16_t)p.x;
    b = (int16_t)p.y;
    return true;
#endif
}

// See touch_cal.h: aTop/aBottom/bLeft/bRight are generic (whatever the
// RawReader's two raw axes are, sampled at the top/bottom row and
// left/right column respectively) — map them onto whichever named
// constants pollTouch() actually uses, matching the same top/bottom/
// left/right relationship already derived for each touch type above.
static void applyBrightness() {
    uint8_t duty = Settings::brightness();
    ledcWrite(BL_CH_ORIG, duty);
    ledcWrite(BL_CH_CAP,  duty);
    ledcWrite(BL_CH_AWOK, duty);
}

static void applyCal(const TouchCal::Cal& cal) {
    if (usingCapTouch) {
        CAP_NX_MAX = (uint16_t)cal.aTop;
        CAP_NX_MIN = (uint16_t)cal.aBottom;
        CAP_NY_MIN = (uint16_t)cal.bLeft;
        CAP_NY_MAX = (uint16_t)cal.bRight;
    } else {
        RAW_X_MAX = (uint16_t)cal.aTop;
        RAW_X_MIN = (uint16_t)cal.aBottom;
        RAW_Y_MIN = (uint16_t)cal.bLeft;
        RAW_Y_MAX = (uint16_t)cal.bRight;
    }
}

// ---- State transitions ----
#if defined(CYD35)
// BOOT/CLEAR/ALERT all share the one half-height `frame` sprite for
// their two-pass rendering (see each state's case in loop()), but each
// screen's own xxxInit() clears *canvas -- the real display for this
// board, not `frame` itself -- so it never actually touches the
// buffer the two-pass rendering reads from. Harmless back when only
// CLEAR ever used `frame` (its own content was always self-consistent
// frame to frame), but once BOOT/ALERT started reusing that same
// buffer, switching screens could leave one screen's leftover pixels
// sitting in whatever region the next screen's own drawing never
// touches (confirmed on real hardware: CLEAR's background animation
// stops short of the button bar row's gaps, so BOOT's last frame was
// showing through there as colored noise after switching to CLEAR).
// One full clear of the real physical buffer on entry to any of these
// three screens is enough -- nothing after that leaves stray pixels
// behind on its own.
static void clearSharedFrameBuffer() {
    if (frameBufferOk) frame.fillRect(0, 0, frame.width(), frame.height(), Theme::BG);
}
#endif

static void enterBoot() {
    state = AppState::BOOT;
    bootStart = millis();
    transitionStart = bootStart;
    uiBootInit(*canvas);
#if defined(CYD35)
    clearSharedFrameBuffer();
#endif
}

static void enterClear() {
    state = AppState::CLEAR;
    transitionStart = millis();
    uiClearInit(*canvas);
#if defined(CYD35)
    clearSharedFrameBuffer();
#endif
}

static void enterAlert(const Detection& d) {
    state = AppState::ALERT;
    alertStart = millis();
    transitionStart = alertStart;
    lastAlertType = d.type;
    lastAlertHits = d.hits;
    uiAlertInit(*canvas, d);
#if defined(CYD35)
    clearSharedFrameBuffer();
#endif
}

static void enterLog() {
    state = AppState::LOG;
    transitionStart = millis();
    uiLogInit(*canvas);
}

static void enterSettings() {
    state = AppState::SETTINGS;
    transitionStart = millis();
    uiSettingsInit(*canvas);
}

static void enterDiary() {
    state = AppState::DIARY;
    transitionStart = millis();
    uiDiaryInit(*canvas);
}

static void enterOutfit() {
    state = AppState::OUTFIT;
    transitionStart = millis();
    uiOutfitInit(*canvas);
}

// ---- Arduino setup / loop ----
void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println();
    Serial.println("SquachWatch-CYD v1.0  --  TALKING SASQUACH");
#if defined(CYD35)
    // One-time diagnostic: is PSRAM actually present on this unit? The
    // "no PSRAM" conclusion driving the no-full-framebuffer tradeoff
    // (see `frame`'s declaration up top) was from an earlier pass --
    // worth confirming directly before deciding whether a PSRAM-backed
    // full double buffer is even on the table.
    Serial.printf("PSRAM found: %s (%u bytes)\n", psramFound() ? "yes" : "no", (unsigned)ESP.getPsramSize());
#endif

    // Backlight: the original board uses GPIO21 for this, the
    // JC2432W328C uses GPIO27 (confirmed by sweeping candidate pins on
    // a physical unit). Driving both HIGH is harmless either way —
    // each is simply an unused GPIO on the "other" board — and means
    // the screen lights up before we've even figured out which board
    // this is.
    //
    // GPIO21 exception on AWOK: it's TOUCH_CS there, not a spare. A
    // brief digitalWrite HIGH pre-init just holds CS deasserted and
    // would be harmless on its own, but the LEDC attach further down
    // would fight TFT_eSPI's control of the pin, so pin 21 is skipped
    // off entirely on AWOK for consistency with that.
#if !defined(AWOK)
    pinMode(21, OUTPUT); digitalWrite(21, HIGH);
#endif
    pinMode(27, OUTPUT); digitalWrite(27, HIGH);
    pinMode(32, OUTPUT); digitalWrite(32, HIGH);  // AWOK's real BL pin; unused GPIO on the other two boards

    tft.init();
    // Landscape (320 wide × 240 tall) — the CYD's natural orientation
    // with the ST7789 driver. The 0xC2 unlock that was here was for
    // ST7796U and will lock up an ST7789 panel; do not re-add it unless
    // we confirm the panel is actually ST7796.
    tft.setRotation(screenRotation);

    // Load persisted settings before the first real pixel is drawn, so
    // boot itself already reflects the saved theme/invert choice
    // instead of flashing the defaults for a moment first.
    Settings::load();
#if defined(AWOK)
    // No rotate button on this board (see the rotate handler in
    // loop(), not even compiled in on AWOK) -- hide the icon too so
    // there's nothing dead-looking left in the title bar.
    Theme::setRotateIconVisible(false);
#endif
    // See PANEL_NEEDS_INVERSION's definition up top for why this XORs
    // against a per-board baseline instead of calling invertDisplay()
    // with Settings::inverted() directly.
    tft.invertDisplay(PANEL_NEEDS_INVERSION != Settings::inverted());
    tft.fillScreen(Theme::BG);

    // Hand the digitalWrite(HIGH) backlight pins above off to LEDC PWM
    // so the settings-menu brightness slider can dim them — same
    // "drive both boards' pin, only one is really wired" reasoning as
    // the digitalWrite call, just with a duty cycle instead of a flat
    // HIGH. BL_CH_ORIG/GPIO21 is skipped on AWOK because it's TOUCH_CS
    // there (same reasoning as the digitalWrite skip above) — attaching
    // LEDC to it would fight TFT_eSPI's control of the pin.
#if !defined(AWOK)
    ledcSetup(BL_CH_ORIG, 5000, 8);
    ledcAttachPin(BL_PIN_ORIG, BL_CH_ORIG);
#endif
    ledcSetup(BL_CH_CAP, 5000, 8);
    ledcAttachPin(BL_PIN_CAP, BL_CH_CAP);
    ledcSetup(BL_CH_AWOK, 5000, 8);
    ledcAttachPin(BL_PIN_AWOK, BL_CH_AWOK);
    applyBrightness();

#if defined(CYD35)
    // No FULL-screen double buffer on this board — confirmed on real
    // hardware that the 320x480 panel's ~150KB sprite need exceeds the
    // largest contiguous free heap block (~110KB, no PSRAM). `canvas`
    // aliases `tft` directly for this build (see its declaration up
    // top) and is what most screens draw into, unbuffered.
    //
    // The CLEAR screen is the exception: it's the most visibly animated
    // (Squachy + a background effect), so it gets a real half-height
    // sprite (~76KB at 8-bit, comfortably fits) and renders in two
    // bands via setViewport() -- see the AppState::CLEAR case in
    // loop(). `advance` on uiClearTick()/Squachy::tick()/
    // Theme::drawMatrixRain() gates state mutation to the first band
    // only, so calling them twice per logical frame doesn't double
    // animation speed.
    canvas->setTextSize(1);
    frame.setColorDepth(8);
    if (!frame.createSprite(tft.width(), tft.height() / 2)) {
        Serial.println("ERROR: cyd35 half-height frame buffer allocation failed");
    }
    frame.setTextSize(1);
#else
    // 8-bit (palette) mode: 320x240 needs ~75KB instead of ~150KB at
    // 16-bit — the full 16-bit buffer didn't fit in the available
    // contiguous heap on this board.
    frame.setColorDepth(8);
    if (!frame.createSprite(tft.width(), tft.height())) {
        Serial.println("ERROR: frame buffer allocation failed (low memory)");
    }
    frame.setTextSize(1);
#endif

#if defined(CYD35)
    // The standalone XPT2046_Touchscreen library (own SPIClass, own
    // IRQ pin) produced constant garbage reads and a free-running IRQ
    // here -- not a wrong-pin problem, a second SPI master fighting
    // TFT_eSPI for the same physical bus. Same wiring shape as AWOK
    // (touch shares the display's own SPI bus, no dedicated
    // peripheral), so this uses the same fix: drive touch entirely
    // through TFT_eSPI's own calibrateTouch()/setTouch()/getTouch()
    // path instead, which shares the bus properly. No I2C cap-touch
    // chip on this board either, so no probe -- no touch.begin(), no
    // touchSPI. All subsequent touch reads go through pollTouch()'s
    // CYD35 branch (tft.getTouch()).
    usingCapTouch = false;
    Serial.println("cyd35 build -- XPT2046 on shared VSPI bus via TFT_eSPI.");
#elif defined(AWOK)
    // AWOK's XPT2046 sits on the display's own shared VSPI bus (TOUCH_CS=21,
    // already armed by TFT_eSPI itself once awok_user_setup.h's #define
    // is in scope) and is driven entirely through TFT_eSPI's own touch
    // path -- no I2C cap-touch probe (this board has no cap-touch chip
    // at all), no touch.begin(), no touchSPI. All subsequent touch
    // reads go through pollTouch()'s AWOK branch (tft.getTouch()).
    usingCapTouch = false;
    Serial.println("AWOK build -- XPT2046 on shared VSPI bus via TFT_eSPI.");
#else
    // Touch: probe for the capacitive controller first (I2C 0x15 on
    // SDA=33/SCL=32, reset on GPIO25 — the JC2432W328C). If it doesn't
    // answer, release the I2C bus and fall back to the resistive
    // XPT2046 on its own dedicated SPI bus (the original board) — a
    // genuinely separate HSPI peripheral on pins that don't overlap
    // the display's VSPI pins at all, so there's no GPIO-matrix
    // conflict either way.
    CapTouch::begin(CAP_SDA, CAP_SCL, CAP_RST);
    usingCapTouch = CapTouch::probe();
    if (usingCapTouch) {
        Serial.println("Capacitive touch (CST816/820) detected -- JC2432W328C-style board.");
    } else {
        Wire.end();
        Serial.println("No capacitive touch found -- assuming resistive XPT2046.");
        touchSPI.begin(TOUCH_SCK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
        touch.begin(touchSPI);
        touch.setRotation(0);
    }
#endif

    // Recovery escape hatch: hold touch ANYWHERE for ~1s right here to
    // wipe a saved calibration back to defaults. A bad calibration can
    // make touch too inaccurate to reliably re-tap a "recalibrate"
    // button, so this needs no precision at all — just a hold anywhere
    // during the window right after boot.
    tft.fillScreen(Theme::BG);
    tft.setTextColor(Theme::AMBER, Theme::BG);
    tft.setTextSize(1);
    tft.setCursor(4, 4);
    tft.print("Hold anywhere now to reset touch calibration...");
    {
        uint32_t holdStart = 0;
        uint32_t windowStart = millis();
        while (millis() - windowStart < 1200) {
            int16_t a, b;
            bool down = usingCapTouch ? rawReadCap(a, b) : rawReadResistive(a, b);
            if (down) {
                if (holdStart == 0) holdStart = millis();
                else if (millis() - holdStart > 800) {
                    TouchCal::reset();
#if defined(AWOK)
                    // TouchCal::reset() only clears the "touchcal"
                    // namespace the other boards use -- AWOK's blob
                    // lives in a separate one and would otherwise
                    // silently reload on the next boot, making this
                    // gesture a no-op here.
                    awokResetTouchCal();
#elif defined(CYD35)
                    // Same reasoning as AWOK above -- cyd35's four
                    // per-rotation blobs live in their own namespace.
                    cyd35ResetTouchCal();
#endif
                    tft.fillScreen(Theme::BG);
                    tft.setTextColor(Theme::AMBER, Theme::BG);
                    tft.setTextSize(2);
                    const char* msg = "CALIBRATION RESET";
                    tft.setCursor((tft.width() - tft.textWidth(msg)) / 2, tft.height() / 2 - 8);
                    tft.print(msg);
                    delay(1200);
                    break;
                }
            } else {
                holdStart = 0;
            }
            delay(10);
        }
    }
    tft.fillScreen(Theme::BG);

#if defined(AWOK)
    // The compiled-in defaults for the CYD 2.8" board's XPT2046
    // (RAW_X_MIN=200..RAW_X_MAX=3800) don't match the shared-bus
    // XPT2046 on this board, so an uncalibrated first boot leaves
    // ghost taps landing all over the screen. Force the 4-corner
    // interactive cal right here on first boot if nothing's saved yet
    // (awokEnsureCal() does that automatically when awokLoadTouchCal()
    // finds nothing saved).
    awokEnsureCal();
#elif defined(CYD35)
    // Same reasoning as AWOK above, but keyed to the current rotation
    // -- see cyd35EnsureCal()'s comment for why one blob per rotation
    // is needed here where AWOK only ever needs one.
    cyd35EnsureCal(screenRotation);
#else
    // Apply a saved touch calibration if one exists (long-press the
    // title bar on the CLEAR/LOG screen to (re)calibrate — see
    // checkCalibrationTrigger()); otherwise keep the compiled-in
    // defaults above.
    TouchCal::Cal savedCal;
    if (TouchCal::load(savedCal)) {
        applyCal(savedCal);
        Serial.println("Loaded saved touch calibration.");
    }
#endif

    // Seed the PRNG so the matrix rain starts in a fresh-looking state
    // on every boot. Analog read on a floating pin is plenty.
    randomSeed(analogRead(34));

    engine.init();
    Squachy::trigger(Squachy::Event::BOOTED, DetectionType::UNKNOWN, engine.lifetimeTotal());
    enterBoot();
}

void loop() {
    uint32_t now = millis();
    TouchPoint tp = pollTouch();
    // True only on the exact frame a touch begins/ends -- unlike
    // TOUCH_DEBOUNCE_MS below (a cooldown timer that still re-fires on a
    // long-held finger once the cooldown elapses), these compare this
    // frame's tp.valid against last frame's, so they each fire exactly
    // once per physical press no matter how long it's held.
    bool touchJustDown = tp.valid && !prevTouchValid;
    bool touchJustUp    = !tp.valid && prevTouchValid;
    engine.loop();

    // Rotate button lives in the title bar's top-right corner, shown on
    // the CLEAR, LOG, SETTINGS and OUTFIT screens (drawTitleBar always
    // draws it on the two boards that have one — gating the hit-test
    // to these keeps it inert wherever there's no title bar drawn at
    // all, i.e. BOOT/ALERT). Not built at all on AWOK: confirmed on
    // real hardware that board's case only holds the panel in one
    // orientation (portrait), so rotation was pure unused complexity
    // there — see Theme::setRotateIconVisible(false) in setup(), which
    // also hides the icon itself, not just this handler.
#if !defined(AWOK)
    if (tp.valid && (state == AppState::CLEAR || state == AppState::LOG ||
                      state == AppState::SETTINGS || state == AppState::OUTFIT) &&
        Theme::rotateButtonHit(tp.x, tp.y, tft.width()) &&
        (now - lastTouch) > TOUCH_DEBOUNCE_MS) {
        lastTouch = now;
        Squachy::trigger(Squachy::Event::ROTATED);
        screenRotation = (screenRotation + 1) % 4;
        tft.setRotation(screenRotation);
#if defined(CYD35)
        // Arms this rotation's own calibration blob (running the
        // interactive calibration on the spot if this rotation has
        // never been used before) -- see cyd35EnsureCal()'s comment
        // for why one blob per rotation is needed on this board.
        cyd35EnsureCal(screenRotation);
#endif
        // Landscape and portrait need differently-*shaped* buffers, but
        // not differently-*sized* ones -- a rectangular panel has the
        // same total pixel count either way (320x240 and 240x320 are
        // both 76800 px), so the buffer allocated once at boot already
        // fits every orientation. resizeInPlace() (see ResizableSprite
        // above) just re-points the sprite's own width/height/stride
        // bookkeeping at the new shape; it never frees or reallocates.
        //
        // This replaces an earlier delete+recreate-every-rotate
        // approach that looked safe (retried 3x with delays between
        // attempts) but confirmed-failed on real hardware despite 123KB
        // of TOTAL free heap -- the largest contiguous block was only
        // 73.7KB against a 76.8KB need, pure fragmentation from
        // WiFi/BLE churn, not a shortage retries could out-wait (delay()
        // doesn't make the ESP32 heap allocator compact anything). Since
        // resizeInPlace() never frees the buffer, that failure mode is
        // now structurally impossible after the first successful boot
        // allocation -- there's no more free/realloc cycle left to lose
        // the fragmentation gamble against.
        if (frame.created()) {
#if defined(CYD35)
            frame.resizeInPlace(tft.width(), tft.height() / 2);
#else
            frame.resizeInPlace(tft.width(), tft.height());
#endif
        } else if (frameBufferOk) {
            // Never got its one-time boot-time allocation in the first
            // place (see setup()) -- same permanent fallback a failed
            // rotate used to trigger, since there's no buffer to resize.
            Serial.println("[rotate] frame buffer was never allocated -- falling back to unbuffered rendering");
            frameBufferOk = false;
#if !defined(CYD35)
            canvas = &tft;
#endif
        }
        transitionStart = now;
    }
#endif  // !AWOK

    // Settings button lives in the title bar's top-left corner, shown
    // on the same screens as the rotate button. Tapping it while
    // already on the SETTINGS screen backs out to CLEAR instead of
    // re-entering itself — same toggle-off feel as the LOG button. From
    // OUTFIT (only reachable from SETTINGS' OUTFIT row) it backs out to
    // SETTINGS instead of CLEAR, matching where it was entered from —
    // this is the only way back out of that screen, since its own taps
    // are all claimed by the arrows.
    if (tp.valid && (state == AppState::CLEAR || state == AppState::LOG ||
                      state == AppState::SETTINGS || state == AppState::OUTFIT) &&
        Theme::settingsButtonHit(tp.x, tp.y) &&
        (now - lastTouch) > TOUCH_DEBOUNCE_MS) {
        lastTouch = now;
        if (state == AppState::OUTFIT) enterSettings();
        else if (state == AppState::SETTINGS) enterClear();
        else enterSettings();
    }

    // Long-press the middle of the title bar (between the settings and
    // rotate icons) on CLEAR/LOG to (re)calibrate touch — held, not
    // tapped, so normal use (including tapping either icon) can't
    // trigger it by accident.
    static uint32_t calHoldStart = 0;
    bool overTitleBar = tp.valid && tp.x >= 50 && tp.x < tft.width() - 50 && tp.y < 20;
    if ((state == AppState::CLEAR || state == AppState::LOG) && overTitleBar) {
        if (calHoldStart == 0) calHoldStart = now;
        else if (now - calHoldStart > 1500) {
            calHoldStart = 0;
            lastTouch = now;
#if defined(AWOK)
            awokRunCalibration();
#elif defined(CYD35)
            cyd35RunCalibration();
#else
            TouchCal::RawReader reader = usingCapTouch ? rawReadCap : rawReadResistive;
            TouchCal::Cal newCal = TouchCal::runInteractive(*canvas, reader, Theme::BG, Theme::WHITE, Theme::CYAN);
            applyCal(newCal);
#endif
            enterClear();
        }
    } else {
        calHoldStart = 0;
    }

    switch (state) {
        case AppState::BOOT: {
#if defined(CYD35)
            if (frameBufferOk) {
                // Same two-pass half-height `frame` trick CLEAR uses --
                // reuses that same already-allocated sprite (see its
                // setup() comment) rather than needing a second
                // allocation, since BOOT/CLEAR/ALERT are never on
                // screen at the same time. uiBootTick() has no internal
                // per-call state to double-advance, so no advance flag
                // needed here unlike uiClearTick().
                int halfH = tft.height() / 2;
                frame.setViewport(0, 0, tft.width(), tft.height(), true);
                uiBootTick(frame, now);
                frame.pushSprite(0, 0);
                frame.setViewport(0, -halfH, tft.width(), tft.height(), true);
                uiBootTick(frame, now);
                frame.pushSprite(0, halfH);
                frame.resetViewport();
            } else {
                uiBootTick(tft, now);
            }
#else
            uiBootTick(*canvas, now);
#endif
            if (uiBootDone(bootStart)) {
                enterClear();
            }
            break;
        }
        case AppState::CLEAR: {
#if defined(CYD35)
            if (frameBufferOk) {
                // Two passes through the half-height `frame` sprite
                // instead of one direct-to-tft pass -- see the setup()
                // comment by its creation. advance=true only on the
                // first pass so Squachy/matrix-rain state advances once
                // per logical frame even though this draws twice.
                int halfH = tft.height() / 2;
                frame.setViewport(0, 0, tft.width(), tft.height(), true);
                uiClearTick(frame, now, engine, true);
                frame.pushSprite(0, 0);
                frame.setViewport(0, -halfH, tft.width(), tft.height(), true);
                uiClearTick(frame, now, engine, false);
                frame.pushSprite(0, halfH);
                frame.resetViewport();
            } else {
                // Fallback if a post-boot rotate ever failed to
                // reallocate `frame` (see loop()) -- same direct-to-tft
                // path this board already uses for every other screen.
                uiClearTick(tft, now, engine);
            }
#else
            uiClearTick(*canvas, now, engine);
#endif
            // Check for new detection — gated by the settings-menu
            // confidence filter (LOW_CONF/default = no filtering, every
            // match still interrupts with the ALERT screen).
            const Detection* latest = engine.latest();
            if (latest && (now - latest->firstSeen) < 200 &&
                confidenceFor(latest->type) >= Settings::minConfidence()) {
                enterAlert(*latest);
            }
            // First-boot walkthrough: tapping its bubble advances (or
            // ends) it. Checked before everything else below so it eats
            // the tap on a hit — but it only ever hits its own bubble,
            // never Squachy himself or the button bar, so pet-tap,
            // background-cycling and the buttons all keep working
            // normally throughout the walkthrough, same as any other
            // time on this screen. Both this and the pet-tap check are
            // skipped outright in "boring mode": uiClearTick() never
            // draws him there, so hitTest() would otherwise still be
            // checking against wherever he last stood before the mode
            // was turned on — a tap on empty background shouldn't pet
            // a mascot that isn't there.
            bool boring = Settings::boringMode();
            ButtonId barBtn = tp.valid ? Theme::hitTestButtonBar(tp.x, tp.y, tft.width(), tft.height()) : ButtonId::NONE;

            // Left/right 10% slivers of the screen (excluding the button
            // bar row itself, so its own leftmost/rightmost buttons still
            // win there) cycle the background one step. Edge-triggered on
            // touchJustDown rather than the TOUCH_DEBOUNCE_MS cooldown
            // below, so a held finger fires exactly once no matter how
            // long it stays down. This frees up tapping Squachy himself
            // for the gesture classifier instead of also nudging the
            // background, so he can be poked without changing the scene.
            const int edgeZoneW = tft.width() / 10;
            bool inEdgeZone = tp.valid && barBtn == ButtonId::NONE &&
                               (tp.x < edgeZoneW || tp.x >= tft.width() - edgeZoneW);

            // Squachy gesture state, tracked across frames from the
            // moment a touch lands on him until it releases. A quick tap
            // (released before SQ_HOLD_MS with negligible movement) reads
            // as PETTED -- the only one of the three that counts toward
            // the persisted pet-count/milestones. A stationary press held
            // past SQ_HOLD_MS reads as HELD. Movement past SQ_MOVE_PX
            // reads as PETTING (a drag/stroke) and keeps firing, throttled
            // internally by squachy.cpp, for as long as the stroke
            // continues.
            static bool     sqActive = false;
            static bool     sqHeld = false;
            static bool     sqPetting = false;
            static uint32_t sqStartMs = 0;
            static int      sqStartX = 0, sqStartY = 0;
            constexpr uint32_t SQ_HOLD_MS = 600;
            constexpr int32_t  SQ_MOVE_PX = 12;
            constexpr int32_t  SQ_MOVE_PX_SQ = SQ_MOVE_PX * SQ_MOVE_PX;

            // Decide up front whether a brand-new touch lands on Squachy
            // -- this only updates gesture-tracking state, it doesn't by
            // itself claim the touch, so a miss still falls through to
            // the button-bar branch below instead of being swallowed.
            // (A touch that DOES land on him, or continues an already-
            // active gesture, still takes priority over the button bar
            // in the branch below -- Squachy is drawn well clear of the
            // button row, so the two never really compete in practice.)
            if (touchJustDown) {
                sqActive = !boring && Squachy::hitTest(tp.x, tp.y);
                sqHeld = false;
                sqPetting = false;
                sqStartMs = now;
                sqStartX = tp.x;
                sqStartY = tp.y;
            }

            if (!boring && Squachy::onboardingActive() && tp.valid && (now - lastTouch) > TOUCH_DEBOUNCE_MS &&
                Squachy::onboardingTapAdvance(tp.x, tp.y)) {
                lastTouch = now;
            } else if (touchJustDown && inEdgeZone) {
                if (tp.x < edgeZoneW) Settings::cyclePrevBackground();
                else                  Settings::cycleBackground();
            } else if (!boring && tp.valid && sqActive) {
                int32_t dx = tp.x - sqStartX;
                int32_t dy = tp.y - sqStartY;
                if ((dx * dx + dy * dy) > SQ_MOVE_PX_SQ) sqPetting = true;
                if (sqPetting) {
                    Squachy::trigger(Squachy::Event::PETTING);
                } else if (!sqHeld && (now - sqStartMs) >= SQ_HOLD_MS) {
                    sqHeld = true;
                    Squachy::trigger(Squachy::Event::HELD);
                }
            } else if (tp.valid && (now - lastTouch) > TOUCH_DEBOUNCE_MS) {
                lastTouch = now;
                trackOutfitEasterEgg(barBtn);
                if (barBtn == ButtonId::LOG)  { Squachy::trigger(Squachy::Event::LOG_OPENED); enterLog(); }
                else if (barBtn == ButtonId::CLR) { engine.clearLog(); Squachy::trigger(Squachy::Event::LOG_CLEARED); enterClear(); }
                // SCAN is a no-op in CLEAR (we're already there)
                else if (boring && tp.y >= 20) {
                    // No Squachy to tap for this in boring mode — any tap
                    // on the main content area (below the title bar, not
                    // a real button, and not already claimed by an edge
                    // zone above) cycles the background instead, so it's
                    // still reachable without him.
                    Settings::cycleBackground();
                }
            }

            // Finalize the Squachy gesture on the exact frame the touch
            // releases, wherever the finger happens to end up (it can
            // slide off him mid-stroke and still release cleanly).
            if (touchJustUp && sqActive) {
                if (!sqPetting && !sqHeld) {
                    Squachy::trigger(Squachy::Event::PETTED);
                }
                sqActive = false;
            }
            break;
        }
        case AppState::ALERT: {
#if defined(CYD35)
            if (frameBufferOk) {
                // Same two-pass half-height `frame` trick CLEAR/BOOT
                // use, reusing that same already-allocated sprite.
                // uiAlertTick() (and everything it calls) is a pure
                // function of `now`/the alert's own fixed detection
                // data, so calling it twice with the same `now` is safe.
                int halfH = tft.height() / 2;
                frame.setViewport(0, 0, tft.width(), tft.height(), true);
                uiAlertTick(frame, now);
                frame.pushSprite(0, 0);
                frame.setViewport(0, -halfH, tft.width(), tft.height(), true);
                uiAlertTick(frame, now);
                frame.pushSprite(0, halfH);
                frame.resetViewport();
            } else {
                uiAlertTick(tft, now);
            }
#else
            uiAlertTick(*canvas, now);
#endif
            if (tp.valid && (now - lastTouch) > TOUCH_DEBOUNCE_MS) {
                lastTouch = now;
                Squachy::trigger(Squachy::Event::DETECTION, lastAlertType, engine.lifetimeTotal(), lastAlertHits);
                enterClear();
            } else if ((now - alertStart) > ALERT_AUTO_DISMISS_MS) {
                Squachy::trigger(Squachy::Event::DETECTION, lastAlertType, engine.lifetimeTotal(), lastAlertHits);
                enterClear();
            }
            break;
        }
        case AppState::LOG: {
            uiLogTick(*canvas, now, engine, 0);
            if (tp.valid && (now - lastTouch) > TOUCH_DEBOUNCE_MS) {
                ButtonId b = Theme::hitTestButtonBar(tp.x, tp.y, tft.width(), tft.height());
                lastTouch = now;
                trackOutfitEasterEgg(b);
                if (b == ButtonId::SCAN) { enterClear(); }
                if (b == ButtonId::CLR)  { engine.clearLog(); enterClear(); }
                if (b == ButtonId::LOG)  { enterClear(); }   // toggle off
            }
            // Swipe to scroll: detect Y delta from prior touch
            static int lastY = -1;
            if (tp.valid) {
                if (lastY >= 0) {
                    int dy = tp.y - lastY;
                    if (abs(dy) > 10) {
                        uiLogScroll(dy > 0 ? 1 : -1);
                        lastY = tp.y;
                    }
                } else {
                    lastY = tp.y;
                }
            } else {
                lastY = -1;
            }
            break;
        }
        case AppState::SETTINGS: {
            uiSettingsTick(*canvas, now, engine);
            if (tp.valid && (now - lastTouch) > TOUCH_DEBOUNCE_MS) {
                lastTouch = now;
                SettingsRow row = uiSettingsHitTest(tp.x, tp.y, tft.width(), tft.height());
                switch (row) {
                    case SettingsRow::THEME:      Settings::cyclePalette(); break;
                    case SettingsRow::BACKGROUND: Settings::cycleBackground(); break;
                    case SettingsRow::INVERT:
                        Settings::toggleInvert();
                        // XOR against the panel's own baseline, not an
                        // absolute call -- see PANEL_NEEDS_INVERSION.
                        tft.invertDisplay(PANEL_NEEDS_INVERSION != Settings::inverted());
                        break;
                    case SettingsRow::BORING_MODE: Settings::toggleBoringMode(); break;
                    case SettingsRow::BRIGHTNESS:
                        Settings::adjustBrightness(tp.x < tft.width() / 2 ? -16 : 16);
                        applyBrightness();
                        break;
                    case SettingsRow::CONFIDENCE: Settings::cycleMinConfidence(); break;
                    case SettingsRow::CALIBRATE: {
#if defined(AWOK)
                        awokRunCalibration();
#elif defined(CYD35)
                        cyd35RunCalibration();
#else
                        TouchCal::RawReader reader = usingCapTouch ? rawReadCap : rawReadResistive;
                        TouchCal::Cal newCal = TouchCal::runInteractive(*canvas, reader, Theme::BG, Theme::WHITE, Theme::CYAN);
                        applyCal(newCal);
#endif
                        enterSettings();
                        break;
                    }
                    case SettingsRow::REPLAY_INTRO:
                        Squachy::replayIntro();
                        enterClear();
                        break;
                    case SettingsRow::NICKNAME:     Squachy::cycleNickname(); break;
                    case SettingsRow::SHADES_COLOR: Squachy::cycleShadesColor(); break;
                    case SettingsRow::OUTFIT:       enterOutfit(); break;
                    case SettingsRow::VIEW_DIARY:   enterDiary(); break;
                    case SettingsRow::RESET_STATS: engine.resetLifetime(); break;
                    case SettingsRow::BACK:        enterClear(); break;
                    default: break;
                }
            }
            break;
        }
        case AppState::DIARY: {
            uiDiaryTick(*canvas, now, engine);
            // Simple read-only info panel — any tap takes you back,
            // no button bar or scroll needed.
            if (tp.valid && (now - lastTouch) > TOUCH_DEBOUNCE_MS) {
                lastTouch = now;
                enterClear();
            }
            break;
        }
        case AppState::OUTFIT: {
            uiOutfitTick(*canvas, now);
            // Arrow taps cycle the equipped outfit (already persisted
            // live, no separate "confirm" step needed); a tap anywhere
            // else jumps straight back to the main screen, same "tap to
            // dismiss" feel as the Diary screen — the settings icon
            // (handled by the global back-navigation check above, which
            // runs before this switch and already changes `state`
            // itself when it fires) remains the way back to SETTINGS
            // specifically.
            if (tp.valid && (now - lastTouch) > TOUCH_DEBOUNCE_MS) {
                lastTouch = now;
                if (!uiOutfitTapArrow(tp.x, tp.y, tft.width(), tft.height())) {
                    enterClear();
                }
            }
            break;
        }
    }

#if !defined(CYD35)
    // Skipped on cyd35: this effect reads back already-drawn pixels to
    // shift them sideways, which is instant/reliable against the sprite
    // in RAM but would mean a live SPI readback from the panel itself
    // with no sprite buffer -- pixel readback (MISO) is a known-flaky
    // path on cheap SPI panels, not worth the risk for a 220ms cosmetic
    // transition effect.
    //
    // frameBufferOk guards both: if a rotate ever failed to reallocate
    // `frame` (see the rotate handler above), canvas already points
    // straight at tft and every draw this frame already landed on the
    // real screen -- pushing `frame` here would just paint stale data
    // from the sprite we stopped using back over the top of it.
    if (frameBufferOk) {
        if (now - transitionStart < TRANSITION_MS) {
            Theme::drawTransitionGlitch(frame, now - transitionStart, TRANSITION_MS);
        }
        frame.pushSprite(0, 0);
    }
#endif

    prevTouchValid = tp.valid;
}
