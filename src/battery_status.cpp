#include "battery_status.h"
#include <Arduino.h>
namespace BatteryStatus {
namespace {
Reading value = {Level::UNKNOWN, 0, 0, 0};
#if defined(SQW_BATTERY_ADC_PIN)
volatile uint32_t edges = 0;
portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
uint32_t sampledAt = 0;
void IRAM_ATTR pulse() {
    portENTER_CRITICAL_ISR(&lock); ++edges; portEXIT_CRITICAL_ISR(&lock);
}
#endif
}
void begin() {
#if defined(SQW_BATTERY_ADC_PIN)
    analogSetPinAttenuation(SQW_BATTERY_ADC_PIN, ADC_11db);
    analogSetPinAttenuation(SQW_USB_ADC_PIN, ADC_11db);
    pinMode(SQW_CHARGING_PIN, INPUT);
    pinMode(SQW_BATTERY_PULSE_PIN, INPUT_PULLDOWN);
    attachInterrupt(digitalPinToInterrupt(SQW_BATTERY_PULSE_PIN), pulse, RISING);
#endif
}
void tick(uint32_t now) {
#if defined(SQW_BATTERY_ADC_PIN)
    if (now - sampledAt < 1000) return;
    sampledAt = now;
    uint32_t battery = 0, usb = 0;
    for (unsigned i = 0; i < 8; ++i) {
        battery += analogReadMilliVolts(SQW_BATTERY_ADC_PIN);
        usb += analogReadMilliVolts(SQW_USB_ADC_PIN);
    }
    value.millivolts = battery / 8 * SQW_BATTERY_ADC_NUMERATOR / SQW_BATTERY_ADC_DENOMINATOR;
    value.usbMillivolts = usb / 8;
    portENTER_CRITICAL(&lock); value.pulses = edges; edges = 0; portEXIT_CRITICAL(&lock);
    // GPIO21 pulses identify an absent module in stock firmware. Do not
    // turn a missing/invalid battery reading into a fictitious full battery.
    value.level = classify(value.millivolts, value.usbMillivolts,
                           digitalRead(SQW_CHARGING_PIN) == LOW, value.pulses, value.level);
#endif
}
Reading reading() { return value; }
}
