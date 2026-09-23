// SquachWatch-CYD — GT911 capacitive touch on the CrowPanel 7.
//
// Same shape as src/cap_touch.cpp's CST820 driver (which is left byte-
// identical): a probe at boot, then a raw read that the caller feeds into
// TouchFit. The GT911 reports panel pixels, so on this board the fit is the
// identity and nothing ever needs calibrating.
#pragma once
#include <stdint.h>

namespace Gt911 {
    // Begins Wire (shared with the backlight helper — never call Wire.end())
    // and runs the wake dance. Returns true if a GT911 answered.
    bool begin();
    bool present();
    // One contact, in panel pixels. False when no finger is down.
    bool read(uint16_t& x, uint16_t& y);
    uint8_t address();
}
