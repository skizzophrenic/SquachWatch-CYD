// SquachWatch-CYD — persistent touch calibration
#include "touch_cal.h"
#include <Preferences.h>
#include <Arduino.h>

namespace TouchCal {

static const char* NS = "touchcal";

// A stored (or freshly-sampled) calibration only counts as valid if it
// also looks like real data, not just "present"/"in range" -- protects
// against both corrupted NVS bytes AND a genuinely bad interactive
// sampling (confirmed on real hardware, twice: first a fresh
// calibration where two opposite corners landed on nearly the same
// raw reading -- 2333 vs. 2343, a spread of 10 -- then, after a first
// version of this check with too loose a floor, ANOTHER bad one with
// a spread of ~180-190, both producing a "successfully calibrated"
// board with completely unusable touch, since that near-zero
// denominator makes tiny raw differences swing wildly on screen).
// minSpread comes from the caller rather than being fixed in here --
// see the header comment for why one universal threshold can't
// correctly serve both capacitive (small legitimate range) and
// resistive (much larger) touch at once.
static bool plausible(const Cal& c, int16_t minSpread) {
    auto ok = [minSpread](int16_t a, int16_t b) {
        return a >= 0 && b >= 0 && a <= 4095 && b <= 4095 &&
               abs((int)a - (int)b) >= minSpread;
    };
    return ok(c.aTop, c.aBottom) && ok(c.bLeft, c.bRight);
}

bool load(Cal& out, int16_t minSpread) {
    Preferences p;
    p.begin(NS, true);
    bool has = p.isKey("aTop");
    if (has) {
        out.aTop    = (int16_t)p.getShort("aTop", 0);
        out.aBottom = (int16_t)p.getShort("aBottom", 0);
        out.bLeft   = (int16_t)p.getShort("bLeft", 0);
        out.bRight  = (int16_t)p.getShort("bRight", 0);
        if (!plausible(out, minSpread)) has = false;
    }
    p.end();
    return has;
}

void save(const Cal& cal) {
    Preferences p;
    p.begin(NS, false);
    p.putShort("aTop", cal.aTop);
    p.putShort("aBottom", cal.aBottom);
    p.putShort("bLeft", cal.bLeft);
    p.putShort("bRight", cal.bRight);
    p.end();
}

void reset() {
    Preferences p;
    p.begin(NS, false);
    p.clear();
    p.end();
}

// Returns false if no touch ever registered within the timeout. Every
// wait loop in here used to be unbounded -- confirmed on real hardware
// that a genuinely flaky/marginal touch signal (touched() flickering
// between reporting a touch and not, even mid-press) could leave one
// of these spinning forever. delay() yields cleanly, so an unbounded
// wait like that doesn't trip the watchdog -- it just runs forever
// with no crash, no reset, nothing in the log, until someone notices
// the board is frozen and power-cycles it. That's the actual cause of
// the calibration "crash" -- there was never an exception, just a
// wait condition that could never be satisfied.
static bool sampleCorner(TFT_eSPI& t, RawReader readRaw, int cx, int cy,
                         uint16_t bg, uint16_t accent, const char* label,
                         int16_t& aOut, int16_t& bOut) {
    t.setTextSize(1);
    t.setTextColor(accent, bg);
    t.setCursor(6, 6);
    t.print(label);

    t.drawFastHLine(cx - 6, cy, 13, accent);
    t.drawFastVLine(cx, cy - 6, 13, accent);
    t.drawCircle(cx, cy, 8, accent);

    const uint32_t TOUCH_WAIT_TIMEOUT_MS   = 15000;
    const uint32_t RELEASE_WAIT_TIMEOUT_MS = 5000;

    int16_t a, b;
    uint32_t waitStart = millis();
    while (!readRaw(a, b)) {
        if (millis() - waitStart > TOUCH_WAIT_TIMEOUT_MS) return false;
        delay(10);
    }

    // Sample while held for stability instead of trusting one reading.
    long sumA = 0, sumB = 0;
    int n = 0;
    uint32_t start = millis();
    while (millis() - start < 300) {
        if (readRaw(a, b)) { sumA += a; sumB += b; n++; }
        delay(10);
    }
    if (n > 0) { aOut = (int16_t)(sumA / n); bOut = (int16_t)(sumB / n); }
    else       { aOut = a; bOut = b; }

    // Wait for release so the next corner doesn't instantly re-trigger
    // -- bounded too, but just moves on rather than failing outright if
    // it times out, since a usable sample was already captured above.
    uint32_t releaseStart = millis();
    while (readRaw(a, b)) {
        if (millis() - releaseStart > RELEASE_WAIT_TIMEOUT_MS) break;
        delay(10);
    }
    delay(150);

    t.fillRect(cx - 12, cy - 12, 24, 24, bg);
    return true;
}

bool runInteractive(TFT_eSPI& t, RawReader readRaw,
                    uint16_t bg, uint16_t fg, uint16_t accent,
                    Cal& out, int16_t minSpread) {
    int w = t.width(), h = t.height();

    // As close to the true corners as the crosshair graphic itself can
    // render (it draws +/-8px from center) — pollTouch()'s map() calls
    // treat these samples as the actual screen edges (0/w, 0/h), so
    // any inset here becomes a direct, uncorrected offset in every
    // touch afterward. A big inset (originally 24px) was exactly that
    // bug.
    int margin = 9;
    int cx[4] = { margin, w - margin, margin,     w - margin };
    int cy[4] = { margin, margin,     h - margin, h - margin };
    const char* labels[4] = { "1/4", "2/4", "3/4", "4/4" };

    // Single pass -- an internal retry loop here (tried and reverted:
    // see git history) ran the whole blocking 4-corner sample sequence
    // up to 3 times back to back, which seemed related to real crashes
    // on hardware at the time but wasn't actually the cause -- the
    // real cause was sampleCorner()'s unbounded wait loops (see its own
    // comment), fixed directly there now regardless of how many passes
    // this runs. A single pass is what this function always did before
    // the plausibility check existed; if the check fails, the user
    // just long-presses to try again, same gesture, one attempt at a
    // time.
    t.fillRect(0, 0, w, h, bg);
    t.setTextColor(fg, bg);
    t.setTextSize(2);
    const char* msg = "TAP EACH CROSSHAIR";
    int mw = t.textWidth(msg);
    t.setCursor((w - mw) / 2, h / 2 - 10);
    t.print(msg);
    delay(900);
    t.fillRect(0, 0, w, h, bg);

    int16_t a[4], b[4];
    for (int i = 0; i < 4; i++) {
        if (!sampleCorner(t, readRaw, cx[i], cy[i], bg, accent, labels[i], a[i], b[i])) {
            t.fillRect(0, 0, w, h, bg);
            t.setTextColor(accent, bg);
            t.setTextSize(1);
            const char* fail = "No touch detected -- try again";
            int fw = t.textWidth(fail);
            t.setCursor((w - fw) / 2, h / 2 - 6);
            t.print(fail);
            delay(1800);
            return false;
        }
    }

    Cal cal;
    cal.aTop    = (int16_t)(((int)a[0] + a[1]) / 2);  // top-left, top-right
    cal.aBottom = (int16_t)(((int)a[2] + a[3]) / 2);  // bottom-left, bottom-right
    cal.bLeft   = (int16_t)(((int)b[0] + b[2]) / 2);  // top-left, bottom-left
    cal.bRight  = (int16_t)(((int)b[1] + b[3]) / 2);  // top-right, bottom-right

    if (plausible(cal, minSpread)) {
        save(cal);
        out = cal;
        t.fillRect(0, 0, w, h, bg);
        t.setTextColor(fg, bg);
        const char* done = "CALIBRATED!";
        int dw = t.textWidth(done);
        t.setCursor((w - dw) / 2, h / 2 - 10);
        t.print(done);
        delay(1000);
        return true;
    }

    // Implausible -- don't persist or hand back known-bad data. Caller
    // keeps whatever calibration (saved or compiled-in default) it
    // already had; the user can long-press to try the whole gesture
    // again.
    t.fillRect(0, 0, w, h, bg);
    t.setTextColor(accent, bg);
    t.setTextSize(1);
    const char* fail = "Calibration failed -- try again";
    int fw = t.textWidth(fail);
    t.setCursor((w - fw) / 2, h / 2 - 6);
    t.print(fail);
    delay(1800);
    return false;
}

}  // namespace TouchCal
