// SquachWatch-CYD — Waveshare ESP32-C5-Touch-LCD-3.5: the I/O expander.
// See include/ws_c5_board.h for the register map and where it came from.
#if defined(WAVESHARE_C5)
#include "ws_c5_board.h"
#include <Arduino.h>
#include <Wire.h>

namespace WsC5 {

static const uint8_t EXIO_ADDR  = 0x24;
static const uint8_t REG_MODE   = 0x02;
static const uint8_t REG_OUTPUT = 0x03;
static const uint8_t REG_PWM    = 0x05;

static const uint8_t EXIO_TP_RST  = 0;
static const uint8_t EXIO_LCD_RST = 1;
static const uint8_t EXIO_POWER   = 5;

static bool    s_ok     = false;
static uint8_t s_out    = 0;
static int     s_lastBl = -1;

static bool writeReg(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(EXIO_ADDR);
    Wire.write(reg);
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

bool begin() {
    // The card shares the panel's bus: hold its chip select off before the
    // first byte TFT_eSPI sends, or the card can answer a panel command.
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);

    Wire.begin(I2C_SDA, I2C_SCL);
    Wire.setClock(400000);

    // Waveshare's order: everything an input with outputs pre-loaded low and
    // the backlight off, then the three outputs enabled, then both resets
    // pulsed high-low-high with the power enable raised at the end.
    const uint8_t outputs = (1u << EXIO_TP_RST) | (1u << EXIO_LCD_RST) | (1u << EXIO_POWER);
    s_ok = writeReg(REG_MODE, 0xFF) &&
           writeReg(REG_OUTPUT, 0x00) &&
           writeReg(REG_PWM, 0x00) &&
           writeReg(REG_MODE, outputs);
    if (!s_ok) {
        // Once more after a breath: the CH32V003 boots off the same rail and
        // can still be starting when the C5 gets here on a cold power-up.
        delay(50);
        s_ok = writeReg(REG_MODE, 0xFF) &&
               writeReg(REG_OUTPUT, 0x00) &&
               writeReg(REG_PWM, 0x00) &&
               writeReg(REG_MODE, outputs);
    }
    if (!s_ok) {
        Serial.println("[ws-c5] I/O expander (CH32V003 @0x24) did not answer: panel may stay in reset");
        return false;
    }
    s_lastBl = 0;

    s_out = (1u << EXIO_TP_RST) | (1u << EXIO_LCD_RST);
    writeReg(REG_OUTPUT, s_out);
    delay(50);
    s_out = 0;
    writeReg(REG_OUTPUT, s_out);
    delay(50);
    s_out = outputs;
    writeReg(REG_OUTPUT, s_out);
    // The ST7796 wants 120 ms after reset before it takes SLPOUT; the FT6336
    // a little less. tft.init() comes straight after this.
    delay(120);
    Serial.println("[ws-c5] I/O expander up: panel and touch out of reset, power on");
    return true;
}

void backlight(uint8_t duty) {
    if (!s_ok || (int)duty == s_lastBl) return;
    if (writeReg(REG_PWM, duty)) s_lastBl = duty;
}

bool ok() { return s_ok; }

}  // namespace WsC5
#endif
