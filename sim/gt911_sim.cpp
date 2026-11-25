// SquachWatch-CYD browser emulator — the T-Deck's GT911 touch chip.
//
// The T-Deck build (make wasm-tdeck) reads touch through Gt911::read(), as the
// board does. Here the "chip" answers with whatever the page's finger is,
// held in SimTouch by sw_touch() -- in the same raw space the CYD build's
// XPT2046 path uses, so the firmware's default calibration maps it back onto
// the pixel that was tapped, exactly as it does for the CYD.
#include "gt911_touch.h"
#include "sim_touch.h"

namespace Gt911 {

bool begin()   { return true; }
bool present() { return true; }
uint8_t address() { return 0x5D; }

bool read(uint16_t& x, uint16_t& y) {
    if (!SimTouch::down) return false;
    x = SimTouch::rawX;
    y = SimTouch::rawY;
    return true;
}

}
