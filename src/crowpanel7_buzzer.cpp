#if defined(CROWPANEL7)
#include "crowpanel7_buzzer.h"
#include "crowpanel7_board.h"
#include <Arduino.h>
#include <Wire.h>

namespace CrowBuzzer {

// A flag rather than a zero-means-silent timestamp, so the once-in-49-days
// pass where millis() + ms wraps to exactly 0 needs no thought.
static bool     s_on      = false;   // ON acknowledged and no OFF since: sounding
static uint32_t s_offAt   = 0;       // while s_on: millis() at which the OFF goes out
// OFFs the helper has not acknowledged in a row. The bus is shared with the
// touch controller and one NAK is one lost write, not a dead helper, so the
// OFF is sent again from tick() every pass: a buzzer left sounding is the
// one failure this module can produce that is worse than having no buzzer.
// Bounded, so a helper that really has gone away costs one console line
// rather than a write per frame for ever.
static uint8_t  s_offNaks = 0;
static const uint8_t OFF_NAKS_MAX = 8;

// One command byte to the helper, no register address. True on ack.
static bool send(uint8_t command) {
    Wire.beginTransmission(STC8_ADDR);
    Wire.write(command);
    return Wire.endTransmission() == 0;
}

// The OFF, and what to do when the helper does not take it.
static void off() {
    if (send(STC8_BUZZ_OFF)) { s_on = false; s_offNaks = 0; return; }
    if (++s_offNaks < OFF_NAKS_MAX) return;        // tick() sends it again next pass
    Serial.printf("[buzz] off NAK x%u\n", (unsigned)s_offNaks);
    s_on = false;
    s_offNaks = 0;
}

void begin() {
    // If the helper takes the OFF the buzzer is silent, whatever a crash
    // left it doing. If it does not, assume the worst -- a buzzer that may
    // be sounding -- and let tick() keep asking, bounded like any other OFF.
    s_offNaks = 0;
    s_on      = !send(STC8_BUZZ_OFF);
    s_offAt   = millis();
}

void chirp(uint16_t ms) {
    if (ms < 20)  ms = 20;
    if (ms > 500) ms = 500;
    const uint32_t until = millis() + ms;
    if (!s_on) {
        if (!send(STC8_BUZZ_ON)) return;           // not sounding, so nothing to time
        s_on    = true;
        s_offAt = until;
        return;
    }
    if ((int32_t)(until - s_offAt) > 0) s_offAt = until;   // extended, not doubled
}

void tick() {
    if (s_on && (int32_t)(millis() - s_offAt) >= 0) off();
}

void quiet() {
    if (!s_on) return;
    s_offAt = millis();    // due now, so a NAK here is retried by tick() at once
    off();
}

bool sounding() { return s_on; }

}  // namespace CrowBuzzer
#endif
