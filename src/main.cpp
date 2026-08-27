// PROBE BUILD — throwaway hardware diagnostic for the JC2432W328C
// capacitive-touch CYD variant. NOT the real app; branch
// probe/jc2432w328c only, discard/checkout master to restore main.cpp.
//
// What it does:
//   1. Explicitly drives the backlight pin (GPIO21) high, in case it
//      isn't on by default on this variant.
//   2. Draws a sequence of known reference color swatches full-screen
//      with on-screen labels, so a human can visually confirm whether
//      colors render correctly or inverted against this board's
//      specific ST7789V panel/glass.
//   3. Scans the I2C bus on SDA=33 / SCL=32 (the CST816/CST820
//      capacitive touch controller's documented pins for this board)
//      and reports any responding addresses, both on-screen and over
//      Serial, so we can confirm the exact address before writing a
//      real touch driver.
// Uses the project's existing cyd_user_setup.h (already ST7789 on the
// same SPI pins this board uses), so the display config needs no
// changes to test on this hardware.

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <TFT_eSPI.h>

TFT_eSPI tft = TFT_eSPI();

static const int BL_PIN   = 21;
static const int I2C_SDA  = 33;
static const int I2C_SCL  = 32;

struct Swatch { uint16_t color; const char* name; };
static const Swatch SWATCHES[] = {
    { TFT_RED,   "RED"   },
    { TFT_GREEN, "GREEN" },
    { TFT_BLUE,  "BLUE"  },
    { TFT_WHITE, "WHITE" },
    { TFT_BLACK, "BLACK" },
};
static const int N_SWATCHES = sizeof(SWATCHES) / sizeof(SWATCHES[0]);

static void i2cScan() {
    Wire.begin(I2C_SDA, I2C_SCL);
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(4, 4);
    tft.println("I2C SCAN");
    tft.println("SDA=33 SCL=32");
    tft.println();

    Serial.println();
    Serial.println("=== I2C scan (SDA=33, SCL=32) ===");
    int found = 0;
    int y = tft.getCursorY();
    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        uint8_t err = Wire.endTransmission();
        if (err == 0) {
            found++;
            char buf[32];
            snprintf(buf, sizeof(buf), "FOUND: 0x%02X", addr);
            Serial.println(buf);
            tft.setCursor(4, y);
            tft.println(buf);
            y = tft.getCursorY();
        }
    }
    if (found == 0) {
        Serial.println("No I2C devices found.");
        tft.setCursor(4, y);
        tft.println("(none found)");
    }
    Serial.println("=== scan done ===");
    Serial.println();
}

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println();
    Serial.println("=== JC2432W328C PROBE BUILD ===");

    pinMode(BL_PIN, OUTPUT);
    digitalWrite(BL_PIN, HIGH);
    Serial.println("Backlight (GPIO21) driven HIGH.");

    tft.init();
    tft.setRotation(1);
    Serial.printf("tft.width()=%d tft.height()=%d\n", tft.width(), tft.height());

    i2cScan();
    delay(6000);
}

void loop() {
    static uint32_t lastSwitch = 0;
    static int idx = 0;
    uint32_t now = millis();
    if (now - lastSwitch > 2500) {
        lastSwitch = now;
        const Swatch& s = SWATCHES[idx];
        tft.fillScreen(s.color);
        uint16_t textCol = (s.color == TFT_WHITE) ? TFT_BLACK : TFT_WHITE;
        tft.setTextColor(textCol, s.color);
        tft.setTextSize(3);
        tft.setCursor(10, 10);
        tft.println(s.name);
        tft.setTextSize(1);
        tft.setCursor(10, 40);
        tft.println("If this doesn't match the label,");
        tft.setCursor(10, 50);
        tft.println("colors are likely inverted.");
        Serial.printf("Showing swatch: %s\n", s.name);
        idx = (idx + 1) % N_SWATCHES;
    }
}
