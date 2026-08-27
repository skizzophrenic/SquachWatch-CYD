// SquachWatch-CYD — main firmware
// Wires the state machine (DESIGN.md §9) across the UI modules
// and the DetectionEngine.

#include <Arduino.h>
#include <SPI.h>
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

// Touch is on its OWN dedicated SPI bus, entirely separate from the
// display — NOT a shared bus, despite that being the near-universal
// (and wrong, for this board revision) assumption in CYD community
// docs. Confirmed against Espressif's official board-variant file for
// this exact board (arduino-esp32 variants/jczn_2432s028r/pins_arduino.h):
//   display: DC=2 MISO=12 MOSI=13 SCK=14 CS=15 BL=21   (VSPI, via TFT_eSPI)
//   touch:   CS=33 IRQ=36 SCK=25  MOSI=32 MISO=39       (independent bus)
#define TOUCH_SCK  25
#define TOUCH_MOSI 32
#define TOUCH_MISO 39
#define TOUCH_CS   33
#define TOUCH_IRQ  36

// ---- Globals ----
TFT_eSPI            tft = TFT_eSPI();
// All screens draw into this off-screen buffer, pushed to the physical
// display in one shot at the end of each loop(). Without it, every
// screen's erase-then-redraw sequence is briefly visible on real
// hardware — most noticeable as flickering text.
TFT_eSprite         frame = TFT_eSprite(&tft);
XPT2046_Touchscreen touch(TOUCH_CS, TOUCH_IRQ);
SPIClass            touchSPI(HSPI);
DetectionEngine     engine;
AppState            state     = AppState::BOOT;
uint32_t            bootStart = 0;
uint32_t            alertStart= 0;
uint32_t            lastTouch = 0;
DetectionType       lastAlertType = DetectionType::UNKNOWN;
const uint16_t      TOUCH_DEBOUNCE_MS = 200;
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

static TouchPoint pollTouch() {
    TouchPoint tp = { false, 0, 0 };
    if (!touch.tirqTouched()) return tp;
    if (touch.touched()) {
        TS_Point p = touch.getPoint();
        int w = tft.width(), h = tft.height();
        // XPT2046 raw ADC axes, solved back to the panel's native
        // (rotation 0, portrait) frame from the landscape (rotation 1)
        // calibration measured against known button positions: raw X
        // maps directly to native column, raw Y directly to native
        // row — no swap in the native frame itself. Each TFT_eSPI
        // rotation is then a fixed transform of that native frame:
        // odd rotations (1,3) are landscape and swap the axes; the
        // "flipped" pair (2,3 vs 0,1) reverses both directions. This
        // reproduces the two empirically-verified landscape mappings
        // exactly, so the same method should hold for portrait.
        bool landscape = (screenRotation % 2) == 1;
        bool flipped   = screenRotation >= 2;
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

    // Touch has its own dedicated SPI bus (see pin table above) — a
    // genuinely separate HSPI peripheral on pins that don't overlap the
    // display's VSPI pins at all, so there's no GPIO-matrix conflict.
    touchSPI.begin(TOUCH_SCK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
    touch.begin(touchSPI);
    touch.setRotation(0);

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
            } else if ((now - alertStart) > 5000) {
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
