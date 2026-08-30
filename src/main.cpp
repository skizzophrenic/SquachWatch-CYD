// SquachWatch-CYD — main firmware
// Wires the state machine (DESIGN.md §9) across the UI modules
// and the DetectionEngine.

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include "state.h"
#include "theme.h"
#include "detection.h"
#include "ui_boot.h"
#include "ui_clear.h"
#include "ui_alert.h"
#include "ui_log.h"
#include "ui_settings.h"
#include "ui_diary.h"
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
//     XPT2046 again, but CS=33/IRQ=36 share the *display's* SPI bus
//     (SCK/MOSI/MISO = 14/13/12) instead of getting a dedicated
//     peripheral — this board has no capacitive-touch chip at all, so
//     the I2C probe below is skipped entirely rather than just failing.
#if defined(CYD35)
    #define TOUCH_SCK  TFT_SCLK
    #define TOUCH_MOSI TFT_MOSI
    #define TOUCH_MISO TFT_MISO
#else
    #define TOUCH_SCK  25
    #define TOUCH_MOSI 32
    #define TOUCH_MISO 39
#endif
#define TOUCH_CS   33
#define TOUCH_IRQ  36
#define CAP_SDA    33
#define CAP_SCL    32
#define CAP_RST    25
// Backlight brightness (Settings menu): both boards' backlight pins are
// driven at boot regardless of which one is actually wired (see the
// digitalWrite(HIGH) comment in setup() — same reasoning applies here),
// each on its own LEDC channel so ledcWrite can dim whichever one is
// real without needing to know which board this is.
#define BL_PIN_ORIG 21
#define BL_PIN_CAP  27
#define BL_CH_ORIG  0
#define BL_CH_CAP   1

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
TFT_eSprite         frame = TFT_eSprite(&tft);
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
DetectionType       lastAlertType = DetectionType::UNKNOWN;
uint32_t            lastAlertHits = 1; // times this exact MAC+type has ever matched — see Squachy's "seen before" reaction
const uint16_t      TOUCH_DEBOUNCE_MS = 200;
// The ALERT screen carries real information (type, confidence, MAC,
// RSSI) — tapping it away is the expected dismiss, but the automatic
// fallback still needs to actually clear itself in a reasonable time
// if nobody's there to tap it.
const uint32_t      ALERT_AUTO_DISMISS_MS = 10000;
// TFT_eSPI rotation: all four orientations are supported (0/2 portrait,
// 1/3 landscape), cycled in order by the rotate button in the title bar.
uint8_t             screenRotation = 1;
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
    // Touch is disabled on this board for now (see the setup() comment
    // by TOUCH_CS/TOUCH_IRQ) -- `touch` was never begin()'d, so don't
    // call into it at all.
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
    if (!touch.tirqTouched() || !touch.touched()) return false;
    TS_Point p = touch.getPoint();
    a = (int16_t)p.x;
    b = (int16_t)p.y;
    return true;
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
static void enterBoot() {
    state = AppState::BOOT;
    bootStart = millis();
    transitionStart = bootStart;
    uiBootInit(*canvas);
}

static void enterClear() {
    state = AppState::CLEAR;
    transitionStart = millis();
    uiClearInit(*canvas);
}

