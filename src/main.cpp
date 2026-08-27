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
#include "squachy.h"
#include "cap_touch.h"

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
#define TOUCH_SCK  25
#define TOUCH_MOSI 32
#define TOUCH_MISO 39
#define TOUCH_CS   33
#define TOUCH_IRQ  36
#define CAP_SDA    33
#define CAP_SCL    32
#define CAP_RST    25

// ---- Globals ----
TFT_eSPI            tft = TFT_eSPI();
// All screens draw into this off-screen buffer, pushed to the physical
// display in one shot at the end of each loop(). Without it, every
// screen's erase-then-redraw sequence is briefly visible on real
// hardware — most noticeable as flickering text.
TFT_eSprite         frame = TFT_eSprite(&tft);
XPT2046_Touchscreen touch(TOUCH_CS, TOUCH_IRQ);
SPIClass            touchSPI(HSPI);
// Set once in setup() by probing for the capacitive controller —
// decides which branch pollTouch() takes for the rest of the run.
bool                usingCapTouch = false;
DetectionEngine     engine;
AppState            state     = AppState::BOOT;
uint32_t            bootStart = 0;
uint32_t            alertStart= 0;
uint32_t            lastTouch = 0;
DetectionType       lastAlertType = DetectionType::UNKNOWN;
const uint16_t      TOUCH_DEBOUNCE_MS = 200;
// The ALERT screen carries real information (type, confidence, MAC,
// RSSI) — tapping it away is the expected dismiss, but nobody should
// feel rushed reading it, so the automatic fallback is generous rather
// than a quick blink-and-you-missed-it 5 seconds.
const uint32_t      ALERT_AUTO_DISMISS_MS = 60000;
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

// CST816/CST820 native coordinate range measured against this exact
// JC2432W328C unit's 4 corners in landscape (rotation 1) — the chip's
// usable range falls short of the theoretical 0-239/0-319 panel
// resolution (bezel/active-area margin), so these are the real
// measured extremes, not assumed ones.
static const uint16_t CAP_NX_MIN = 32,  CAP_NX_MAX = 166;
static const uint16_t CAP_NY_MIN = 10,  CAP_NY_MAX = 308;

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

    // Resistive XPT2046 path (original jczn_2432s028r board) —
    // unchanged from the earlier single-board firmware.
    if (!touch.tirqTouched()) return tp;
    if (touch.touched()) {
        TS_Point p = touch.getPoint();
        if (!landscape) {
            tp.x = flipped ? map(p.x, 200, 3800, w, 0) : map(p.x, 200, 3800, 0, w);
            tp.y = flipped ? map(p.y, 200, 3800, h, 0) : map(p.y, 200, 3800, 0, h);
        } else {
            tp.x = flipped ? map(p.y, 200, 3800, w, 0) : map(p.y, 200, 3800, 0, w);
            tp.y = flipped ? map(p.x, 200, 3800, 0, h) : map(p.x, 200, 3800, h, 0);
        }
        tp.valid = (tp.x >= 0 && tp.x < w && tp.y >= 0 && tp.y < h);
    }
    return tp;
}

// ---- State transitions ----
static void enterBoot() {
    state = AppState::BOOT;
    bootStart = millis();
    transitionStart = bootStart;
    uiBootInit(frame);
}

static void enterClear() {
    state = AppState::CLEAR;
    transitionStart = millis();
    uiClearInit(frame);
}

static void enterAlert(const Detection& d) {
    state = AppState::ALERT;
    alertStart = millis();
    transitionStart = alertStart;
    lastAlertType = d.type;
    uiAlertInit(frame, d);
}

static void enterLog() {
    state = AppState::LOG;
    transitionStart = millis();
    uiLogInit(frame);
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
    tft.fillScreen(Theme::BG);

    // 8-bit (palette) mode: 320x240 needs ~75KB instead of ~150KB at
    // 16-bit — the full 16-bit buffer didn't fit in the available
    // contiguous heap on this board.
    frame.setColorDepth(8);
    if (!frame.createSprite(tft.width(), tft.height())) {
        Serial.println("ERROR: frame buffer allocation failed (low memory)");
    }
    frame.setTextSize(1);

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

    // Seed the PRNG so the matrix rain starts in a fresh-looking state
    // on every boot. Analog read on a floating pin is plenty.
    randomSeed(analogRead(34));

    engine.init();
    Squachy::trigger(Squachy::Event::BOOTED);
    enterBoot();
}

void loop() {
    uint32_t now = millis();
    TouchPoint tp = pollTouch();
    engine.loop();

    // Rotate button lives in the title bar's top-right corner, shown on
    // the CLEAR and LOG screens only.
    if (tp.valid && (state == AppState::CLEAR || state == AppState::LOG) &&
        Theme::rotateButtonHit(tp.x, tp.y, tft.width()) &&
        (now - lastTouch) > TOUCH_DEBOUNCE_MS) {
        lastTouch = now;
        Squachy::trigger(Squachy::Event::ROTATED);
        screenRotation = (screenRotation + 1) % 4;
        tft.setRotation(screenRotation);
        // Landscape and portrait need differently-shaped buffers —
        // recreate at the new dimensions rather than trying to reuse
        // the old (now wrong-shaped) one.
        frame.deleteSprite();
        frame.setColorDepth(8);
        frame.createSprite(tft.width(), tft.height());
        transitionStart = now;
    }

    switch (state) {
        case AppState::BOOT: {
            uiBootTick(frame, now);
            if (uiBootDone(bootStart)) {
                enterClear();
            }
            break;
        }
        case AppState::CLEAR: {
            uiClearTick(frame, now, engine);
            // Check for new detection
            const Detection* latest = engine.latest();
            if (latest && (now - latest->firstSeen) < 200) {
                enterAlert(*latest);
            }
            // Buttons
            if (tp.valid && (now - lastTouch) > TOUCH_DEBOUNCE_MS) {
                ButtonId b = Theme::hitTestButtonBar(tp.x, tp.y, tft.width(), tft.height());
                lastTouch = now;
                if (b == ButtonId::LOG)  { Squachy::trigger(Squachy::Event::LOG_OPENED); enterLog(); }
                if (b == ButtonId::CLR)  { engine.clearLog(); Squachy::trigger(Squachy::Event::LOG_CLEARED); enterClear(); }
                // SCAN is a no-op in CLEAR (we're already there)
            }
            break;
        }
        case AppState::ALERT: {
            uiAlertTick(frame, now);
            if (tp.valid && (now - lastTouch) > TOUCH_DEBOUNCE_MS) {
                lastTouch = now;
                Squachy::trigger(Squachy::Event::DETECTION, lastAlertType, engine.lifetimeTotal());
                enterClear();
            } else if ((now - alertStart) > ALERT_AUTO_DISMISS_MS) {
                Squachy::trigger(Squachy::Event::DETECTION, lastAlertType, engine.lifetimeTotal());
                enterClear();
            }
            break;
        }
        case AppState::LOG: {
            uiLogTick(frame, now, engine, 0);
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
    }

    if (now - transitionStart < TRANSITION_MS) {
        Theme::drawTransitionGlitch(frame, now - transitionStart, TRANSITION_MS);
    }
    frame.pushSprite(0, 0);
}
