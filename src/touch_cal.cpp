// SquachWatch-CYD — persistent touch calibration
#include "touch_cal.h"
#include <Preferences.h>
#include <Arduino.h>
#include <math.h>

namespace TouchCal {

static const char* NS = "touchcal";

// How far in from each screen edge the crosshairs are drawn -- as close to
// the true corners as the crosshair graphic (+/-8px around its centre) can
// render.
//
// pollTouch()'s map() calls treat the four stored values as the screen EDGES
// (0 and w, 0 and h), and for a long time the values stored were the raw
// readings AT the crosshairs. So the inset became a stretch away from the
// centre of every touch afterward, by span / (span - 2 * MARGIN): 6% across a
// 320-pixel screen and 8% down a 240-pixel one. A bigger inset (originally
// 24px) was the first version of that bug; shrinking it to 9 only shrank it.
// Measured on hardware, typing on the QWERTY board: presses landed at
// 1.065 x the key's true position - 12.7, putting the outer keys a third of a
// key off -- R typed as E, H as G.
//
// The fix is toEdges() below: each pair of readings is extended outward along
// its own line to where the edges actually are, before it is stored.
static const int MARGIN = 9;

// Calibrations saved before toEdges() existed have no "v" key. They are
// corrected once, on load, with the spans every board this runs on has in the
// orientation calibration runs in -- raw "a" across the 240-pixel side, "b"
// across the 320 -- and re-saved as version 2 so it never happens twice.
static const uint8_t CAL_VERSION = 2;
static const int     LEGACY_SPAN_A = 240, LEGACY_SPAN_B = 320;

// Extends the raw readings taken MARGIN pixels in from each end of a span out
// to the ends themselves. Works whichever of the two is larger.
static void toEdges(int16_t& lo, int16_t& hi, int span) {
    if (span <= 2 * MARGIN) return;
    const float k = (float)MARGIN / (float)(span - 2 * MARGIN);
    const float d = (float)hi - (float)lo;
    lo = (int16_t)lroundf((float)lo - d * k);
    hi = (int16_t)lroundf((float)hi + d * k);
}

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
//
// The bounds are a little wider than the ADC's 0..4095: a value extended out
// to the screen edge by toEdges() can legitimately sit just past the range the
// panel itself ever reports.
static bool plausible(const Cal& c, int16_t minSpread) {
    auto ok = [minSpread](int16_t a, int16_t b) {
        return a >= -1024 && b >= -1024 && a <= 5119 && b <= 5119 &&
               abs((int)a - (int)b) >= minSpread;
    };
    return ok(c.aTop, c.aBottom) && ok(c.bLeft, c.bRight);
}

bool load(Cal& out, int16_t minSpread) {
    Preferences p;
    p.begin(NS, true);
    bool has = p.isKey("aTop");
    bool legacy = false;
    Cal c = out;
    if (has) {
        c.aTop    = (int16_t)p.getShort("aTop", 0);
        c.aBottom = (int16_t)p.getShort("aBottom", 0);
        c.bLeft   = (int16_t)p.getShort("bLeft", 0);
        c.bRight  = (int16_t)p.getShort("bRight", 0);
        legacy = p.getUChar("v", 1) < CAL_VERSION;
    }
    p.end();
    if (!has) return false;
    if (legacy) {
        toEdges(c.aTop, c.aBottom, LEGACY_SPAN_A);
        toEdges(c.bLeft, c.bRight, LEGACY_SPAN_B);
    }
    if (!plausible(c, minSpread)) return false;
    // Written back already corrected, so the correction is applied once.
    if (legacy) save(c);
    out = c;
    return true;
}

void save(const Cal& cal) {
    Preferences p;
    p.begin(NS, false);
    p.putShort("aTop", cal.aTop);
    p.putShort("aBottom", cal.aBottom);
    p.putShort("bLeft", cal.bLeft);
    p.putShort("bRight", cal.bRight);
    p.putUChar("v", CAL_VERSION);
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

    // MARGIN in from each corner; toEdges() below takes the inset back out.
    int cx[4] = { MARGIN, w - MARGIN, MARGIN,     w - MARGIN };
    int cy[4] = { MARGIN, MARGIN,     h - MARGIN, h - MARGIN };
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
    // From the crosshairs out to the edges pollTouch() maps these onto.
    toEdges(cal.aTop, cal.aBottom, h);
    toEdges(cal.bLeft, cal.bRight, w);

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
