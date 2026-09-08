// SquachWatch-CYD — wall-clock time
//
// Everything with a timestamp on it used to be counting from boot. A log
// row reading "71581:47" is 71581 minutes since the board powered up,
// which tells you the order things happened in and nothing else -- and the
// diary, which is supposed to be a record of days, could not name one.
//
// WHY THERE IS NO NETWORK SYNC. The obvious answer is SNTP, and it is not
// available here. detection.cpp puts the radio into promiscuous mode and
// never associates with an access point: the whole device is a passive
// sniffer. Getting network time would mean asking the user for WiFi
// credentials, building somewhere to type them, dropping out of
// promiscuous mode for several seconds and going deaf while it syncs.
// That is a feature, not a detail, and it is not this one.
//
// So the clock is SET rather than synced -- over serial, which the project
// already documents at 2,000,000 baud -- and it is honest about not
// knowing. Unset, everything falls back to uptime exactly as before.
//
// It does survive a reboot, which is the case that matters most: the ESP32
// keeps system time across a software reset, so a watchdog or a panic (or
// the SD-card boot loop) does not lose it. Pulling the power does.
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace Clock {

// Whether the clock has been told what time it is. Everything below
// degrades to uptime when this is false rather than inventing a date.
bool isSet();

// Seconds since the Unix epoch, or 0 when unset.
uint32_t nowEpoch();

// Sets the clock. Rejects anything before 2025, which is what an unset
// ESP32 reports and what a mistyped command usually looks like.
bool setEpoch(uint32_t epoch);

uint32_t uptimeSec();

// "3d 04:11:52", or "04:11:52" under a day.
void formatUptime(char* out, size_t n);

// "2026-09-08 14:32", or "not set" -- for the diagnostics screen.
void formatClock(char* out, size_t n);

// One millis() stamp, as a log row wants it: "14:32" wall-clock when the
// clock is set, and the old "MMMM:SS" since boot when it is not.
void formatStamp(uint32_t ms, char* out, size_t n);

// Polled from loop(). Reads a line and understands one command:
//
//   TIME <seconds since epoch>
//
// One line, no handshake, no menu. It is meant to be typed into a serial
// monitor or piped from `date +%s`, both of which somebody debugging this
// board already has open.
void pollSerial();

}  // namespace Clock
