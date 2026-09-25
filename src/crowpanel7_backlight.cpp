#if defined(CROWPANEL7)
#include "crowpanel7_backlight.h"
#include "crowpanel7_board.h"
#include <Arduino.h>
#include <Wire.h>

namespace CrowBL {

static bool s_present = false;

static bool send(uint8_t command) {
    if (!s_present) return false;
    Wire.beginTransmission(STC8_ADDR);
    Wire.write(command);
    return Wire.endTransmission() == 0;
}

bool begin() {
    Wire.beginTransmission(STC8_ADDR);
    s_present = (Wire.endTransmission() == 0);
    return s_present;
}

bool present() { return s_present; }

void set(uint8_t level) {
    if (level == 0) { send(STC8_BL_OFF); return; }
    send((uint8_t)(STC8_BL_DIMMEST - ((uint32_t)level * STC8_BL_DIMMEST) / 255));
}

}  // namespace CrowBL
#endif
