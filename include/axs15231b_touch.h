// SquachWatch-CYD — the JC3248W535's touch, over I2C. The AXS15231B has a
// built-in touch controller at I2C address 0x3B, accessed via a special
// command sequence rather than simple register reads like the ST77922's TDDI.
//
// The protocol: write an 8-byte command (0xB5 0xAB 0xA5 0x5A 0x00 0x00
// 0x00 0x08), then read 8 bytes back. Touch is present when byte 0 == 0
// and byte 1 != 0. Coordinates are 12-bit in bytes 2-5, already scaled to
// the panel's 320x480 resolution.
//
// Pins: SDA=4, SCL=8, INT=3, address=0x3B.
#pragma once
#include <stdint.h>

namespace Axs15231bTouch {

static const uint8_t ADDR = 0x3B;

// The panel's physical pixel range, for bounds clamping. Like the ST77922
// touch, coordinates are in the panel's own portrait frame (0-319, 0-479).
static const uint16_t NX = 320, NY = 480;
// The native coordinate range, for TouchFit. The controller reports panel
// pixels directly, so these are [0, 320) and [0, 480).
static const uint16_t NX_MIN = 0, NX_MAX = 320, NY_MIN = 0, NY_MAX = 480;

// Start the I2C bus and probe the touch controller.
void begin(int sda, int scl, int irq);

// True once begin() has found the controller.
bool probe();

// Read one touch. Returns true if a finger is down, and sets x/y in
// PANEL coordinates (0-319, 0-479). The caller (main.cpp) rotates these
// through TouchFit the same way it does for every other board.
bool read(uint16_t& x, uint16_t& y);

}  // namespace Axs15231bTouch
