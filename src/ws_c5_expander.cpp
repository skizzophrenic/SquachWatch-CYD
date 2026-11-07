// SquachWatch-CYD — Waveshare C5 3.5" I/O expander (0x24). See ws_c5_expander.h.
// Adapted from the verified PorkChop/Marauder C5 bring-up (ws_c5_exio.h).
#if defined(WS_C5_35)
#include "ws_c5_expander.h"
#include <Arduino.h>
#include <Wire.h>

namespace WsExio {

// Shared capacitive-touch / expander / sensor I2C bus on this board.
static const int  SDA_PIN = 27;
static const int  SCL_PIN = 26;

static const uint8_t ADDR       = 0x24;
static const uint8_t MODE_REG   = 0x02;   // direction/mode
static const uint8_t OUTPUT_REG = 0x03;   // output latch
static const uint8_t PWM_REG    = 0x05;   // backlight PWM
static const uint8_t TP_RST     = 0;      // EXIO0 -> FT6336 reset
static const uint8_t LCD_RST    = 1;      // EXIO1 -> ST7796 reset
static const uint8_t POWER      = 5;      // EXIO5 -> panel power / backlight

static uint8_t s_output = 0;
static bool    s_ready  = false;

static bool exioWrite(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(ADDR);
    Wire.write(reg);
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

static bool exioSet(uint8_t pin, bool level) {
    if (level) s_output |= (uint8_t)(1U << pin);
    else       s_output &= (uint8_t)~(1U << pin);
    return exioWrite(OUTPUT_REG, s_output);
}

bool present() { return s_ready; }

bool begin() {
    // Run ONCE. Pulsing EXIO1 (LCD reset) again after tft.init() would blank
    // the panel; the touch driver's begin() calls this too, so this guard
    // makes the repeat a safe no-op.
    if (s_ready) return true;

    Wire.begin(SDA_PIN, SCL_PIN, 100000U);  // 100 kHz for robust reads over the flex

    // Mark the three signals as outputs, then bring the rail up TOGETHER with
    // both resets released — resetting the touch chip while its rail is off
    // leaves the FT6336 unresponsive.
    bool ok = exioWrite(MODE_REG, 0xFF) &&
              exioWrite(OUTPUT_REG, 0x00) &&
              exioWrite(PWM_REG, 0x00) &&
              exioWrite(MODE_REG, (uint8_t)((1U << TP_RST) | (1U << LCD_RST) | (1U << POWER)));

    s_output = (uint8_t)((1U << TP_RST) | (1U << LCD_RST));
    ok = ok && exioWrite(OUTPUT_REG, s_output); delay(50);
    s_output = 0x00;
    ok = ok && exioWrite(OUTPUT_REG, s_output); delay(50);
    s_output = (uint8_t)((1U << TP_RST) | (1U << LCD_RST) | (1U << POWER));
    ok = ok && exioWrite(OUTPUT_REG, s_output); delay(50);

    // Power is on; pulse each reset (active-low) with the rail up.
    ok = ok && exioSet(LCD_RST, false); delay(20);
    ok = ok && exioSet(LCD_RST, true);  delay(120);
    ok = ok && exioSet(TP_RST, false);  delay(20);
    ok = ok && exioSet(TP_RST, true);   delay(300);   // FT6336 boot
    ok = ok && exioWrite(PWM_REG, 0xFF);              // backlight full

    s_ready = ok;
    Serial.printf("[WS-C5] I/O expander 0x24 %s\n", ok ? "ready" : "NOT FOUND");
    return ok;
}

void backlight(uint8_t duty) {
    if (!s_ready) return;
    exioWrite(PWM_REG, duty);
}

}  // namespace WsExio
#endif  // WS_C5_35
