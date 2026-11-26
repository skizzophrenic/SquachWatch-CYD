// SquachWatch-CYD - CYD40 board audio hooks.
#pragma once
#include <stdint.h>

namespace BoardAudio {
bool available();
void begin();
void enable(bool on);
void writeSample(uint8_t sample);
void toneStart(uint16_t hz);
void toneStop();
void testBeep();
void alertBeep();
}