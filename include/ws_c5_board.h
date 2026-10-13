// SquachWatch-CYD — Waveshare ESP32-C5-Touch-LCD-3.5: the I/O expander.
//
// On this board the panel's reset, the touch chip's reset, a power enable and
// the backlight are not GPIOs. They belong to a CH32V003 microcontroller on
// the I2C bus (SDA 27, SCL 26, address 0x24) running Waveshare's expander
// firmware, which takes plain register writes:
//
//   0x02  direction, one bit per EXIO pin (1 = output)
//   0x03  output levels, one bit per EXIO pin
//   0x05  backlight PWM duty, 0..255 (255 brightest)
//
//   EXIO0  FT6336 touch reset (active low)
//   EXIO1  ST7796 panel reset (active low)
//   EXIO5  power enable (Waveshare's BSP raises it with the two resets)
//
// The register map and the bring-up order are Waveshare's own, from
// example/Arduino-v3.3.10/example/07_lvgl_demo/Board_IO.cpp and the factory
// BSP (bsp_io_expander_init / bsp_display_brightness_set).
#pragma once
#include <stdint.h>

namespace WsC5 {
    static const int     I2C_SDA = 27;
    static const int     I2C_SCL = 26;
    static const uint8_t TOUCH_ADDR = 0x38;   // FT6336
    static const int     TOUCH_INT = 3;       // not used: the touch is polled
    static const int     SD_CS = 9;           // on the panel's SPI bus

    // Starts Wire on 27/26 and brings the board up the way Waveshare's demos
    // do: backlight dark, both resets pulsed, power enable on. Call BEFORE
    // tft.init(): the panel has no reset line of its own and will not answer
    // until EXIO1 has been released. False if the expander never answered,
    // in which case the panel is probably still held in reset.
    bool begin();

    // The firmware's 0..255 brightness (255 brightest), straight into the
    // expander's PWM register. Writes only when the value changes.
    void backlight(uint8_t duty);

    // Whether begin() found the expander.
    bool ok();

    // ---- AXP2101 power chip (I2C 0x34), battery only -------------------
    // Turns on the chip's battery/USB measurements, as Waveshare's own
    // example does. Leaves every power rail and the charger settings alone:
    // the rails feed the board itself. Call after begin().
    bool pmuBegin();
    // Percent and voltage of the cell, and whether it is charging (the cable
    // counts: full on USB still reads as charging, like the watch). False
    // when there is no PMU or no cell on the connector. Cached for 2 s.
    bool battery(uint8_t& pct, bool& charging, uint16_t& mv);

    // ---- PCF85063 clock chip (I2C 0x51) --------------------------------
    // At boot, after Clock::begin(): if the chip holds a time SquachWatch
    // wrote and it never stopped, the clock is set from it. From then on
    // every real time the clock learns is written back (Clock::onSet).
    // Kept in UTC.
    void rtcBegin();
}
