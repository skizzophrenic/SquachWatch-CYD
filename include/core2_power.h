// SquachWatch-CYD -- the M5Stack Core2's power chip.
//
// Nothing on this board is switched by a GPIO the way a CYD's is. The screen's
// supply and reset line, the touch chip's reset, the microSD's supply and the
// backlight are all outputs of the power chip on I2C (SDA 21, SCL 22, 0x34):
// an AXP192 on the first Core2, an AXP2101 on v1.1 and later. They are told
// apart by their ID register, so one build serves both.
//
// The register sequences are M5GFX's own (M5GFX.cpp, the Core2 autodetect and
// Light_M5StackCore2 / Light_M5StackCore2_AXP2101) and M5Unified's battery
// reads; nothing here is guessed.
#pragma once
#include <stdint.h>

namespace Core2Power {
    // Starts the I2C bus, finds the chip, turns on the LCD/touch/SD supplies and
    // pulses the panel's reset. Must run before tft.init(). False when no chip
    // answers; the screen then stays dark and the console says why.
    bool begin();

    // 192 or 2101 once begin() has found one, else 0.
    int chip();

    // 0 is off, 255 the brightest this chip can drive. The same convention as
    // every other backlight in main.cpp; 0 cuts the supply, not just the duty.
    void setBacklight(uint8_t duty);

    // The cell's charge and whether a cable is in. False while no cell is
    // fitted. Reads the chip at most every two seconds.
    bool battery(uint8_t& pct, bool& charging);

    // The cell's voltage in millivolts, 0 when unknown.
    uint16_t batteryMv();

    // The speaker amplifier's enable: AXP192 GPIO2, AXP2101 ALDO3. Off at boot.
    void speaker(bool on);

    // Feeds the M-Bus 5 V pin for a stacked module (GNSS). AXP192 verified; AXP2101 untried.
    void busPower(bool on);

    // The clock chip (a BM8563, register-compatible with the PCF8563), on the
    // same bus with its own backup cell. Kept in UTC. rtcBegin() runs once after
    // Clock::begin(): the chip's time, when it has one worth believing, sets the
    // clock; from then on every time the clock learns the real time it is written
    // back to the chip.
    void rtcBegin();

    // The motion sensor (an MPU6886). imuBegin() wakes it; shaken() is true once
    // per shake, polled from the loop at most twenty times a second.
    bool imuBegin();
    bool shaken(uint32_t now);

    // The vibration motor: AXP192 LDO3, AXP2101 DLDO1, voltage-driven. A pattern
    // is played from tick() so nothing here ever blocks the loop. `level` is
    // Settings::buzzStrength(): 0 LOW, 1 MED, 2 HIGH.
    enum class Pattern : uint8_t { ALERT, WATCH, MESSAGE };
    void vibrate(Pattern p, uint8_t level);
    void tick(uint32_t now);   // call every loop pass
}
