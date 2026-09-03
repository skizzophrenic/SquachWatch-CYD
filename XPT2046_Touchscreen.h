// SquachWatch-Sim — XPT2046 resistive touch controller shim.
//
// Deliberately dumb: it reports whatever SimTouch holds and does no
// mapping of its own, because the firmware's pollTouch() owns all of
// that (raw->screen, rotation, axis swap, clamping) and the whole point
// is to run that real code rather than reimplement it.
//
// This is the board the emulator targets: the boot log on real hardware
// reads "No capacitive touch found -- assuming resistive XPT2046", so
// usingCapTouch ends up false and pollTouch() takes this path.
#pragma once
#include <cstdint>
#include "sim_touch.h"

struct TS_Point {
    int16_t x = 0, y = 0, z = 0;
    TS_Point() {}
    TS_Point(int16_t x_, int16_t y_, int16_t z_) : x(x_), y(y_), z(z_) {}
};

class XPT2046_Touchscreen {
public:
    XPT2046_Touchscreen(uint8_t /*csPin*/, uint8_t /*irqPin*/ = 255) {}

    bool begin() { return true; }
    template <typename T> bool begin(T&) { return true; }   // the SPIClass& overload

    // tirqTouched() is the IRQ-line check pollTouch() gates on before
    // touched(); both answer the same question here.
    bool tirqTouched() { return SimTouch::down; }
    bool touched()     { return SimTouch::down; }

    TS_Point getPoint() {
        return TS_Point((int16_t)SimTouch::rawX, (int16_t)SimTouch::rawY,
                        SimTouch::down ? 1000 : 0);
    }

    void setRotation(uint8_t) {}
};
