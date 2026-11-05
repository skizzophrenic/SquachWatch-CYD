// SquachWatch-CYD -- the M5Stack Tough's power chip. See include/m5tough_board.h.
#include "m5tough_board.h"

#if defined(M5TOUGH)
#include <Arduino.h>
#include <Wire.h>

namespace M5Tough {

static const uint8_t AXP = 0x34;
static bool s_ok = false;

static bool rd(uint8_t reg, uint8_t& v) {
    Wire.beginTransmission(AXP);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((uint8_t)AXP, (uint8_t)1) != 1) return false;
    v = Wire.read();
    return true;
}

static bool wr(uint8_t reg, uint8_t v) {
    Wire.beginTransmission(AXP);
    Wire.write(reg);
    Wire.write(v);
    return Wire.endTransmission() == 0;
}

// (old & keep) | set: the {register, data, mask} triples M5GFX's autodetect is
// written in. `keep` is the bits of the old value that survive.
static bool rmw(uint8_t reg, uint8_t set, uint8_t keep) {
    uint8_t v = 0;
    if (!rd(reg, v)) return false;
    return wr(reg, (uint8_t)((v & keep) | set));
}

bool powerUp() {
    // The card's chip select, high, before anything talks on the shared bus:
    // the display's first transfers would otherwise reach a card whose CS is
    // floating. (sd_log.cpp takes it over later.)
    pinMode(4, OUTPUT);
    digitalWrite(4, HIGH);

    Wire.begin(21, 22, 400000);
    uint8_t id = 0;
    s_ok = rd(0x03, id) && id == 0x03;      // AXP192's chip ID
    Serial.printf("[tough] AXP192 %s (reg 0x03 = 0x%02X)\n",
                  s_ok ? "answered" : "DID NOT ANSWER -- no screen power", id);

    // 1. GPIO4 as the panel's reset output, LDO2 to 3.3 V and on (LCD logic
    //    and the SD slot), GPIO1 as an open-drain output (the touch chip's reset).
    rmw(0x95, 0x84, 0x72);
    rmw(0x28, 0xF0, 0xFF);
    rmw(0x12, 0x04, 0xFF);
    rmw(0x92, 0x00, 0xF8);
    // 2. Both resets released, then one clean pulse on both: the panel
    //    answers 5 ms after release, and TFT_eSPI's own software reset
    //    follows in tft.init().
    rmw(0x96, 0x02, 0xFF);
    rmw(0x94, 0x02, 0xFF);
    delay(5);
    rmw(0x96, 0x00, 0xFD);
    rmw(0x94, 0x00, 0xFD);
    delay(10);
    rmw(0x96, 0x02, 0xFF);
    rmw(0x94, 0x02, 0xFF);
    delay(10);

    // 3. What M5Unified parks on this board: the speaker amp off (GPIO2 low),
    //    DC-DC3 off (the Core2's backlight rail; here LDO3 is the backlight).
    wr(0x93, 0x05);
    rmw(0x12, 0x00, 0xFD);
    return s_ok;
}

bool pmuOk() { return s_ok; }

void backlight(uint8_t duty) {
    if (!s_ok) return;
    static int last = -1;                    // the screen asks every frame
    if (duty == last) return;
    last = duty;
    if (duty) {
        // LDO3 = 1.8 V + 0.1 V * N drives the backlight directly and does not
        // light below 2.5 V (N = 7): map 1..255 onto N = 7..15, as M5GFX does.
        const uint8_t n = (uint8_t)(7 + ((uint16_t)duty * 9) / 256);
        rmw(0x28, n, 0xF0);                  // the voltage first, then on
        rmw(0x12, 0x08, 0xFF);
    } else {
        rmw(0x12, 0x00, 0xF7);               // LDO3 off
    }
}

// M5Unified's setGPIO2(): 0x05 drives the pin low (amp off), 0x06 lets it float
// (amp on).
void speaker(bool on) {
    if (!s_ok) return;
    wr(0x93, on ? 0x06 : 0x05);
}

int batteryMv() {
    if (!s_ok) return 0;
    Wire.beginTransmission(AXP);
    Wire.write(0x78);
    if (Wire.endTransmission(false) != 0) return 0;
    if (Wire.requestFrom((uint8_t)AXP, (uint8_t)2) != 2) return 0;
    const uint8_t hi = Wire.read(), lo = Wire.read();
    const uint16_t raw = (uint16_t)((hi << 4) | (lo & 0x0F));   // 12 bits, 1.1 mV a step
    return (int)(raw * 11 / 10);
}

bool batteryState(uint8_t& pct, bool& charging) {
    if (!s_ok) return false;
    static uint32_t at = 0;
    static int      p = -1;
    static bool     chg = false, present = false;
    const uint32_t now = millis();
    if (!at || now - at >= 2000) {           // a few I2C reads, every two seconds
        at = now ? now : 1;
        uint8_t st0 = 0, st1 = 0;
        if (rd(0x00, st0) && rd(0x01, st1)) {
            present = st1 & 0x20;            // battery present
            chg     = (st0 & 0x20) || (st0 & 0x04);   // cable in (VBUS) or charging
            const int mv = batteryMv();
            // M5Unified's own curve for this chip (Power_Class::getBatteryLevel).
            int level = (int)((mv - 3300) * 100.0f / (4150 - 3350));
            p = level < 0 ? 0 : (level > 100 ? 100 : level);
        } else {
            present = false;
        }
    }
    if (!present || p < 0) return false;
    pct = (uint8_t)p;
    charging = chg;
    return true;
}

}  // namespace M5Tough

#endif  // M5TOUGH
