// PROBE BUILD — capacitive touch coordinate calibration for the
// JC2432W328C. NOT the real app; branch probe/jc2432w328c only.
//
// Draws small yellow crosshair reference marks at known screen
// positions (corners + center) and, on every touch, plots a red dot
// at the RAW (x, y) the CST816/820 reports — no rotation/calibration
// applied. Also prints the raw values over Serial. Comparing where a
// physical tap lands vs. where the dot appears tells us exactly what
// transform (swap axes / invert one or both) turns raw controller
// coordinates into real screen coordinates, the same way the original
// board's resistive-touch mapping was derived.

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <TFT_eSPI.h>
#include "cap_touch.h"

TFT_eSPI tft = TFT_eSPI();

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println();
    Serial.println("=== CAP TOUCH CALIBRATION PROBE ===");

    // Confirmed by the backlight sweep: driving both candidate pins
    // HIGH lights the panel, and it's harmless on either board variant.
    pinMode(21, OUTPUT); digitalWrite(21, HIGH);
    pinMode(27, OUTPUT); digitalWrite(27, HIGH);

    tft.init();
    tft.setRotation(1);  // landscape — matches the real app's default
    tft.fillScreen(TFT_BLACK);

    CapTouch::begin(33, 32);
    bool found = CapTouch::probe();
    Serial.printf("Cap touch probe: %s\n", found ? "FOUND (0x15)" : "NOT FOUND");

    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(10, 10);
    tft.println("Tap the yellow crosses");
    tft.setTextSize(1);
    tft.setCursor(10, 40);
    tft.println("A RED dot is drawn at the raw");
    tft.setCursor(10, 50);
    tft.println("(x,y) the chip reports — no");
    tft.setCursor(10, 60);
    tft.println("correction applied yet.");
    tft.setCursor(10, 75);
    tft.println("Values also print over Serial.");

    int w = tft.width(), h = tft.height();
    int pts[5][2] = { {10, 100}, {w - 10, 100}, {10, h - 10}, {w - 10, h - 10}, {w / 2, h / 2} };
    for (auto& p : pts) {
        tft.drawFastHLine(p[0] - 5, p[1], 11, TFT_YELLOW);
        tft.drawFastVLine(p[0], p[1] - 5, 11, TFT_YELLOW);
    }
}

void loop() {
    static uint32_t lastPrint = 0;
    uint16_t rx, ry;
    if (CapTouch::read(rx, ry)) {
        if (rx < (uint16_t)tft.width() && ry < (uint16_t)tft.height()) {
            tft.fillCircle(rx, ry, 2, TFT_RED);
        }
        uint32_t now = millis();
        if (now - lastPrint > 150) {
            lastPrint = now;
            Serial.printf("raw x=%u y=%u  (screen %dx%d)\n", rx, ry, tft.width(), tft.height());
        }
    }
}
