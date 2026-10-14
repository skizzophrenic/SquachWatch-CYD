#if defined(WAVESHARE_C5)
#include "waveshare_c5_board.h"
#include <Arduino.h>
#include <Wire.h>

#define PIN_I2C_SDA    0
#define PIN_I2C_SCL    1
#define PIN_TOUCH_INT  5
#define I2C_HZ         400000

// ---- The CH32V003 helper ----
#define EXIO_ADDR      0x24
#define EXIO_REG_MODE  0x02   // one bit per EXIO, 1 = output
#define EXIO_REG_OUT   0x03   // one bit per EXIO, the levels
#define EXIO_REG_PWM   0x05   // the backlight, 0..255
#define EXIO_TP_RST    0      // touch reset, active low
#define EXIO_LCD_RST   1      // panel reset, active low
#define EXIO_PA_CTRL   3      // speaker amplifier enable, high = on

// ---- The CST3530 ----
#define CST_ADDR          0x58
#define CST_REG_DATA      0xD0070000u
#define CST_REG_END_READ  0xD00002ABu

namespace WsC5 {

static bool    s_ok  = false;
static uint8_t s_out = 0;   // the helper cannot be read back reliably; this is what was last sent

static bool writeReg(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(EXIO_ADDR);
    Wire.write(reg);
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

// The sound task flips the amp while the loop may be resetting the touch
// chip: the read-modify-write of the cached byte is the part Wire's own lock
// does not cover.
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

static bool setPin(uint8_t pin, bool high) {
    portENTER_CRITICAL(&s_mux);
    if (high) s_out |= (uint8_t)(1u << pin);
    else      s_out &= (uint8_t)~(1u << pin);
    const uint8_t v = s_out;
    portEXIT_CRITICAL(&s_mux);
    return writeReg(EXIO_REG_OUT, v);
}

void setAmp(bool on) {
    if (s_ok) setPin(EXIO_PA_CTRL, on);
}
bool begin() {
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_HZ);
    delay(10);
    // Both resets released (high), amplifier OFF: nothing here plays sound,
    // and an enabled NS4150B with a floating input can hiss. (Waveshare's own
    // init turns it on; that made no difference to the touch chip, tried.)
    s_out = (uint8_t)((1u << EXIO_TP_RST) | (1u << EXIO_LCD_RST));
    s_ok = writeReg(EXIO_REG_MODE, 0xFF) && writeReg(EXIO_REG_OUT, s_out);
    if (!s_ok) {
        Serial.println(F("[board] CH32 helper did not answer at 0x24: no panel reset, no backlight"));
        return false;
    }
    writeReg(EXIO_REG_PWM, 0);   // dark until the panel has something on it
    // Waveshare's timings: 50 ms low, 120 ms to come out of reset.
    setPin(EXIO_LCD_RST, false);
    delay(50);
    setPin(EXIO_LCD_RST, true);
    delay(120);
    Serial.println(F("[board] CH32 helper at 0x24; panel reset"));
    return true;
}

void setBacklight(uint8_t duty) {
    if (!s_ok) return;
    // Waveshare's own driver clamps at 97%; 255 here is that, not more.
    writeReg(EXIO_REG_PWM, (uint8_t)(((uint16_t)duty * 247u + 127u) / 255u));
}

bool resetTouch() {
    if (!s_ok) return false;
    // 100 ms low, then 500 ms before the controller answers -- Waveshare's.
    setPin(EXIO_TP_RST, false);
    delay(100);
    setPin(EXIO_TP_RST, true);
    delay(500);
    return true;
}

}  // namespace WsC5

