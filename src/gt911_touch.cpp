#if defined(CROWPANEL7)
#include "gt911_touch.h"
#include "crowpanel7_board.h"
#include "crowpanel7_backlight.h"
#include <Arduino.h>
#include <Wire.h>

namespace Gt911 {

static uint8_t s_addr = 0;

static uint8_t s_diag = 8;   // first few failures only

// The register address goes out big-endian and the read needs a REPEATED
// START (endTransmission(false)); a stop between the address and the read
// makes the controller answer from wherever it happened to be.
static bool read16(uint16_t reg, uint8_t* out, size_t len) {
    if (!s_addr) return false;
    Wire.beginTransmission(s_addr);
    Wire.write((uint8_t)(reg >> 8));
    Wire.write((uint8_t)(reg & 0xFF));
    uint8_t et = Wire.endTransmission(false);
    if (et != 0) {
        if (s_diag) { s_diag--; Serial.printf("[touch] reg %04X: endTransmission=%u\n", reg, et); }
        return false;
    }
    size_t got = Wire.requestFrom((int)s_addr, (int)len);
    if (got != len) {
        if (s_diag) { s_diag--; Serial.printf("[touch] reg %04X: asked %u got %u\n", reg, (unsigned)len, (unsigned)got); }
        while (Wire.available()) Wire.read();
        return false;
    }
    for (size_t i = 0; i < len; i++) out[i] = Wire.read();
    return true;
}

static bool write16(uint16_t reg, uint8_t value) {
    if (!s_addr) return false;
    Wire.beginTransmission(s_addr);
    Wire.write((uint8_t)(reg >> 8));
    Wire.write((uint8_t)(reg & 0xFF));
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

static bool i2cPresent(uint8_t addr) {
    Wire.beginTransmission(addr);
    return Wire.endTransmission() == 0;
}

// The controller will not answer until the helper MCU has been told to wake
// it AND its INT line has been pulsed low. Holding INT low across the release
// of reset is also what latches its address to 0x5D rather than 0x14, which
// is why this runs before the identify rather than after a failed one.
static void wake() {
    for (uint8_t attempt = 0; attempt < 6; attempt++) {
        if (i2cPresent(GT911_ADDR_A)) return;
        CrowBL::begin();
        Wire.beginTransmission(STC8_ADDR);
        Wire.write((uint8_t)STC8_TOUCH_WAKE);
        Wire.endTransmission();
        pinMode(PIN_TOUCH_INT, OUTPUT);
        digitalWrite(PIN_TOUCH_INT, LOW);
        delay(120);
        pinMode(PIN_TOUCH_INT, INPUT);
        delay(100);
    }
}

static bool identify() {
    const uint8_t candidates[2] = { GT911_ADDR_A, GT911_ADDR_B };
    for (uint8_t i = 0; i < 2; i++) {
        s_addr = candidates[i];
        uint8_t id[6] = {0};
        if (read16(GT911_REG_PRODUCT_ID, id, sizeof(id)) &&
            id[0] == '9' && id[1] == '1' && id[2] == '1') {
            return true;
        }
    }
    s_addr = 0;
    return false;
}

bool begin() {
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_HZ);
    delay(20);
    CrowBL::begin();
    wake();
    if (!identify()) {
        Serial.println(F("[touch] no GT911 answered on 0x5D or 0x14"));
        return false;
    }
    Serial.printf("[touch] GT911 at 0x%02X\n", s_addr);
    return true;
}

bool present() { return s_addr != 0; }
uint8_t address() { return s_addr; }

// The GT911 sets bit 7 of its status register ("buffer ready") only when it
// has a NEW report, at its own scan rate; between reports the bit is clear
// even though the finger is still down. Answering "no contact" in those gaps
// turned one tap into a burst of down/up/down/up edges -- and on the WiFi
// password keyboard, one tap into three or four characters. So a contact is
// HELD until the controller says the finger has gone (a fresh report with
// zero touches), or until it has been silent for longer than any scan gap.
static bool     s_down = false;
static uint16_t s_x = 0, s_y = 0;
static uint32_t s_lastReportMs = 0;
static const uint32_t HOLD_MS = 80;   // GT911 reports every ~10-20 ms while touched
#if defined(CROWPANEL7_TOUCH_TRACE)
static uint32_t s_trDownMs = 0, s_trReports = 0, s_trMaxGap = 0;
#endif

bool read(uint16_t& x, uint16_t& y) {
    if (!s_addr) return false;

    uint8_t status = 0;
    if (!read16(GT911_REG_STATUS, &status, 1)) return false;

    if (status & 0x80) {
        const uint8_t touches = status & 0x0F;
        if (touches >= 1) {
            // 8 bytes per contact: [0] track id, [1..2] x LE, [3..4] y LE,
            // [5..6] size LE. Read from 0x814F, NOT 0x8150 -- one byte late
            // makes x out of (x_hi | y_lo << 8) and every real finger lands
            // past 1024, looking exactly like a ghost contact.
            uint8_t p[8];
            if (read16(GT911_REG_POINT1, p, sizeof(p))) {
                const uint16_t rx = (uint16_t)(p[1] | (p[2] << 8));
                const uint16_t ry = (uint16_t)(p[3] | (p[4] << 8));
                // Reject nonsense, never clamp it: a clamped bad read is a
                // tap on an edge of the screen that nobody made.
                if (rx < 1024 && ry < 1024) {
#if defined(CROWPANEL7_TOUCH_TRACE)
                    { const uint32_t now = millis();
                      if (!s_down) { Serial.printf("[touch] down at %u,%u\n", rx, ry); s_trDownMs = now; s_trReports = 0; s_trMaxGap = 0; }
                      else { const uint32_t gap = now - s_lastReportMs; if (gap > s_trMaxGap) s_trMaxGap = gap; }
                      s_trReports++; }
#endif
                    s_x = rx; s_y = ry; s_down = true; s_lastReportMs = millis();
                }
            }
        } else {
#if defined(CROWPANEL7_TOUCH_TRACE)
            if (s_down) Serial.printf("[touch] up after %lu ms: %lu reports, longest gap %lu ms (controller reported lift)\n",
                                      (unsigned long)(millis() - s_trDownMs), (unsigned long)s_trReports, (unsigned long)s_trMaxGap);
#endif
            s_down = false;                  // the controller saw the finger lift
        }
        // The status register must be written back to 0 after EVERY fresh
        // report, or the controller never raises the ready bit again.
        write16(GT911_REG_STATUS, 0);
    } else if (s_down && millis() - s_lastReportMs > HOLD_MS) {
#if defined(CROWPANEL7_TOUCH_TRACE)
        Serial.printf("[touch] up after %lu ms: %lu reports, longest gap %lu ms (TIMEOUT, no report for %lu ms)\n",
                      (unsigned long)(millis() - s_trDownMs), (unsigned long)s_trReports, (unsigned long)s_trMaxGap, (unsigned long)(millis() - s_lastReportMs));
#endif
        s_down = false;                      // silent too long: treat as lifted
    }

    if (!s_down) return false;
    x = s_x; y = s_y;
    return true;
}

}  // namespace Gt911
#endif
