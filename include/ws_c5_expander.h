// SquachWatch-CYD — Waveshare ESP32-C5-Touch-LCD-3.5 I/O expander (0x24).
//
// The expander owns the panel's reset, the touch controller's reset, and the
// panel power / backlight PWM — none of which is a GPIO on this board. Register
// map and bring-up follow Waveshare's Board_IO example and were verified on
// this board during the Marauder/PorkChop C5 ports. Pinout per Waveshare's
// ESP32-C5-Touch-LCD-3.5 docs (https://docs.waveshare.com/ESP32-C5-Touch-LCD-3.5).
#pragma once
#include <stdint.h>

namespace WsExio {
    // Opens the shared I2C bus (SDA 27 / SCL 26), powers the panel rail, then
    // pulses the LCD (EXIO1) and touch (EXIO0) resets, and sets the backlight
    // to full. Call ONCE, BEFORE tft.init(): it pulses the ST7796 reset, so a
    // second call after the panel is up would blank it — hence the internal
    // idempotency guard (the touch driver's begin() also calls this, safely).
    // Returns true if 0x24 answered.
    bool begin();

    // True once begin() has brought the expander up.
    bool present();

    // Backlight brightness via the expander PWM register (EXIO5, reg 0x05),
    // 0 = off .. 255 = full. This is the backlight path for this board; there
    // is no TFT_BL GPIO.
    void backlight(uint8_t duty);
}
