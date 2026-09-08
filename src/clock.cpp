// SquachWatch-CYD — wall-clock time. See clock.h.
#include "clock.h"
#include <Arduino.h>
#include <time.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>   // strncasecmp

namespace Clock {

// 2025-01-01. An ESP32 that has never been told the time reports 1970, and
// a mistyped command is usually either tiny or enormous, so one threshold
// screens out both the unset case and the fat-fingered one.
static const uint32_t kPlausible = 1735689600u;

// Deliberately backed by the system clock rather than by a variable of our
// own. settimeofday puts it in the RTC domain, which keeps counting across
// a software reset -- so a watchdog reboot, a panic, or the SD-card boot
// loop does not take the time with it. A private static would.
bool isSet() {
    return (uint32_t)time(nullptr) > kPlausible;
}

uint32_t nowEpoch() {
    const uint32_t t = (uint32_t)time(nullptr);
    return (t > kPlausible) ? t : 0u;
}

bool setEpoch(uint32_t epoch) {
    if (epoch <= kPlausible) return false;
    struct timeval tv;
    tv.tv_sec  = (time_t)epoch;
    tv.tv_usec = 0;
    settimeofday(&tv, nullptr);
    return true;
}

uint32_t uptimeSec() { return millis() / 1000u; }

void formatUptime(char* out, size_t n) {
    const uint32_t s = uptimeSec();
    const uint32_t d = s / 86400u;
    const uint32_t h = (s % 86400u) / 3600u;
    const uint32_t m = (s % 3600u) / 60u;
    const uint32_t sec = s % 60u;
    if (d) snprintf(out, n, "%lud %02lu:%02lu:%02lu",
                    (unsigned long)d, (unsigned long)h,
                    (unsigned long)m, (unsigned long)sec);
    else   snprintf(out, n, "%02lu:%02lu:%02lu",
                    (unsigned long)h, (unsigned long)m, (unsigned long)sec);
}

void formatClock(char* out, size_t n) {
    if (!isSet()) { snprintf(out, n, "not set"); return; }
    const time_t t = (time_t)nowEpoch();
    struct tm tmv;
    localtime_r(&t, &tmv);
    snprintf(out, n, "%04d-%02d-%02d %02d:%02d",
             tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
             tmv.tm_hour, tmv.tm_min);
}

void formatStamp(uint32_t ms, char* out, size_t n) {
    if (isSet()) {
        // Wind the wall clock back by however long ago the stamp was
        // taken. The stamp itself is a millis() value, so the arithmetic
        // has to happen in uptime and only then convert.
        const uint32_t nowMs  = millis();
        const uint32_t agoSec = (nowMs > ms) ? (nowMs - ms) / 1000u : 0u;
        const time_t   t      = (time_t)(nowEpoch() - agoSec);
        struct tm tmv;
        localtime_r(&t, &tmv);
        snprintf(out, n, "%02d:%02d", tmv.tm_hour, tmv.tm_min);
        return;
    }
    // Unchanged from before the clock existed: minutes and seconds since
    // boot. Not useful for telling the time, but it still orders events,
    // which is all it ever did.
    const uint32_t sec = ms / 1000u;
    snprintf(out, n, "%02lu:%02lu",
             (unsigned long)(sec / 60u), (unsigned long)(sec % 60u));
}

void pollSerial() {
    // A line buffer rather than a parser. Anything that is not the one
    // command is answered and dropped -- this is a debug port, and silence
    // in response to a typo is worse than a line of help.
    static char line[48];
    static uint8_t len = 0;

    while (Serial.available() > 0) {
        const int c = Serial.read();
        if (c < 0) break;
        if (c == '\r') continue;
        if (c != '\n') {
            if (len < sizeof(line) - 1) line[len++] = (char)c;
            continue;   // keep reading; an over-long line is truncated, not split
        }
        line[len] = '\0';
        len = 0;
        if (line[0] == '\0') continue;

        if (strncasecmp(line, "TIME ", 5) == 0) {
            const uint32_t e = (uint32_t)strtoul(line + 5, nullptr, 10);
            if (setEpoch(e)) {
                char buf[32];
                formatClock(buf, sizeof(buf));
                Serial.printf("[clock] set to %s\n", buf);
            } else {
                Serial.printf("[clock] refused %lu -- expected seconds since "
                              "the epoch, e.g. TIME %lu\n",
                              (unsigned long)e, (unsigned long)kPlausible + 1u);
            }
        } else {
            Serial.printf("[clock] unknown command. TIME <epoch seconds> "
                          "sets the clock.\n");
        }
    }
}

}  // namespace Clock