namespace Cst3530 {

static bool s_ok = false;
static uint8_t s_diag = 8;   // first few failures only

// The register address is four bytes, most significant first, and the read
// needs a REPEATED START, as with the GT911.
static bool readReg(uint32_t reg, uint8_t* out, size_t len) {
    Wire.beginTransmission(CST_ADDR);
    Wire.write((uint8_t)(reg >> 24));
    Wire.write((uint8_t)(reg >> 16));
    Wire.write((uint8_t)(reg >> 8));
    Wire.write((uint8_t)(reg & 0xFF));
    const uint8_t et = Wire.endTransmission(false);
    // Quiet until the controller has answered once: asleep, every poll fails.
    if (et != 0) {
        if (s_ok && s_diag) { s_diag--; Serial.printf("[touch] reg %08lX: endTransmission=%u\n", (unsigned long)reg, et); }
        return false;
    }
    const size_t got = Wire.requestFrom((int)CST_ADDR, (int)len);
    if (got != len) {
        if (s_ok && s_diag) { s_diag--; Serial.printf("[touch] reg %08lX: asked %u got %u\n", (unsigned long)reg, (unsigned)len, (unsigned)got); }
        while (Wire.available()) Wire.read();
        return false;
    }
    for (size_t i = 0; i < len; i++) out[i] = Wire.read();
    return true;
}

// Address only, no data: tells the controller this report has been taken.
static void endRead() {
    Wire.beginTransmission(CST_ADDR);
    Wire.write((uint8_t)(CST_REG_END_READ >> 24));
    Wire.write((uint8_t)(CST_REG_END_READ >> 16));
    Wire.write((uint8_t)(CST_REG_END_READ >> 8));
    Wire.write((uint8_t)(CST_REG_END_READ & 0xFF));
    Wire.endTransmission();
}

bool begin() {
    pinMode(PIN_TOUCH_INT, INPUT);
    WsC5::resetTouch();
    Wire.beginTransmission(CST_ADDR);
    s_ok = Wire.endTransmission() == 0;
    if (!s_ok) {
        // Some controllers ignore an empty write: try a real read too.
        uint8_t b[9];
        s_ok = readReg(CST_REG_DATA, b, sizeof(b));
    }
    // Seen on the board 2026-10-13: on some boots the CST3530 does NOT answer
    // here on an untouched screen -- it sleeps, and the bus shows only the
    // codec, helper, RTC, IMU and SHTC3 (0x18 0x24 0x51 0x6B 0x70) -- and then
    // answers as soon as a finger lands; on others it answers at once.
    // read() keeps asking, so a failed probe is normal, not "no touch".
    if (s_ok) Serial.println(F("[touch] CST3530 at 0x58"));
    else      Serial.println(F("[touch] CST3530 asleep at boot (normal); it answers on the first touch"));
    return s_ok;
}

bool present() { return s_ok; }

// A contact is held across a short gap between reports, for the reason the
// GT911 driver gives: answering "no contact" between two scans of a finger
// that never lifted turns one tap into several.
static bool     s_down = false;
static uint16_t s_x = 0, s_y = 0;
static uint32_t s_lastMs = 0;
static const uint32_t HOLD_MS = 50;

bool read(uint16_t& x, uint16_t& y) {
    // Asked even when the boot probe failed: a controller that sleeps until a
    // finger lands does not answer a probe on an untouched screen.
    // The first nine bytes carry the count and the first contact; the others
    // (up to five) would follow from a second register, and one is all the
    // firmware uses.
    uint8_t b[9];
    if (readReg(CST_REG_DATA, b, sizeof(b))) {
        if (!s_ok) { s_ok = true; Serial.println(F("[touch] CST3530 answered after boot")); }
        const uint8_t n = b[3] & 0x0F;
        if (n >= 1 && n <= 5 && (b[8] & 0xF0) != 0) {
            s_x = (uint16_t)(((b[7] & 0x0F) << 8) | b[4]);
            s_y = (uint16_t)(((b[7] & 0xF0) << 4) | b[5]);
            s_down = true;
            s_lastMs = millis();
        } else {
            s_down = false;
        }
        endRead();
    } else if (s_down && millis() - s_lastMs > HOLD_MS) {
        s_down = false;
    }
    if (!s_down) return false;
    x = s_x; y = s_y;
    return true;
}

}  // namespace Cst3530
#endif
