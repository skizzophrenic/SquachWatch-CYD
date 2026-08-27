// SquachWatch-CYD — capacitive touch (CST816/CST820) driver
// Used on the JC2432W328C board variant, whose touch controller sits
// on I2C (SDA/SCL) instead of the original board's dedicated XPT2046
// SPI bus. No IRQ pin is used — the vendor docs for this board say not
// to wire one, so this driver polls the controller directly.
#pragma once
#include <stdint.h>

namespace CapTouch {
    // Starts the I2C bus on the given pins and pulses the controller's
    // reset line (active-low, per this board's vendor docs). Without
    // this the chip can power up in a stale/undefined state — X moved
    // sensibly across taps in early testing but Y stayed frozen, which
    // looks exactly like an unreset chip. Call once from setup(),
    // before probe().
    void begin(int sda, int scl, int rst);

    // True if a CST816/CST820 answers at its known I2C address (0x15).
    // Call after begin(); if this returns false, call Wire.end() and
    // fall back to the resistive touch path — nothing here should be
    // trusted without a confirmed probe.
    bool probe();

    // True and fills x/y (native controller coordinates, NOT yet
    // rotated/calibrated to the display's coordinate space) if a
    // finger is currently down.
    bool read(uint16_t& x, uint16_t& y);
}
