// shake_test -- Shake over IIO (shake_iio.cpp) on a fake bmi270 directory
// and a fake backlight: the reader thread samples the raw files, a knock is
// a TAP, four hard swings a SHAKE, a jiggle a WIGGLE (the firmware's own
// detector, verbatim), and with the panel dimmed motion only wakes it.
#include "shake.h"
#include "shake_iio.h"
#include "dimmer.h"

#include <Arduino.h>   // millis()

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <time.h>
#include <unistd.h>

static int g_checks = 0, g_failures = 0;
#define CHECK(cond, what) do { g_checks++; if (cond) std::printf("ok  : %s\n", what); \
    else { g_failures++; std::printf("FAIL: %s\n", what); } } while (0)

static void put(const std::string& dir, const char* f, const char* v) {
    FILE* o = fopen((dir + "/" + f).c_str(), "wb"); fputs(v, o); fclose(o);
}
static int level(const std::string& dir) {
    FILE* i = fopen((dir + "/brightness").c_str(), "rb");
    char b[16] = {0};
    if (i) { size_t n = fread(b, 1, sizeof b - 1, i); b[n] = 0; fclose(i); }
    return atoi(b);
}
static uint32_t monoMs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}
static void sleepMs(int ms) { struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L }; nanosleep(&ts, nullptr); }

// Firmware time for the synthetic samples: far ahead of the real clock,
// 10 ms a sample (the stock 100 Hz).
static uint32_t T = 0;
static void quiet(int ms) { for (int i = 0; i < ms; i += 10) { cm0::shakeInject(T, 1000); T += 10; } }
static void swing(int mg, int ms) { for (int i = 0; i < ms; i += 10) { cm0::shakeInject(T, mg); T += 10; } }

int main() {
    char ftmpl[] = "/tmp/shake-test-iio-XXXXXX";
    char btmpl[] = "/tmp/shake-test-bl-XXXXXX";
    const std::string iio = mkdtemp(ftmpl), bl = mkdtemp(btmpl);

    // No device: present() false, poll() NONE.
    cm0::shakeSetDir((iio + "/missing").c_str());
    CHECK(!Shake::begin(), "no accelerometer files: begin() false");
    CHECK(!Shake::present(), "no accelerometer files: not present");
    CHECK(Shake::poll(millis()) == Shake::Event::NONE, "no accelerometer files: poll() NONE");

    // The device at rest: 1 g on z at the stock scale (0.002394 m/s^2 a count).
    put(iio, "in_accel_scale", "0.002394\n");
    put(iio, "in_accel_sampling_frequency", "100.000000\n");
    put(iio, "in_accel_x_raw", "0\n");
    put(iio, "in_accel_y_raw", "0\n");
    put(iio, "in_accel_z_raw", "4096\n");
    cm0::shakeSetDir(iio.c_str());
    CHECK(Shake::begin(), "fake bmi270: begin() true");
    CHECK(Shake::present(), "fake bmi270: present");
    sleepMs(250);
    const uint32_t n = cm0::shakeSamples();
    std::printf("      (%u samples in 250 ms at 100 Hz)\n", n);
    CHECK(n >= 15 && n <= 35, "reader thread samples at the ODR (100 Hz)");
    CHECK(Shake::poll(millis()) == Shake::Event::NONE, "at rest (1 g): no event");

    // Stop the real samples (an empty raw file is a failed read, skipped),
    // so only the synthetic ones below reach the detector.
    put(iio, "in_accel_x_raw", "");
    sleepMs(50);
    Shake::poll(millis());   // drain what came before
    T = millis() + 100000;

    // A knock: one 20 ms spike to 3 g, then quiet -> TAP after 300 ms.
    quiet(100); swing(3000, 20); quiet(400);
    CHECK(Shake::poll(T) == Shake::Event::TAP, "knock: TAP");

    // A shake: four hard 100 ms swings to 2.5 g inside 800 ms -> SHAKE.
    quiet(1500);
    for (int i = 0; i < 4; i++) { swing(2500, 100); quiet(60); }
    quiet(100);
    CHECK(Shake::poll(T) == Shake::Event::SHAKE, "four hard swings: SHAKE");

    // A jiggle: four light 40 ms excursions to 1.6 g -> WIGGLE.
    quiet(2000);
    Shake::poll(T);
    for (int i = 0; i < 4; i++) { swing(1600, 40); quiet(100); }
    CHECK(Shake::poll(T) == Shake::Event::WIGGLE, "a light jiggle: WIGGLE");

    // A hand moving it slowly: one long excursion is not a knock.
    quiet(2000);
    Shake::poll(T);
    swing(3000, 200); quiet(400);
    CHECK(Shake::poll(T) == Shake::Event::NONE, "one long 200 ms excursion: not a TAP");

    // The dimmed panel: motion only wakes it.
    put(bl, "max_brightness", "100\n");
    put(bl, "brightness", "40\n");
    const uint32_t m0 = monoMs();
    CHECK(cm0::dimmerOpenAt(bl.c_str(), m0), "fake backlight opened");
    cm0::dimmerTick(m0 + cm0::DIM_AFTER_MS + 1);
    CHECK(cm0::dimmed() && level(bl) == 2, "dimmed after the idle time");

    quiet(2000); Shake::poll(T);
    quiet(100); swing(3000, 20); quiet(400);
    CHECK(Shake::poll(T) == Shake::Event::NONE, "dimmed: a knock does nothing");
    CHECK(cm0::dimmed(), "dimmed: a knock does not wake");

    quiet(2000); Shake::poll(T);
    for (int i = 0; i < 4; i++) { swing(1600, 40); quiet(100); }
    CHECK(Shake::poll(T) == Shake::Event::NONE, "dimmed: a jiggle is not passed on");
    CHECK(!cm0::dimmed() && level(bl) == 40, "dimmed: a jiggle wakes the panel (level back)");

    cm0::dimmerTick(monoMs() + cm0::DIM_AFTER_MS + 1);
    CHECK(cm0::dimmed(), "dimmed again");
    quiet(2000); Shake::poll(T);
    for (int i = 0; i < 4; i++) { swing(2500, 100); quiet(60); }
    quiet(100);
    CHECK(Shake::poll(T) == Shake::Event::NONE, "dimmed: a shake is not passed on (no toss)");
    const uint32_t wokeBy = monoMs();   // the idle clock restarted at or before this
    CHECK(!cm0::dimmed(), "dimmed: a shake wakes the panel");

    // Awake: a knock restarts the dimmer's idle clock. Without it, a tick a
    // full idle time after the wake would dim; with it (50 ms later), not.
    sleepMs(50);
    quiet(2000); Shake::poll(T);
    quiet(100); swing(3000, 20); quiet(400);
    CHECK(Shake::poll(T) == Shake::Event::TAP, "awake: a knock is a TAP");
    cm0::dimmerTick(wokeBy + cm0::DIM_AFTER_MS + 1);
    CHECK(!cm0::dimmed(), "awake: the knock restarted the idle clock");

    cm0::dimmerClose();
    cm0::shakeClose();
    CHECK(!Shake::present(), "closed: not present");

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    for (const char* f : { "in_accel_scale", "in_accel_sampling_frequency", "in_accel_x_raw",
                           "in_accel_y_raw", "in_accel_z_raw" })
        unlink((iio + "/" + f).c_str());
    unlink((bl + "/max_brightness").c_str());
    unlink((bl + "/brightness").c_str());
    rmdir(iio.c_str());
    rmdir(bl.c_str());
    return g_failures ? 1 : 0;
}
