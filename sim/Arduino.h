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
#include <string>
#include <vector>

// ---- timing ---------------------------------------------------------
// Wall-clock ms since this process started, so animations (Squachy's
// bob, the matrix rain, glitch timers -- all driven by `now` params
// computed from millis()) actually progress between rendered frames
// instead of every frame reading the same instant.
// Virtual time: when enabled, millis() is driven by the harness instead
// of the wall clock -- one tick per loop() step. That makes the
// interactive emulator deterministic and fast-forwardable (the 3-second
// boot splash is 91 steps, not 3 seconds of waiting), and means a
// recorded sequence of taps replays identically every time. The
// one-shot renderer leaves it off and uses real time.
namespace SimClock {
    inline bool     virtualTime = false;
    inline uint32_t nowMs       = 0;
    // Called after every delay() in virtual time. The touch-calibration
    // render uses it to film the screens that sit on a delay() -- a notice
    // held for a second reads no touch, so nothing else would see it.
    inline void   (*onDelay)()  = nullptr;
}

inline uint32_t millis() {
    if (SimClock::virtualTime) return SimClock::nowMs;
    using namespace std::chrono;
    static const auto t0 = steady_clock::now();
    return (uint32_t)duration_cast<milliseconds>(steady_clock::now() - t0).count();
}
inline uint32_t micros() { return millis() * 1000; }

// delay() advances virtual time rather than sleeping. That matters more
// than it looks: the firmware has real-time busy-waits that would spin
// forever otherwise -- setup()'s 1.2s "hold to reset calibration"
// window, and touch_cal.cpp's press/release waits -- all of which loop
// on millis() with a delay() inside. Advancing here is what lets them
// terminate, and it means the whole 4s startup passes in microseconds
// instead of being waited out.
inline void delay(uint32_t ms) {
    if (SimClock::virtualTime) {
        SimClock::nowMs += ms;
        if (SimClock::onDelay) SimClock::onDelay();
    }
}
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

// ---- GPIO / PWM: inert. There are no pins, and the one thing the
// firmware drives through them (the backlight) has no meaning here.
#define INPUT        0x01
#define OUTPUT       0x03
#define INPUT_PULLUP 0x05
#define HIGH         0x1
#define LOW          0x0
inline void pinMode(uint8_t, uint8_t) {}
inline void digitalWrite(uint8_t, uint8_t) {}
inline int  digitalRead(uint8_t) { return 0; }
inline int  analogRead(uint8_t) { return 0; }
inline void ledcSetup(uint8_t, double, uint8_t) {}
inline void ledcAttachPin(uint8_t, uint8_t) {}
inline void ledcWrite(uint8_t, uint32_t) {}

// Interrupt masking around the firmware's IRAM queues -- single-
// threaded here, so there's nothing to mask.
inline void interrupts() {}
inline void noInterrupts() {}

// Arduino's integer map(), reproduced exactly: pollTouch() maps raw
// touch through it, and the harness inverts that same arithmetic to
// turn a mouse click back into raw driver values.
inline long map(long x, long in_min, long in_max, long out_min, long out_max) {
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}
#ifndef constrain
#define constrain(a, lo, hi) ((a) < (lo) ? (lo) : ((a) > (hi) ? (hi) : (a)))
#endif

// ---- ESP runtime info, as read by the diagnostics screen -------------
// Fixed plausible values; the emulator has no ESP32 heap to report and
// nothing here should read as a real measurement.
struct EspClass {
    uint32_t getFreeHeap()    { return 180000; }
    uint32_t getHeapSize()    { return 327680; }
    uint32_t getPsramSize()   { return 0; }
    uint32_t getFreePsram()   { return 0; }
    const char* getChipModel(){ return "ESP32-SIM"; }
    uint8_t  getChipRevision(){ return 1; }
    uint32_t getCpuFreqMHz()  { return 240; }
};
inline EspClass ESP;
inline bool psramFound() { return false; }

