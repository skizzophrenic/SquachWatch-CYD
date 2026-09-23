#if defined(CROWPANEL7)
#include "crowpanel7_buzzer.h"
#include "crowpanel7_board.h"
#include <Arduino.h>
#include <Wire.h>

namespace CrowBuzzer {

static uint32_t s_offAt = 0;   // millis() at which to stop; 0 = silent

static void send(uint8_t command) {
    Wire.beginTransmission(STC8_ADDR);
    Wire.write(command);
    Wire.endTransmission();
}

void chirp(uint16_t ms) {
    if (ms < 20) ms = 20;
    if (ms > 500) ms = 500;
    const uint32_t until = millis() + ms;
    if (!s_offAt) send(246);
    if (until > s_offAt) s_offAt = until;
}

void tick() {
    if (s_offAt && (int32_t)(millis() - s_offAt) >= 0) {
        send(247);
        s_offAt = 0;
    }
}

void quiet() {
    if (s_offAt) { send(247); s_offAt = 0; }
}

bool sounding() { return s_offAt != 0; }

}  // namespace CrowBuzzer
#endif
