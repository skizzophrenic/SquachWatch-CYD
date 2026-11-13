// SquachWatch-CYD — Waveshare ESP32-C5-Touch-LCD-2.8: the board's I2C side.
//
// Two chips on one I2C bus (SDA 0, SCL 1) that the rest of the firmware never
// has to know about:
//
//   - A CH32V003 helper MCU at 0x24, running Waveshare's own firmware (it ships
//     programmed). It owns the panel's RESET line (EXIO1), the touch chip's
//     RESET (EXIO0), the speaker amplifier's enable (EXIO3) and the BACKLIGHT,
//     which is its PWM output -- there is no backlight GPIO on this board at
//     all. Nothing shows on the panel until this has been spoken to.
//   - A CST3530 capacitive touch controller at 0x58, 32-bit register
//     addresses, INT on GPIO5 (unused: polled).
//
// Register map and timings from Waveshare's Arduino examples
// (github.com/waveshareteam/ESP32-C5-Touch-LCD-2.8, examples/arduino/01_exio
// and 06_lvgl_demo). The QMI8658, PCF85063, SHTC3 and ES8311 share the bus and
// are left alone.
#pragma once
#include <stdint.h>

namespace WsC5 {
    // Brings Wire up and the helper chip to a known state: every EXIO an
    // output, both resets released, amplifier off, backlight off. Then pulses
    // the panel's reset. Call BEFORE tft.init() -- TFT_RST is -1 here, so the
    // library cannot reset the panel itself. True if the helper answered.
    bool begin();
    // 0..255, 255 brightest -- the firmware's own convention. The helper takes
    // 0..255 too, but Waveshare's driver never sends more than 97%; neither
    // does this.
    void setBacklight(uint8_t duty);
    // Pulses the touch chip's reset (EXIO0) with Waveshare's timings.
    bool resetTouch();
    // The speaker amplifier's enable (EXIO3). Safe from the sound task.
    // (EXIO6 is NOT spare: driven high, it reset the board. Leave it alone.)
    void setAmp(bool on);
}

namespace Cst3530 {
    // Pulses the touch reset through the helper and probes 0x58. Call after
    // WsC5::begin(). True if the controller answered.
    bool begin();
    bool present();
    // One contact, in the controller's own (panel-portrait) units. False when
    // no finger is down.
    bool read(uint16_t& x, uint16_t& y);
}
