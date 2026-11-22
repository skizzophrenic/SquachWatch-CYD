// SquachWatch-CM0 -- Shake (shake.h) on the Cardputer Zero's BMI270, over
// Linux IIO. See shake_iio.h.
//
// What differs from the firmware's shake.cpp, and why:
//  - The chip: the kernel's bmi270 driver owns it, so there is no I2C, no
//    config upload and no FIFO. A thread samples in_accel_{x,y,z}_raw at the
//    chip's own output rate (100 Hz on the stock image; the FIFO and the rate
//    are root-only, the raw values are world-readable) and queues
//    (millis(), |a| mg) pairs. A 5-50 ms knock is still one to five samples.
//  - The detector is NOT re-tuned: the constants, feed() and the end of
//    poll() below are verbatim copies of the firmware's, between BEGIN/END
//    markers that scripts/shake_sync_check.sh diffs on every `make test`.
//  - "Dimmed" is this board's backlight dimmer (dimmer.h), not the
//    firmware's saver: while it is dimmed a SHAKE or a WIGGLE only wakes it
//    and nothing else happens (the firmware's own rule for its dimmed
//    screen), a TAP is ignored; awake, a SHAKE or TAP also restarts the
//    dimmer's idle clock, as it restarts the firmware's (lastTouch).
//  - Virtual time (--shot, --selftest): no IMU, as in the emulator.
#include "shake.h"
#include "shake_iio.h"
#include "dimmer.h"

#include <Arduino.h>   // Serial, millis(): the firmware's clock

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <mutex>
#include <string>
#include <thread>
#include <time.h>
#include <unistd.h>
#include <vector>

namespace {

struct Sample { uint32_t t; int32_t mg; };

std::string        s_dirOverride;
std::mutex         s_qLock;
std::vector<Sample> s_q;               // reader thread -> poll()
std::thread        s_thread;
std::atomic<bool>  s_run{false};
std::atomic<uint32_t> s_samples{0};
uint32_t           s_startMs = 0;      // monoMs() at begin(), for the rate at close
const size_t       Q_MAX = 400;        // 4 s at 100 Hz: a stalled loop's backlog

void push(uint32_t t, int32_t mg) {
    std::lock_guard<std::mutex> g(s_qLock);
    if (s_q.size() >= Q_MAX) s_q.clear();   // poll() was away: too old to mean anything
    s_q.push_back({t, mg});
}

// The dimmer's clock (main_linux.cpp's monoMs), not millis().
uint32_t monoMs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}

bool readNum(const std::string& path, double& out) {
    FILE* f = fopen(path.c_str(), "re");
    if (!f) return false;
    const bool ok = fscanf(f, "%lf", &out) == 1;
    fclose(f);
    return ok;
}

bool readRaw(int fd, long& v) {
    char b[24];
    const ssize_t n = pread(fd, b, sizeof b - 1, 0);   // sysfs: offset 0 re-reads the chip
    if (n <= 0) return false;
    b[n] = 0;
    char* end = nullptr;
    v = strtol(b, &end, 10);
    return end != b;
}

// The bmi270 under /sys/bus/iio/devices, by name.
std::string findDevice() {
    const char* base = "/sys/bus/iio/devices";
    std::string found;
    if (DIR* d = opendir(base)) {
        while (dirent* e = readdir(d)) {
            if (strncmp(e->d_name, "iio:device", 10) != 0) continue;
            const std::string dir = std::string(base) + "/" + e->d_name;
            char name[32] = {0};
            if (FILE* f = fopen((dir + "/name").c_str(), "re")) {
                if (fgets(name, sizeof name, f)) name[strcspn(name, "\n")] = 0;
                fclose(f);
            }
            if (strcmp(name, "bmi270") == 0) { found = dir; break; }
        }
        closedir(d);
    }
    return found;
}

void reader(int fx, int fy, int fz, double mgPerLsb, long periodNs) {
    struct timespec next;
    clock_gettime(CLOCK_MONOTONIC, &next);
    while (s_run.load(std::memory_order_relaxed)) {
        long x, y, z;
        if (readRaw(fx, x) && readRaw(fy, y) && readRaw(fz, z)) {
            const double mag = std::sqrt((double)x * x + (double)y * y + (double)z * z) * mgPerLsb;
            push(millis(), (int32_t)mag);
            s_samples.fetch_add(1, std::memory_order_relaxed);
        }
        next.tv_nsec += periodNs;
        while (next.tv_nsec >= 1000000000L) { next.tv_nsec -= 1000000000L; next.tv_sec++; }
        // Fell behind (a stalled CPU): start again from now rather than burst.
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (now.tv_sec > next.tv_sec + 1) next = now;
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, nullptr);
    }
    close(fx); close(fy); close(fz);
}

}   // namespace

