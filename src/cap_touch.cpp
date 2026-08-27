// SquachWatch-CYD — capacitive touch (CST816/CST820) driver
#include "cap_touch.h"
#include <Wire.h>

namespace CapTouch {

// Standard Hynitron CST816-family register map (same layout used
// across the common open-source drivers for this chip family):
//   0x02: finger count (0 = up, 1 = down — single-touch chip)
//   0x03: X high byte (low nibble = X[11:8])
//   0x04: X low byte
//   0x05: Y high byte (low nibble = Y[11:8])
//   0x06: Y low byte
static const uint8_t ADDR = 0x15;

void begin(int sda, int scl, int rst) {
    pinMode(rst, OUTPUT);
    digitalWrite(rst, LOW);
    delay(10);
    digitalWrite(rst, HIGH);
    delay(50);  // chip boot time after reset release
    Wire.begin(sda, scl);
}

bool probe() {
    Wire.beginTransmission(ADDR);
    return Wire.endTransmission() == 0;
}

bool read(uint16_t& x, uint16_t& y) {
    Wire.beginTransmission(ADDR);
    Wire.write(0x02);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((uint8_t)ADDR, (uint8_t)5) != 5) return false;

    uint8_t fingerNum = Wire.read();
    uint8_t xh = Wire.read();
    uint8_t xl = Wire.read();
    uint8_t yh = Wire.read();
    uint8_t yl = Wire.read();
    if (fingerNum == 0) return false;

    x = ((uint16_t)(xh & 0x0F) << 8) | xl;
    y = ((uint16_t)(yh & 0x0F) << 8) | yl;
    return true;
}

}  // namespace CapTouch
