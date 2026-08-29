// SquachWatch-CYD — persistent touch calibration
#include "touch_cal.h"
#include <Preferences.h>
#include <Arduino.h>

namespace TouchCal {

static const char* NS = "touchcal";

bool load(Cal& out) {
    Preferences p;
    p.begin(NS, true);
    bool has = p.isKey("aTop");
    if (has) {
        out.aTop    = (int16_t)p.getShort("aTop", 0);
        out.aBottom = (int16_t)p.getShort("aBottom", 0);
        out.bLeft   = (int16_t)p.getShort("bLeft", 0);
        out.bRight  = (int16_t)p.getShort("bRight", 0);
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

static void sampleCorner(TFT_eSPI& t, RawReader readRaw, int cx, int cy,
                         uint16_t bg, uint16_t accent, const char* label,
                         int16_t& aOut, int16_t& bOut) {
    t.setTextSize(1);
    t.setTextColor(accent, bg);
    t.setCursor(6, 6);
    t.print(label);

    t.drawFastHLine(cx - 6, cy, 13, accent);
    t.drawFastVLine(cx, cy - 6, 13, accent);
    t.drawCircle(cx, cy, 8, accent);

    int16_t a, b;
    while (!readRaw(a, b)) { delay(10); }

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

    // Wait for release so the next corner doesn't instantly re-trigger.
    while (readRaw(a, b)) { delay(10); }
    delay(150);

    t.fillRect(cx - 12, cy - 12, 24, 24, bg);
}

Cal runInteractive(TFT_eSPI& t, RawReader readRaw,
                   uint16_t bg, uint16_t fg, uint16_t accent) {
    int w = t.width(), h = t.height();
    t.fillRect(0, 0, w, h, bg);
    t.setTextColor(fg, bg);
    t.setTextSize(2);
    const char* msg = "TAP EACH CROSSHAIR";
    int mw = t.textWidth(msg);
    t.setCursor((w - mw) / 2, h / 2 - 10);
    t.print(msg);
    delay(900);
    t.fillRect(0, 0, w, h, bg);

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

    int16_t a[4], b[4];
    for (int i = 0; i < 4; i++) {
        sampleCorner(t, readRaw, cx[i], cy[i], bg, accent, labels[i], a[i], b[i]);
    }

    Cal cal;
    cal.aTop    = (int16_t)(((int)a[0] + a[1]) / 2);  // top-left, top-right
    cal.aBottom = (int16_t)(((int)a[2] + a[3]) / 2);  // bottom-left, bottom-right
    cal.bLeft   = (int16_t)(((int)b[0] + b[2]) / 2);  // top-left, bottom-left
    cal.bRight  = (int16_t)(((int)b[1] + b[3]) / 2);  // top-right, bottom-right
    save(cal);

    t.fillRect(0, 0, w, h, bg);
    t.setTextColor(fg, bg);
    const char* done = "CALIBRATED!";
    int dw = t.textWidth(done);
    t.setCursor((w - dw) / 2, h / 2 - 10);
    t.print(done);
    delay(1000);

    return cal;
}

}  // namespace TouchCal