// ---- Serial: the firmware's debug/log channel -------------------------
// Routed to stderr, not stdout, on purpose: the interactive harness
// streams raw binary frames on stdout, and a stray Serial.println()
// mid-frame would corrupt them (the same class of bug that made the
// on-device screenshot protocol unreliable, where SD-driver logging
// spliced ASCII into the pixel payload).
struct SerialShim {
    void begin(unsigned long) {}
    void print(const char* s) { fputs(s, stderr); }
    void print(int v) { fprintf(stderr, "%d", v); }
    void println(const char* s) { fputs(s, stderr); fputc('\n', stderr); }
    void println() { fputc('\n', stderr); }
    void printf(const char* fmt, ...) {
        va_list ap; va_start(ap, fmt);
        vfprintf(stderr, fmt, ap);
        va_end(ap);
    }
    void flush() { fflush(stderr); }
    int  available() { return 0; }   // no PC-side serial input in the sim
    int  read() { return -1; }
    size_t write(const uint8_t*, size_t n) { return n; }
    int  availableForWrite() { return 256; }
};
inline SerialShim Serial;

// ---- Serial1/Serial2: a GPS on a UART, replayed from a file -----------------
// The cyd-gps loop reads Serial2 exactly as on the board. Here begin() loads
// the NMEA file named by SQUACHSIM_NMEA (sim/gps_sample.nmea is one), split
// into one-second bursts at blank lines: burst k becomes readable k seconds
// after begin(), in virtual time or real time, whichever millis() runs on. At
// the end of the file the replay stops, so the fix goes stale as it would if
// the module were unplugged. With no variable or no file, nothing is ever
// available.
#ifndef SERIAL_8N1
#define SERIAL_8N1 0x800001c
#endif
struct HardwareSerial {
    std::string data;                 // the file, blank lines removed
    std::vector<size_t> ends;         // where each burst ends in data
    size_t   pos = 0;
    uint32_t t0 = 0;
    bool     loaded = false;
    void begin(unsigned long baud, uint32_t config = 0, int8_t rx = -1, int8_t tx = -1) {
        t0 = millis();
        pos = 0;
        if (loaded) return;
        loaded = true;
        const char* path = getenv("SQUACHSIM_NMEA");
        FILE* f = path && *path ? fopen(path, "rb") : nullptr;
        if (!f) {
            fprintf(stderr, "[sim] serial %lu baud: %s\n", baud,
                    path && *path ? "SQUACHSIM_NMEA file not found" : "no SQUACHSIM_NMEA");
            return;
        }
        std::string line;
        bool inBurst = false;
        for (int c = fgetc(f); ; c = fgetc(f)) {
            if (c == '\n' || c == EOF) {
                while (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.empty()) {
                    if (inBurst) { ends.push_back(data.size()); inBurst = false; }
                } else {
                    data += line; data += "\r\n"; inBurst = true;
                }
                line.clear();
                if (c == EOF) break;
            } else {
                line += (char)c;
            }
        }
        if (inBurst) ends.push_back(data.size());
        fclose(f);
        fprintf(stderr, "[sim] serial %lu baud: replaying %s, %u bursts\n", baud, path, (unsigned)ends.size());
    }
    void end() {}
    int available() {
        if (ends.empty()) return 0;
        size_t k = (millis() - t0) / 1000 + 1;
        if (k > ends.size()) k = ends.size();
        const size_t upTo = ends[k - 1];
        return upTo > pos ? (int)(upTo - pos) : 0;
    }
    int read() { return available() ? (uint8_t)data[pos++] : -1; }
};
inline HardwareSerial Serial1;
inline HardwareSerial Serial2;

// ESP32 core-clock control. The firmware's POWER SAVER menu calls this; on a
// PC there is nothing to scale, so it records the request and does nothing.
// Kept as a real symbol rather than a #define so squachsim-live still links
// and the setting can be exercised in the emulator.
inline uint32_t g_simCpuMhz = 240;
inline bool setCpuFrequencyMhz(uint32_t mhz) { g_simCpuMhz = mhz; return true; }
inline uint32_t getCpuFrequencyMhz() { return g_simCpuMhz; }