static void enterAlert(const Detection& d) {
    state = AppState::ALERT;
    alertStart = millis();
    transitionStart = alertStart;
    lastAlertType = d.type;
    lastAlertHits = d.hits;
    uiAlertInit(*canvas, d);
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

// ---- Arduino setup / loop ----
void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println();
    Serial.println("SquachWatch-CYD v1.0  --  TALKING SASQUACH");

    // Backlight: the original board uses GPIO21 for this, the
    // JC2432W328C uses GPIO27 (confirmed by sweeping candidate pins on
    // a physical unit). Driving both HIGH is harmless either way —
    // each is simply an unused GPIO on the "other" board — and means
    // the screen lights up before we've even figured out which board
    // this is.
    pinMode(21, OUTPUT); digitalWrite(21, HIGH);
    pinMode(27, OUTPUT); digitalWrite(27, HIGH);

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
    // invertDisplay() sets an ABSOLUTE panel state -- it doesn't toggle
    // relative to whatever TFT_INVERSION_ON/OFF set at compile time, it
    // just overwrites it. Settings::inverted() is a cosmetic per-user
    // theme toggle (originally for the other board), unrelated to a
    // given panel's actual required polarity, so XOR the two: cyd35's
    // ST7796 needs INVON to render correctly (confirmed on real
    // hardware), the original board needs INVOFF, and either way the
    // user's cosmetic toggle still flips it relative to that baseline.
#if defined(CYD35)
    constexpr bool PANEL_NEEDS_INVERSION = true;
#else
    constexpr bool PANEL_NEEDS_INVERSION = false;
#endif
    tft.invertDisplay(PANEL_NEEDS_INVERSION != Settings::inverted());
    tft.fillScreen(Theme::BG);

    // Hand the digitalWrite(HIGH) backlight pins above off to LEDC PWM
    // so the settings-menu brightness slider can dim them — same
    // "drive both boards' pin, only one is really wired" reasoning as
    // the digitalWrite call, just with a duty cycle instead of a flat
    // HIGH.
    ledcSetup(BL_CH_ORIG, 5000, 8);
    ledcAttachPin(BL_PIN_ORIG, BL_CH_ORIG);
    ledcSetup(BL_CH_CAP, 5000, 8);
    ledcAttachPin(BL_PIN_CAP, BL_CH_CAP);
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
    // TEMPORARILY DISABLED: touch.begin()'d against CS=33/IRQ=36 on the
    // display's shared SPI bus produced constant garbage reads (SPI
    // transfers all reading back 0xFFFF -- nothing answering) AND the
    // IRQ line fires continuously even with isrWake forced false right
    // after begin(), which rules out a library quirk and points at
    // TOUCH_CS/TOUCH_IRQ just being the wrong pins for this specific
    // board's touch controller. Left uninitialized on purpose so the
    // display can be evaluated on its own, without a free-running IRQ
    // hammering the shared SPI bus. See TOUCH_CS/TOUCH_IRQ up top once
    // the real pins are confirmed.
    usingCapTouch = false;
    Serial.println("cyd35 build -- touch DISABLED pending real CS/IRQ pin confirmation.");
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
#if !defined(CYD35)
    // Skipped on cyd35: touch isn't begin()'d there right now (see the
    // TOUCH_CS/TOUCH_IRQ comment above), so rawReadResistive() would be
    // reading from an uninitialized driver.
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
#endif
    tft.fillScreen(Theme::BG);

    // Apply a saved touch calibration if one exists (long-press the
    // title bar on the CLEAR/LOG screen to (re)calibrate — see
    // checkCalibrationTrigger()); otherwise keep the compiled-in
    // defaults above.
    TouchCal::Cal savedCal;
    if (TouchCal::load(savedCal)) {
        applyCal(savedCal);
        Serial.println("Loaded saved touch calibration.");
    }

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
    engine.loop();

    // Rotate button lives in the title bar's top-right corner, shown on
    // the CLEAR, LOG and SETTINGS screens (drawTitleBar always draws
    // it — gating the hit-test to these three keeps it inert wherever
    // there's no title bar drawn at all, i.e. BOOT/ALERT).
    if (tp.valid && (state == AppState::CLEAR || state == AppState::LOG || state == AppState::SETTINGS) &&
        Theme::rotateButtonHit(tp.x, tp.y, tft.width()) &&
        (now - lastTouch) > TOUCH_DEBOUNCE_MS) {
        lastTouch = now;
        Squachy::trigger(Squachy::Event::ROTATED);
        screenRotation = (screenRotation + 1) % 4;
        tft.setRotation(screenRotation);
        // Landscape and portrait need differently-shaped buffers —
        // recreate at the new dimensions rather than trying to reuse
        // the old (now wrong-shaped) one. canvas (most cyd35 screens)
        // draws straight to tft, which already reports its new rotated
        // width()/height() from setRotation() above and needs nothing
        // here; frame (cyd35's CLEAR-screen half-height band buffer, or
        // the other board's full-screen one) does.
        //
        // Confirmed on real hardware (2.8" board): this can fail even
        // with plenty of TOTAL free heap -- e.g. 121KB free but the
        // largest contiguous block only 73.7KB against a 76.8KB need --
        // because WiFi/BLE buffers fragment the heap during normal
        // operation, and it's not just transient noise: 8 retries with
        // delays between them (giving background tasks a chance to free
        // something) all failed identically in testing. Rather than
        // leave `frame` with no buffer at all (silent freeze -- the
        // rest of the firmware kept running fine, confirmed via a
        // heartbeat print during bring-up, but nothing ever reached the
        // panel again) or force a disruptive ESP.restart() every time
        // this happens, permanently fail over to unbuffered rendering
        // for the rest of this session -- the exact tradeoff cyd35
        // already accepts by default. The rotation itself still applies
        // fine either way; only the double-buffering is lost.
        frame.deleteSprite();
        frame.setColorDepth(8);
        bool spriteOk = false;
#if defined(CYD35)
        for (uint8_t attempt = 0; attempt < 3 && !spriteOk; attempt++) {
            if (attempt) delay(30);
            spriteOk = frame.createSprite(tft.width(), tft.height() / 2);
        }
#else
        for (uint8_t attempt = 0; attempt < 3 && !spriteOk; attempt++) {
            if (attempt) delay(30);
            spriteOk = frame.createSprite(tft.width(), tft.height());
        }
#endif
        if (!spriteOk) {
            Serial.printf("[rotate] frame buffer alloc failed (heap=%u largest=%u) -- falling back to unbuffered rendering\n",
                          ESP.getFreeHeap(), ESP.getMaxAllocHeap());
            frameBufferOk = false;
#if !defined(CYD35)
            canvas = &tft;
#endif
        }
        transitionStart = now;
    }

    // Settings button lives in the title bar's top-left corner, shown
    // on the same three screens as the rotate button. Tapping it while
    // already on the SETTINGS screen backs out to CLEAR instead of
    // re-entering itself — same toggle-off feel as the LOG button.
    if (tp.valid && (state == AppState::CLEAR || state == AppState::LOG || state == AppState::SETTINGS) &&
        Theme::settingsButtonHit(tp.x, tp.y) &&
        (now - lastTouch) > TOUCH_DEBOUNCE_MS) {
        lastTouch = now;
        if (state == AppState::SETTINGS) enterClear();
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
            TouchCal::RawReader reader = usingCapTouch ? rawReadCap : rawReadResistive;
            TouchCal::Cal newCal = TouchCal::runInteractive(*canvas, reader, Theme::BG, Theme::WHITE, Theme::CYAN);
            applyCal(newCal);
            enterClear();
        }
    } else {
        calHoldStart = 0;
    }

    switch (state) {
        case AppState::BOOT: {
            uiBootTick(*canvas, now);
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
            if (!boring && Squachy::onboardingActive() && tp.valid && (now - lastTouch) > TOUCH_DEBOUNCE_MS &&
                Squachy::onboardingTapAdvance(tp.x, tp.y)) {
                lastTouch = now;
            } else if (!boring && tp.valid && (now - lastTouch) > TOUCH_DEBOUNCE_MS && Squachy::hitTest(tp.x, tp.y)) {
                lastTouch = now;
                Squachy::trigger(Squachy::Event::PETTED);
                Settings::cycleBackground();
            } else if (tp.valid && (now - lastTouch) > TOUCH_DEBOUNCE_MS) {
                ButtonId b = Theme::hitTestButtonBar(tp.x, tp.y, tft.width(), tft.height());
                lastTouch = now;
                if (b == ButtonId::LOG)  { Squachy::trigger(Squachy::Event::LOG_OPENED); enterLog(); }
                else if (b == ButtonId::CLR) { engine.clearLog(); Squachy::trigger(Squachy::Event::LOG_CLEARED); enterClear(); }
                // SCAN is a no-op in CLEAR (we're already there)
                else if (boring && tp.y >= 20) {
                    // No Squachy to tap for this in boring mode — any tap
                    // on the main content area (below the title bar, not
                    // a real button) cycles the background instead, so
                    // it's still reachable without him.
                    Settings::cycleBackground();
                }
            }
            break;
        }
        case AppState::ALERT: {
            uiAlertTick(*canvas, now);
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
                        tft.invertDisplay(Settings::inverted());
                        break;
                    case SettingsRow::BORING_MODE: Settings::toggleBoringMode(); break;
                    case SettingsRow::BRIGHTNESS:
                        Settings::adjustBrightness(tp.x < tft.width() / 2 ? -16 : 16);
                        applyBrightness();
                        break;
                    case SettingsRow::CONFIDENCE: Settings::cycleMinConfidence(); break;
                    case SettingsRow::CALIBRATE: {
                        TouchCal::RawReader reader = usingCapTouch ? rawReadCap : rawReadResistive;
                        TouchCal::Cal newCal = TouchCal::runInteractive(*canvas, reader, Theme::BG, Theme::WHITE, Theme::CYAN);
                        applyCal(newCal);
                        enterSettings();
                        break;
                    }
                    case SettingsRow::REPLAY_INTRO:
                        Squachy::replayIntro();
                        enterClear();
                        break;
                    case SettingsRow::NICKNAME:     Squachy::cycleNickname(); break;
                    case SettingsRow::SHADES_COLOR: Squachy::cycleShadesColor(); break;
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
}
