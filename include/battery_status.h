#pragma once
#include <stdint.h>
namespace BatteryStatus {
enum class Level : uint8_t { UNKNOWN, LOW_BATTERY, MID, FULL, CHARGING };
struct Reading { Level level; uint16_t millivolts, usbMillivolts; uint32_t pulses; };
// Coarse voltage bands derived from the stock linear 3000..4200 mV scale.
// Small hysteresis prevents radio-load/ADC noise from flickering the icon.
inline Level classify(uint16_t mv, uint16_t usbMv, bool chargeLow,
                      uint32_t pulses, Level previous) {
    if (pulses >= 6 || mv < 2500 || mv > 4500) return Level::UNKNOWN;
    if (usbMv > 1300 && chargeLow) return Level::CHARGING;
    if (mv <= 3240 || (previous == Level::LOW_BATTERY && mv < 3300)) return Level::LOW_BATTERY;
    if (mv >= 3960 || (previous == Level::FULL && mv > 3900)) return Level::FULL;
    return Level::MID;
}
void begin();
void tick(uint32_t now);
Reading reading();
}
