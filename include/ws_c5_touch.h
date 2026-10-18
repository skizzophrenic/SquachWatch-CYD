// SquachWatch-CYD — FT6336 capacitive touch on the Waveshare C5 3.5".
//
// I2C-only (addr 0x38) on the shared bus (SDA 27 / SCL 26). Touch reset and
// panel power are on the 0x24 I/O expander (ws_c5_expander.h), so begin() brings
// the expander up first. Reports panel pixels (native portrait 0..319 x 0..479),
// so like the GT911 path the TouchFit is the identity. The C5 needs the bus
// re-opened on each read (see read()); verified on this board during the
// Marauder/PorkChop C5 ports.
#pragma once
#include <stdint.h>

namespace WsTouch {
    // Brings up the expander (power + resets + backlight) and sets the FT6336
    // sensitivity. Returns true if the FT6336 answered. Call once from setup().
    bool begin();
    bool present();
    // One contact, in panel pixels. False when no finger is down.
    bool read(uint16_t& x, uint16_t& y);
}
