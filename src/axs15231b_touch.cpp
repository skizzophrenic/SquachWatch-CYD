// SquachWatch-CYD — the AXS15231B's touch, over I2C. See axs15231b_touch.h.
#include "axs15231b_touch.h"
#if defined(JC3248W535)
#include <Arduino.h>
#include <Wire.h>

namespace Axs15231bTouch {

// The command the controller expects before it returns touch data.
// From the JC3248W535 reference driver and the LVGL ESP32-S3 examples.
static const uint8_t READ_CMD[8] = {
    0xB5, 0xAB, 0xA5, 0x5A, 0x00, 0x00, 0x00, 0x08
};

static bool s_up = false;
static int  s_irq = -1;

void begin(int sda, int scl, int irq) {
    Wire.begin(sda, scl, 400000);   // 400 kHz fast mode
    s_irq = irq;
    if (irq >= 0) pinMode(irq, INPUT);
    // Probe: send the read command and see if the controller ACKs.
    Wire.beginTransmission(ADDR);
    Wire.write(READ_CMD, sizeof READ_CMD);
    uint8_t err = Wire.endTransmission();
    if (err != 0) {
        Serial.printf("[touch] AXS15231B at 0x%02X: no ACK (err %u)\n", ADDR, (unsigned)err);
        s_up = false;
        return;
    }
    // Read and discard one report to confirm the chip is alive.
    delay(10);
    Wire.requestFrom((uint8_t)ADDR, (uint8_t)8);
    uint8_t dummy[8];
    int got = 0;
    while (Wire.available() && got < 8) dummy[got++] = Wire.read();
    s_up = (got == 8);
    if (s_up) Serial.printf("[touch] AXS15231B at 0x%02X: ready (IRQ GPIO %d)\n", ADDR, irq);
    else      Serial.printf("[touch] AXS15231B at 0x%02X: probe read failed (%d bytes)\n", ADDR, got);
}

bool probe() { return s_up; }

bool read(uint16_t& x, uint16_t& y) {
    if (!s_up) return false;

    // Debounce / rate limit: the controller needs a moment between reads.
    static uint32_t s_nextTry = 0;
    static bool     s_down = false;
    static uint16_t s_x = 0, s_y = 0;
    static uint32_t s_seenAt = 0;
    const uint32_t now = millis();
    if ((int32_t)(now - s_nextTry) < 0) {
        x = s_x; y = s_y;
        return s_down;
    }

    // Send the read command
    Wire.beginTransmission(ADDR);
    Wire.write(READ_CMD, sizeof READ_CMD);
    if (Wire.endTransmission() != 0) {
        s_nextTry = now + 50;
        return false;
    }

    delayMicroseconds(50);

    // Read 8-byte response
    Wire.requestFrom((uint8_t)ADDR, (uint8_t)8);
    uint8_t data[8] = {0};
    int i = 0;
    while (Wire.available() && i < 8) data[i++] = Wire.read();
    if (i < 8) {
        s_nextTry = now + 50;
        return false;
    }

    // Parse touch data:
    //   data[0] == 0 && data[1] != 0  →  touch present
    //   data[2..3] → X (12-bit: low nibble of [2] << 8 | [3])
    //   data[4..5] → Y (12-bit: low nibble of [4] << 8 | [5])
    if (data[0] != 0 || data[1] == 0) {
        // No touch — but hold the last position briefly for debounce.
        if (s_down && now - s_seenAt > 80) {
            s_down = false;
        }
        x = s_x; y = s_y;
        return s_down;
    }

    // Touch detected
    s_seenAt = now;
    uint16_t rx = ((data[2] & 0x0F) << 8) | data[3];
    uint16_t ry = ((data[4] & 0x0F) << 8) | data[5];

    // The controller returns coordinates already in panel resolution.
    // Clamp to panel bounds.
    if (rx >= NX) rx = NX - 1;
    if (ry >= NY) ry = NY - 1;

    s_x = rx;
    s_y = ry;
    s_down = true;

    x = s_x; y = s_y;
    return true;
}

}  // namespace Axs15231bTouch
#endif  // JC3248W535
