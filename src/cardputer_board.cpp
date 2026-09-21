#if defined(CARDPUTER)
#include <Arduino.h>
#include <Preferences.h>
#include "cardputer_board.h"
#include "cardputer_runtime.h"
#include "settings.h"
#include "detection.h"

namespace CardputerBoard {
namespace {
constexpr uint8_t BACKLIGHT = 38, BATTERY_ADC = 10, PWM_CHANNEL = 0;
Preferences prefs;
uint32_t lastInput = 0, lastBattery = 0;
uint16_t battery = 0;
uint8_t mode = 2, duty = 255;
bool dim = false;
}
void begin() {
    prefs.begin("cardputer", false);
    mode = prefs.getUChar("scan", 2);
    if (mode > 2) mode = 2;
    setScanPin(mode);
    ledcSetup(PWM_CHANNEL, 5000, 8);
    ledcAttachPin(BACKLIGHT, PWM_CHANNEL);
    duty = Settings::brightness();
    ledcWrite(PWM_CHANNEL, duty);
    analogReadResolution(12);
    analogSetPinAttenuation(BATTERY_ADC, ADC_11db);
    lastInput = millis();
    lastBattery = lastInput - 5000;
}
bool input(uint32_t now) {
    const bool wasDim = dim;
    lastInput = now;
    dim = false;
    return wasDim;
}
void alert(uint32_t now) { if (Settings::wakeOnAlert()) input(now); }
bool dimmed() { return dim; }
void tick(uint32_t now) {
    dim = Cardputer::dimDue(now, lastInput, Settings::screenTimeoutSec());
    const uint8_t wanted = dim ? min(Settings::brightness(), Settings::dimLevel()) : Settings::brightness();
    if (wanted != duty) { duty = wanted; ledcWrite(PWM_CHANNEL, duty); }
    if (uint32_t(now - lastBattery) >= 5000) {
        lastBattery = now;
        // Original Cardputer schematic: GPIO10, equal 100k divider R8/R9.
        // Average calibrated ADC millivolts; report voltage, not an invented
        // charging flag or fuel-gauge percentage. USB can bias the reading.
        uint32_t total = 0;
        for (unsigned i = 0; i < 8; ++i) total += analogReadMilliVolts(BATTERY_ADC);
        const uint32_t mv = total / 4;
        battery = mv >= 2500 && mv <= 4500 ? mv : 0;
    }
}
uint16_t batteryMillivolts() { return battery; }
uint8_t scanMode() { return mode; }
const char* scanModeName() { return mode == 0 ? "AUTO" : mode == 1 ? "ACTIVE" : "PASSIVE"; }
void cycleScanMode() {
    mode = (mode + 1) % 3;
    prefs.putUChar("scan", mode);
    setScanPin(mode);
}
}
#endif