namespace Shake {
namespace {

// ---- BEGIN upstream shake.cpp: tuning (verbatim; scripts/shake_sync_check.sh)
const int32_t  EXC_MG              = 500;
const int32_t  TAP_PEAK_MG         = 1500;
const uint32_t TAP_MAX_MS          = 80;    // a longer excursion is a hand moving it
const uint8_t  TAP_MAX_SPIKES      = 3;
const uint32_t TAP_QUIET_MS        = 300;
const int32_t  SHAKE_PEAK_MG       = 900;
// A WIGGLE counts every excursion: 4 of them in the shake window. From the
// log, a person waking the fob jiggles it at 0.5 to 0.8 g for a second or two
// before ever reaching a real shake, and waited that long for the screen.
const uint8_t  WIGGLE_EXCURSIONS   = 4;
const uint32_t WIGGLE_REPEAT_MS    = 1000;
const uint8_t  SHAKE_EXCURSIONS    = 4;
const uint32_t SHAKE_WINDOW_MS     = 800;
const uint32_t COOLDOWN_MS         = 1200;
// ---- END upstream shake.cpp: tuning

// The firmware's detector state, by the firmware's names (feed() below).
bool     s_ok = false, s_trace = false;
uint32_t s_traceAt = 0, s_traceN = 0;   // the trace's once-a-second heartbeat
int32_t  s_traceMg = 0;
uint32_t s_cooldownUntil = 0;
bool     s_inExc = false;
uint32_t s_excStart = 0;
int32_t  s_excPeak = 0;
uint32_t s_bigAt[8];
uint8_t  s_bigN = 0;
uint32_t s_lastExcEnd = 0;
uint32_t s_anyAt[8];
uint8_t  s_anyN = 0;
uint32_t s_wiggleAt = 0;
uint8_t  s_tapSpikes = 0;
uint32_t s_tapAt = 0;
bool     s_tapPending = false;

// ---- BEGIN upstream shake.cpp: feed (verbatim; scripts/shake_sync_check.sh)
// One sample, at time t (ms). Returns an event if this sample finishes one.
Event feed(uint32_t t, int32_t devMg) {
    const int32_t a = devMg < 0 ? -devMg : devMg;
    Event ev = Event::NONE;
    if (a > EXC_MG) {
        if (!s_inExc) { s_inExc = true; s_excStart = t; s_excPeak = 0; }
        if (a > s_excPeak) s_excPeak = a;
        return ev;
    }
    if (s_inExc) {
        // An excursion just ended.
        s_inExc = false;
        const uint32_t len = t - s_excStart;
        if (s_trace) Serial.printf("[imu] excursion %lu ms, peak %d mg\n", (unsigned long)len, (int)s_excPeak);
        {
            uint8_t k = 0;
            for (uint8_t i = 0; i < s_anyN; i++) if (t - s_anyAt[i] <= SHAKE_WINDOW_MS) s_anyAt[k++] = s_anyAt[i];
            s_anyN = k;
            if (s_anyN < 8) s_anyAt[s_anyN++] = s_excStart;
        }
        if (s_excPeak >= SHAKE_PEAK_MG) {
            uint8_t k = 0;
            for (uint8_t i = 0; i < s_bigN; i++) if (t - s_bigAt[i] <= SHAKE_WINDOW_MS) s_bigAt[k++] = s_bigAt[i];
            s_bigN = k;
            if (s_bigN < 8) s_bigAt[s_bigN++] = s_excStart;
            if (s_bigN >= SHAKE_EXCURSIONS) {
                s_bigN = 0;
                s_tapPending = false;
                ev = Event::SHAKE;
            }
        }
        if (ev == Event::NONE) {
            if (len > TAP_MAX_MS) {
                s_tapPending = false;              // a hand moving it, not a knock
            } else if (s_excPeak >= TAP_PEAK_MG) {
                if (!s_tapPending) { s_tapPending = true; s_tapSpikes = 0; }
                s_tapSpikes++;
                s_tapAt = t;
            }
        }
        s_lastExcEnd = t;
    }
    if (ev == Event::NONE && s_tapPending && t - s_lastExcEnd >= TAP_QUIET_MS) {
        s_tapPending = false;
        if (s_tapSpikes <= TAP_MAX_SPIKES) ev = Event::TAP;
    }
    // A wiggle does not swallow a tap (a knock rings into several small
    // excursions): it is its own event, at most once a second.
    if (ev == Event::NONE && s_anyN >= WIGGLE_EXCURSIONS && t - s_wiggleAt >= WIGGLE_REPEAT_MS) {
        s_wiggleAt = t;
        s_anyN = 0;
        ev = Event::WIGGLE;
    }
    return ev;
}
// ---- END upstream shake.cpp: feed

// The end of the firmware's poll(), after its FIFO walk.
Event finish(uint32_t now, Event out) {
// ---- BEGIN upstream shake.cpp: poll tail (verbatim; scripts/shake_sync_check.sh)
    if (s_trace && now - s_traceAt >= 1000) {
        Serial.printf("[imu] %lu samples/s, |a| %d mg\n", (unsigned long)s_traceN, (int)s_traceMg);
        s_traceAt = now; s_traceN = 0;
    }
    // A tap still waiting for quiet when the samples stop: let time pass.
    if (out == Event::NONE && s_tapPending && !s_inExc && now - s_lastExcEnd >= TAP_QUIET_MS) {
        s_tapPending = false;
        if (s_tapSpikes <= TAP_MAX_SPIKES) out = Event::TAP;
    }
    if (out == Event::WIGGLE) return out;     // no cooldown: it only ever wakes
    if (out != Event::NONE) {
        if ((int32_t)(now - s_cooldownUntil) < 0) return Event::NONE;
        s_cooldownUntil = now + (out == Event::SHAKE ? COOLDOWN_MS : 300);
        Serial.println(out == Event::SHAKE ? "[imu] SHAKE" : "[imu] TAP");
    }
    return out;
// ---- END upstream shake.cpp: poll tail
}

// This board's dimmer in the firmware's dimmed-screen rule (see the top).
Event gate(Event ev) {
    if (ev == Event::NONE) return ev;
    if (cm0::dimmed()) {
        if (ev == Event::SHAKE || ev == Event::WIGGLE) {
            cm0::dimmerWake(monoMs());
            Serial.println(ev == Event::SHAKE ? "[imu] wake" : "[imu] wake (wiggle)");
        }
        return Event::NONE;
    }
    if (ev == Event::SHAKE || ev == Event::TAP) cm0::dimmerWake(monoMs());
    return ev;
}

}   // namespace

bool begin() {
    if (s_run) return s_ok;
    s_ok = false;
    if (SimClock::virtualTime) {
        Serial.println("[imu] virtual time: no shake or tap");
        return false;
    }
    const std::string dir = s_dirOverride.empty() ? findDevice() : s_dirOverride;
    if (dir.empty()) { Serial.println("[imu] no bmi270 under /sys/bus/iio: no shake or tap"); return false; }
    double scale = 0, hz = 0;
    if (!readNum(dir + "/in_accel_scale", scale) || scale <= 0) {
        Serial.printf("[imu] %s: no in_accel_scale: no shake or tap\n", dir.c_str());
        return false;
    }
    if (!readNum(dir + "/in_accel_sampling_frequency", hz) || hz < 25) hz = 100;
    if (hz > 400) hz = 400;
    const int fx = open((dir + "/in_accel_x_raw").c_str(), O_RDONLY | O_CLOEXEC);
    const int fy = open((dir + "/in_accel_y_raw").c_str(), O_RDONLY | O_CLOEXEC);
    const int fz = open((dir + "/in_accel_z_raw").c_str(), O_RDONLY | O_CLOEXEC);
    long probe = 0;
    if (fx < 0 || fy < 0 || fz < 0 || !readRaw(fz, probe)) {
        Serial.printf("[imu] %s: accelerometer not readable: no shake or tap\n", dir.c_str());
        if (fx >= 0) close(fx);
        if (fy >= 0) close(fy);
        if (fz >= 0) close(fz);
        return false;
    }
    const double mgPerLsb = scale / 9.80665 * 1000.0;   // in_accel_scale is m/s^2 a count
    { std::lock_guard<std::mutex> g(s_qLock); s_q.clear(); }
    s_samples = 0;
    s_startMs = monoMs();
    s_run = true;
    s_thread = std::thread(reader, fx, fy, fz, mgPerLsb, (long)(1e9 / hz));
    s_ok = true;
    Serial.printf("[imu] BMI270 over IIO (%s): %.0f Hz, %.3f mg a count -- shake and tap are on\n",
                  dir.c_str(), hz, mgPerLsb);
    return true;
}

bool present() { return s_ok; }
void setTrace(bool on) { s_trace = on; }

Event poll(uint32_t now) {
    if (!s_ok) return Event::NONE;
    std::vector<Sample> batch;
    {
        std::lock_guard<std::mutex> g(s_qLock);
        batch.swap(s_q);
    }
    Event out = Event::NONE;
    for (const Sample& s : batch) {
        s_traceN++; s_traceMg = s.mg;
        const Event e = feed(s.t, s.mg - 1000);
        if (e != Event::NONE && out == Event::NONE) out = e;
    }
    return gate(finish(now, out));
}

}   // namespace Shake

namespace cm0 {

void shakeSetDir(const char* dir) { s_dirOverride = dir ? dir : ""; }

void shakeClose() {
    if (!s_run) return;
    s_run = false;
    if (s_thread.joinable()) s_thread.join();
    Shake::s_ok = false;
    const uint32_t ms = monoMs() - s_startMs;
    std::fprintf(stderr, "[imu] down (%u samples in %u s, %.0f a second)\n", (unsigned)s_samples.load(),
                 (unsigned)(ms / 1000), ms ? s_samples.load() * 1000.0 / ms : 0.0);
}

void shakeInject(uint32_t tMs, int32_t magMg) { push(tMs, magMg); }

uint32_t shakeSamples() { return s_samples; }

}   // namespace cm0
