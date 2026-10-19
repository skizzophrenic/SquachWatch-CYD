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
    // `rst` below zero skips the reset pulse (the T-Watch's FT6336 has no
    // reset line we drive); `addr` is the controller's I2C address, 0x15
    // for the CST816/CST820 and 0x38 for the FT6336, whose registers
    // 0x02..0x06 (finger count, X high/low, Y high/low) share a layout.
    void begin(int sda, int scl, int rst, uint8_t addr = 0x15);

    // True if a CST816/CST820 answers at its known I2C address (0x15).
    // Call after begin(); if this returns false, call Wire.end() and
    // fall back to the resistive touch path — nothing here should be
    // trusted without a confirmed probe.
    bool probe();

    // True and fills x/y (native controller coordinates, NOT yet
    // rotated/calibrated to the display's coordinate space) if a
    // finger is currently down.
    bool read(uint16_t& x, uint16_t& y);

#if defined(WAVESHARE_206)
    // The controller's interrupt line, active low while a finger is down.
    // Once set, read() asks the chip only while it is low: the Waveshare
    // 2.06"'s FT3168 dozes off when idle and does not answer until touched,
    // and a poll of it then is a timed-out I2C read every frame.
    void setIntPin(int pin);
#endif
}
