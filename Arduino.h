// SquachWatch-CYD PC emulator — minimal Arduino compatibility shim.
// Picked up via -I (this directory comes before the real toolchain
// includes) so `#include <Arduino.h>` resolves here instead of failing
// to find an ESP32 core that doesn't exist on this machine. Covers only
// what the *rendering* path (theme.cpp, squachy.cpp, settings.cpp,
// ui_clear.cpp, detection_info.cpp) actually calls -- see
// tools/sim/README.md for the full list of what's deliberately not
// emulated (WiFi/BLE/SD -- see detection_sim.cpp instead).
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <chrono>

// ---- timing ---------------------------------------------------------
// Wall-clock ms since this process started, so animations (Squachy's
// bob, the matrix rain, glitch timers -- all driven by `now` params
// computed from millis()) actually progress between rendered frames
// instead of every frame reading the same instant.
inline uint32_t millis() {
    using namespace std::chrono;
    static const auto t0 = steady_clock::now();
    return (uint32_t)duration_cast<milliseconds>(steady_clock::now() - t0).count();
}
inline uint32_t micros() { return millis() * 1000; }
inline void delay(uint32_t) {}
inline void yield() {}

// ---- randomness -------------------------------------------------------
// Arduino's random(max) and random(min,max) -- real firmware seeds this
// from an ADC noise source; deterministic seeding here is fine for a
// dev-preview tool and makes a reproducible frame reproducible.
inline long random(long howbig) { return howbig <= 0 ? 0 : ::rand() % howbig; }
inline long random(long howsmall, long howbig) {
    return howbig <= howsmall ? howsmall : howsmall + (::rand() % (howbig - howsmall));
}
inline void randomSeed(unsigned long s) { ::srand((unsigned)s); }

// ---- attributes that only mean something on real hardware ------------
#define IRAM_ATTR
#define PROGMEM
#ifndef PI
#define PI 3.1415926535897932384626433832795
#endif
#ifndef TWO_PI
#define TWO_PI 6.283185307179586476925286766559
#endif
#ifndef DEG_TO_RAD
#define DEG_TO_RAD 0.017453292519943295769236907684886
#endif
#define pgm_read_byte(addr) (*(const unsigned char*)(addr))
#define pgm_read_word(addr) (*(const unsigned short*)(addr))

// ---- Serial: real firmware's debug/log channel, stdout here ----------
struct SerialShim {
    void begin(unsigned long) {}
    void print(const char* s) { fputs(s, stdout); }
    void print(int v) { printf("%d", v); }
    void println(const char* s) { fputs(s, stdout); fputc('\n', stdout); }
    void println() { fputc('\n', stdout); }
    void printf(const char* fmt, ...) {
        va_list ap; va_start(ap, fmt);
        vprintf(fmt, ap);
        va_end(ap);
    }
    void flush() { fflush(stdout); }
    int  available() { return 0; }   // no PC-side serial input in the sim
    int  read() { return -1; }
    size_t write(const uint8_t*, size_t n) { return n; }
    int  availableForWrite() { return 256; }
};
inline SerialShim Serial;
