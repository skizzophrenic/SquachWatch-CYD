// SquachWatch-CYD -- the M5Stack Tough's power chip.
//
// The Tough has no backlight pin and no reset pin for its panel: an AXP192
// on I2C (SDA 21, SCL 22, 0x34) owns the screen's 3.3 V rail (LDO2), its reset
// line (GPIO4) and the touch controller's (GPIO1), and the backlight is LDO3 --
// a voltage, 2.5-3.3 V, that is the brightness. Nothing on the panel answers
// until powerUp() has run, so it is the first thing setup() does.
//
// Register sequences are M5GFX's own for this board (M5GFX.cpp, the Core2/Tough
// autodetect and Light_M5Tough) and M5Unified's (Power_Class.inl), not guesses.
#pragma once
#include <stdint.h>

#if defined(M5TOUGH)

namespace M5Tough {
    // Starts the I2C bus (400 kHz), checks for the AXP192, switches the LCD
    // rail on, pulses the panel's and the touch chip's reset, parks the
    // speaker amp, vibration motor and unused DC-DC off, and drives the SD
    // card's chip select high so it cannot hear the display's traffic.
    // Returns false if the AXP192 did not answer; the rest is still tried,
    // since a board that refuses is better told than silent.
    bool powerUp();

    // True once the AXP192 answered in powerUp().
    bool pmuOk();

    // 0..255 as the rest of the firmware spells brightness; 0 is off. Mapped
    // onto LDO3's 16 steps the way M5GFX does, so every non-zero value lights
    // the panel (below 2.5 V it does not).
    void backlight(uint8_t duty);

    // The NS4168 speaker amp's enable (AXP192 GPIO2). powerUp() parks it off;
    // Sound::begin() turns it on once, from setup(), and leaves it.
    void speaker(bool on);

    // Millivolts at the battery pin, or 0 with no answer.
    int batteryMv();

    // uiClearSetBatterySource()'s shape: percent, and whether the cable is
    // in. False while no cell is plugged in. M5Unified's own curve.
    bool batteryState(uint8_t& pct, bool& charging);
}

#endif  // M5TOUGH
