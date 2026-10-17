// SquachWatch-CYD - CYD40 board audio hooks.
#include "board_audio.h"
#include "settings.h"
#include <Arduino.h>

namespace BoardAudio {

#if defined(CYD40)
static constexpr int AMP_ENABLE_PIN = 4;   // FM8002E SHUTDOWN, active-low
static constexpr int AUDIO_DAC_PIN  = 26;  // ESP32 DAC2
static constexpr uint8_t AUDIO_TONE_CH = 6;
#endif

bool available() {
#if defined(CYD40)
    return true;
#else
    return false;
#endif
}

void begin() {
#if defined(CYD40)
    pinMode(AMP_ENABLE_PIN, OUTPUT);
    digitalWrite(AMP_ENABLE_PIN, HIGH); // amp off / silent
#endif
}

void enable(bool on) {
#if defined(CYD40)
    digitalWrite(AMP_ENABLE_PIN, on ? LOW : HIGH);
#else
    (void)on;
#endif
}

void writeSample(uint8_t sample) {
#if defined(CYD40)
    dacWrite(AUDIO_DAC_PIN, sample);
#else
    (void)sample;
#endif
}


static bool s_toneOn = false;
static uint16_t s_toneHz = 0;

void toneStart(uint16_t hz) {
#if defined(CYD40)
    s_toneHz = hz ? hz : 1400;
    s_toneOn = true;
    digitalWrite(AMP_ENABLE_PIN, LOW);   // FM8002E enabled LOW
#else
    (void)hz;
#endif
}

void toneStop() {
#if defined(CYD40)
    s_toneOn = false;
    dacWrite(AUDIO_DAC_PIN, 128);
    digitalWrite(AMP_ENABLE_PIN, HIGH);  // amp shutdown
#endif
}

static uint8_t volumeSwing() {
#if defined(CYD40)
    static const uint8_t SWING[] = { 18, 34, 58, 92 };  // LOW..MAX
    const uint8_t pct = Settings::soundVolume(); // upstream uses 0..100
    const uint8_t ix = pct < 26 ? 0 : (pct < 51 ? 1 : (pct < 76 ? 2 : 3));
    return SWING[ix];
#else
    return 0;
#endif
}

static void dacBeepMs(uint16_t ms, uint16_t hz) {
#if defined(CYD40)
    if (Settings::soundVolume() == 0) return;
    if (hz < 200) hz = 200;
    const uint8_t swing = volumeSwing();
    const uint8_t hi = (uint8_t)(128 + swing);
    const uint8_t lo = (uint8_t)(128 - swing);
    const uint32_t halfUs = 500000UL / hz;
    const uint32_t cycles = ((uint32_t)ms * hz) / 1000UL;

    digitalWrite(AMP_ENABLE_PIN, LOW);
    delay(2); // let the amp come out of shutdown cleanly
    for (uint32_t i = 0; i < cycles; ++i) {
        dacWrite(AUDIO_DAC_PIN, hi);
        delayMicroseconds(halfUs);
        dacWrite(AUDIO_DAC_PIN, lo);
        delayMicroseconds(halfUs);
    }
    dacWrite(AUDIO_DAC_PIN, 128);
    digitalWrite(AMP_ENABLE_PIN, HIGH);
#else
    (void)ms; (void)hz;
#endif
}

void testBeep() {
#if defined(CYD40)
    dacBeepMs(220, 1000);
#endif
}

void alertBeep() {
#if defined(CYD40)
    // Use the exact same direct-DAC path as the volume preview.
    // That path is already confirmed audible on the CYD40 hardware.
    Serial.println("[audio] alertBeep -> proven testBeep path");
    testBeep();
#endif
}
}