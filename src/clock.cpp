// SquachWatch-CYD — wall-clock time. See clock.h.
#include "clock.h"
#include "serial_flush.h"
#include "security.h"   // a locked device takes no console commands
#include "ota_wifi.h"    // the WIFI command lists the saved networks
#include "detection.h"   // WINDOW N, for the bench; RADIO
#if defined(ARDUINO_ARCH_ESP32)
#include <esp_phy_init.h> // RADIO FULLCAL
#endif
#include "flood_bench.h" // FLOOD N, for the bench (a no-op outside FLOOD_BENCH builds)
#include "settings.h"
#include "crowd_bench.h"
#include "blackbox.h"    // BLACKBOX dumps it
#include "ota_core.h"    // VERTEST, on bench builds
#include "frame_push.h"  // PUSH, the frame-push switch
#include "fast_sprite.h" // FAST, the sprite fast-path switch
#include "ui_clear.h"    // PACE, the mascot step clock
#include "draw_band.h"   // BAND, the 3.5in row gate
#include "squachy.h"     // TEMPO, his durations
#include "lora_sniffer.h" // LORA ..., the wireless slot (a no-op without one)

// PRIM, on every build: main.cpp runs the primitive benchmark on its next pass.
extern volatile bool g_benchPrimNow;
#if defined(ARDUINO_ARCH_ESP32)
// MEM reads the board itself: FreeRTOS for the stacks, NVS for the store.
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <esp_heap_caps.h>
#endif
#include <Arduino.h>
#include <Preferences.h>
#include <time.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>   // strncasecmp
#if defined(ARDUINO_ARCH_ESP32)
#include <WiFi.h>
#include <WiFiUdp.h>
#endif

#ifdef BENCH_TOOLS
// Set by UPDATE NOW below, acted on by main.cpp's loop, which owns it.
extern volatile bool g_benchUpdateNow;
extern volatile bool g_benchUpdateStop;
#endif
// INVERT and ROT: the colour-check toggles and the corner rotate button,
// from the console, for bringing up a panel nobody can read yet.
extern volatile bool g_consoleInvert;
extern volatile bool g_consoleWatchTest;
extern volatile bool g_consoleRotate;
extern volatile bool g_consoleBatt;
extern volatile bool g_consoleBattLog;
extern volatile bool g_consoleRadioTest;
extern volatile bool g_consolePmu;
extern volatile bool g_consoleRtc;
extern volatile bool g_consoleBuzz;
extern volatile bool g_consoleMotion;
extern volatile bool g_consoleXtal;
extern volatile bool g_consoleHeal;

namespace Clock {

// 2025-01-01. An ESP32 that has never been told the time reports 1970, and
// a mistyped command is usually either tiny or enormous, so one threshold
// screens out both the unset case and the fat-fingered one.
static const uint32_t kPlausible = 1735689600u;

static Preferences s_prefs;
static bool        s_begun    = false;
static bool        s_synced   = false;
static uint32_t    s_born     = 0;
static uint32_t    s_greeted  = 0;
static uint32_t    s_mileSaid = 0;
static bool        s_guess    = false;   // running from the note to self
static uint32_t    s_lastNote = 0;       // millis() of the last note written
static const uint32_t NOTE_EVERY_MS = 10u * 60u * 1000u;

#if !defined(ARDUINO_ARCH_ESP32)
// The emulator: SQUACH_EPOCH in the environment pins the clock to a moment
// (moving forward with millis() from there), so a one-shot screen renders
// the same at any hour. Without it the host's real clock is used.
static uint32_t s_simEpoch = 0, s_simMs0 = 0;
static bool     s_simInit  = false;
static void simInit() {
    if (s_simInit) return;
    s_simInit = true;
    const char* e = getenv("SQUACH_EPOCH");
    if (e && *e) { s_simEpoch = (uint32_t)strtoul(e, nullptr, 10); s_simMs0 = millis(); }
}
#endif

static uint32_t rawNow() {
#if !defined(ARDUINO_ARCH_ESP32)
    simInit();
    if (s_simEpoch) return s_simEpoch + (millis() - s_simMs0) / 1000u;
#endif
    return (uint32_t)time(nullptr);
}

// Deliberately backed by the system clock rather than by a variable of our
// own. settimeofday puts it in the RTC domain, which keeps counting across
// a software reset -- so a watchdog reboot, a panic, or the SD-card boot
// loop does not take the time with it. A private static would.
// A clock this far past the build is not set, it is stale: an ESP32-S3
// keeps its system time across resets while the battery keeps it powered,
// and the T-Watch arrived believing it was 2064 from whatever the factory
// firmware left in the RTC. Trusted, that would have blocked every real
// time offer (the mesh only corrects a clock that is NOT trusted). Six
// years past the build date is the line.
static uint32_t farFuture() {
    static uint32_t limit = 0;
    if (!limit) {
        const char* d = __DATE__;          // "Sep 22 2026"
        const int year = atoi(d + 7);
        limit = (uint32_t)((year - 1970 + 6) * 365.25 * 86400.0);
    }
    return limit;
}
bool isSet() {
    const uint32_t t = rawNow();
    return t > kPlausible && t < farFuture();
}
bool trusted() { return isSet() && !s_guess; }
bool guessed() { return isSet() && s_guess; }

uint32_t nowEpoch() {
    const uint32_t t = rawNow();
    return (t > kPlausible && t < farFuture()) ? t : 0u;
}

// The first time this board knows the date, that date is kept: it is the
// day Squachy counts from.
static void noteKnown() {
    if (!s_begun || s_born) return;
    s_born = nowEpoch();
    if (s_born) s_prefs.putUInt("born", s_born);
}

static void writeSystemClock(uint32_t epoch) {
#if defined(ARDUINO_ARCH_ESP32)
    struct timeval tv;
    tv.tv_sec  = (time_t)epoch;
    tv.tv_usec = 0;
    settimeofday(&tv, nullptr);
#else
    // The emulator keeps its own epoch (see rawNow): the host's clock is not
    // ours to set, and the browser build has no settimeofday to link at all.
    simInit();
    s_simEpoch = epoch; s_simMs0 = millis();
#endif
}

static void (*s_onSet)(uint32_t) = nullptr;
void onSet(void (*fn)(uint32_t)) { s_onSet = fn; }

bool setEpoch(uint32_t epoch) {
    if (epoch <= kPlausible) return false;
    // A real answer, from wherever: the guess is over, and the note is
    // brought up to date at once rather than ten minutes from now.
    s_guess = false;
    writeSystemClock(epoch);
    noteKnown();
    if (s_begun) { s_prefs.putUInt("last", epoch); s_lastNote = millis(); }
    if (s_onSet && isSet()) s_onSet(epoch);
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

static bool localNow(struct tm& tmv, uint32_t epoch = 0) {
    if (!isSet()) return false;
    const time_t t = (time_t)(epoch ? epoch : nowEpoch());
    localtime_r(&t, &tmv);
    return true;
}

void formatClock(char* out, size_t n) {
    struct tm tmv;
    if (!localNow(tmv)) { snprintf(out, n, "not set"); return; }
    snprintf(out, n, "%04d-%02d-%02d %02d:%02d%s",
             tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
             tmv.tm_hour, tmv.tm_min, s_guess ? " at least" : "");
}

void formatStamp(uint32_t ms, char* out, size_t n) {
    if (trusted()) {
        // Wind the wall clock back by however long ago the stamp was
        // taken. The stamp itself is a millis() value, so the arithmetic
        // has to happen in uptime and only then convert.
        const uint32_t nowMs  = millis();
        const uint32_t agoSec = (nowMs > ms) ? (nowMs - ms) / 1000u : 0u;
        struct tm then, today;
        localNow(then, nowEpoch() - agoSec);
        localNow(today);
        if (then.tm_yday == today.tm_yday && then.tm_year == today.tm_year)
            snprintf(out, n, "%02d:%02d", then.tm_hour, then.tm_min);
        else
            // Another day: the date says more than the hour would.
            snprintf(out, n, "%d/%d", then.tm_mon + 1, then.tm_mday);
        return;
    }
    // Unchanged from before the clock existed: minutes and seconds since
    // boot. Not useful for telling the time, but it still orders events,
    // which is all it ever did.
    const uint32_t sec = ms / 1000u;
    snprintf(out, n, "%02lu:%02lu",
             (unsigned long)(sec / 60u), (unsigned long)(sec % 60u));
}

void formatEpochStamp(uint32_t epoch, char* out, size_t n) {
    if (!epoch) { snprintf(out, n, "--:--"); return; }
    // The zone is applied at boot whether or not the clock is set, so a
    // stamp from a boot that knew the time still reads right in one that
    // does not; it just cannot say "today".
    struct tm then, today;
    const time_t t = (time_t)epoch;
    localtime_r(&t, &then);
    if (localNow(today) && then.tm_yday == today.tm_yday && then.tm_year == today.tm_year)
        snprintf(out, n, "%02d:%02d", then.tm_hour, then.tm_min);
    else
        snprintf(out, n, "%d/%d", then.tm_mon + 1, then.tm_mday);
}

// ---- the calendar ------------------------------------------------------
uint8_t  hour()    { struct tm t; return localNow(t) ? (uint8_t)t.tm_hour : 0; }
uint8_t  minute()  { struct tm t; return localNow(t) ? (uint8_t)t.tm_min  : 0; }
uint8_t  weekday() { struct tm t; return localNow(t) ? (uint8_t)t.tm_wday : 0; }
bool     weekend() { const uint8_t d = weekday(); return trusted() && (d == 0 || d == 6); }
bool     night()   { const uint8_t h = hour(); return trusted() && (h >= 23 || h < 5); }

uint32_t localDay() {
    struct tm t;
    if (!localNow(t)) return 0;
    // Days since the epoch in LOCAL time: the UTC day number shifted by the
    // zone, so midnight here is where the count ticks.
    const time_t e = (time_t)nowEpoch();
    struct tm u; gmtime_r(&e, &u);
    int32_t day = (int32_t)(e / 86400);
    if (t.tm_yday != u.tm_yday || t.tm_year != u.tm_year) {
        // Local is on the other side of a UTC midnight, one way or the other.
        const bool ahead = (t.tm_year > u.tm_year) || (t.tm_year == u.tm_year && t.tm_yday > u.tm_yday);
        day += ahead ? 1 : -1;
    }
    return (uint32_t)day;
}

static const char* const DAY_NAMES[]   = { "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" };
static const char* const MONTH_NAMES[] = { "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                           "JUL", "AUG", "SEP", "OCT", "NOV", "DEC" };

void formatDate(char* out, size_t n) {
    struct tm t;
    if (!localNow(t)) { snprintf(out, n, "NO DATE YET"); return; }
    snprintf(out, n, "%s %s %d", DAY_NAMES[t.tm_wday], MONTH_NAMES[t.tm_mon], t.tm_mday);
}

void formatTime(char* out, size_t n, bool twelveHour, bool* pm) {
    struct tm t;
    if (!localNow(t)) { snprintf(out, n, "--:--"); if (pm) *pm = false; return; }
    int h = t.tm_hour;
    if (pm) *pm = h >= 12;
    if (twelveHour) { h %= 12; if (h == 0) h = 12; }
    snprintf(out, n, twelveHour ? "%d:%02d" : "%02d:%02d", h, t.tm_min);
}

// ---- the zone ------------------------------------------------------------
// POSIX rules rather than fixed offsets so daylight saving takes care of
// itself. Names are what fits a settings row.
struct Zone { const char* name; const char* rule; };
static const Zone ZONES[] = {
    { "US EASTERN",  "EST5EDT,M3.2.0,M11.1.0" },
    { "US CENTRAL",  "CST6CDT,M3.2.0,M11.1.0" },
    { "US MOUNTAIN", "MST7MDT,M3.2.0,M11.1.0" },
    { "ARIZONA",     "MST7" },
    { "US PACIFIC",  "PST8PDT,M3.2.0,M11.1.0" },
    { "ALASKA",      "AKST9AKDT,M3.2.0,M11.1.0" },
    { "HAWAII",      "HST10" },
    { "ATLANTIC",    "AST4ADT,M3.2.0,M11.1.0" },
    { "NEWFOUNDLAND","NST3:30NDT,M3.2.0,M11.1.0" },
    { "BRAZIL",      "<-03>3" },
    { "UTC",         "UTC0" },
    { "UK",          "GMT0BST,M3.5.0/1,M10.5.0" },
    { "EU CENTRAL",  "CET-1CEST,M3.5.0,M10.5.0/3" },
    { "EU EASTERN",  "EET-2EEST,M3.5.0/3,M10.5.0/4" },
    { "MOSCOW",      "MSK-3" },
    { "INDIA",       "IST-5:30" },
    { "CHINA",       "CST-8" },
    { "JAPAN",       "JST-9" },
    { "AUS EASTERN", "AEST-10AEDT,M10.1.0,M4.1.0/3" },
    { "AUS WESTERN", "AWST-8" },
    { "NEW ZEALAND", "NZST-12NZDT,M9.5.0,M4.1.0/3" },
};
static const uint8_t ZONES_N = sizeof(ZONES) / sizeof(ZONES[0]);

uint8_t     zoneCount()        { return ZONES_N; }
const char* zoneName(uint8_t i){ return ZONES[i < ZONES_N ? i : 0].name; }
void applyZone(uint8_t i) {
    setenv("TZ", ZONES[i < ZONES_N ? i : 0].rule, 1);
    tzset();
}

// ---- network time ----------------------------------------------------------
// One NTP request, by hand, rather than LWIP's SNTP client. That client
// waits a random zero to five seconds before its first request (its
// SNTP_STARTUP_DELAY), which is longer than the whole boot check; measured
// as "no answer in 2500 ms" on the soak board. A single UDP packet gets
// an answer in the time a DNS lookup takes.
void syncStart() {}
void syncStop()  {}

#if defined(ARDUINO_ARCH_ESP32)
static bool ntpOnce(const char* host, uint32_t ms) {
    IPAddress ip;
    if (!WiFi.hostByName(host, ip)) return false;
    WiFiUDP udp;
    if (!udp.begin(2390)) return false;
    uint8_t pkt[48] = {0};
    pkt[0] = 0x1B;                       // LI 0, version 3, mode 3 (client)
    udp.beginPacket(ip, 123);
    udp.write(pkt, sizeof pkt);
    udp.endPacket();
    const uint32_t t0 = millis();
    bool got = false;
    while (millis() - t0 < ms) {
        if (udp.parsePacket() >= 48) {
            udp.read(pkt, sizeof pkt);
            // Transmit timestamp, seconds since 1900.
            const uint32_t secs1900 = ((uint32_t)pkt[40] << 24) | ((uint32_t)pkt[41] << 16) |
                                      ((uint32_t)pkt[42] << 8)  |  (uint32_t)pkt[43];
            const uint32_t epoch = secs1900 - 2208988800UL;
            got = setEpoch(epoch);
            break;
        }
        delay(10);
    }
    udp.stop();
    return got;
}
#endif

bool syncWait(uint32_t ms) {
#if defined(ARDUINO_ARCH_ESP32)
    const uint32_t t0 = millis();
    static const char* const HOSTS[] = { "pool.ntp.org", "time.google.com" };
    for (const char* h : HOSTS) {
        const uint32_t used = millis() - t0;
        if (used >= ms) break;
        if (ntpOnce(h, (ms - used) / 2 < 400 ? ms - used : (ms - used) / 2)) { s_synced = true; break; }
    }
    if (isSet()) noteKnown();
    return isSet();
#else
    (void)ms;
    return isSet();
#endif
}

bool synced() { return s_synced; }

// ---- the board's own history ----------------------------------------------
uint32_t bornEpoch() { return s_born; }
uint32_t daysTogether() {
    const uint32_t now = nowEpoch();
    if (!s_born || !now || now < s_born) return 0;
    return (now - s_born) / 86400u;
}
uint32_t greetedDay()               { return s_greeted; }
void     setGreetedDay(uint32_t d)  { s_greeted = d; if (s_begun) s_prefs.putUInt("greet", d); }
uint32_t milestoneSaid()            { return s_mileSaid; }
void     setMilestoneSaid(uint32_t d){ s_mileSaid = d; if (s_begun) s_prefs.putUInt("mile", d); }

void begin() {
    if (s_begun) return;
    s_prefs.begin("clock", false);
    s_begun    = true;
    s_born     = s_prefs.getUInt("born", 0);
    s_greeted  = s_prefs.getUInt("greet", 0);
    s_mileSaid = s_prefs.getUInt("mile", 0);
    // Carried across a soft reset with the date already known: the first
    // such boot is still the first day.
    if (isSet()) { noteKnown(); return; }
    // A cold boot: the note to self, if there is one, as a floor.
    const uint32_t last = s_prefs.getUInt("last", 0);
    if (last > kPlausible) {
        writeSystemClock(last);
        s_guess = true;
        char buf[40];
        formatClock(buf, sizeof buf);
        Serial.printf("[clock] no clock; starting from the last note: %s\n", buf);
    }
}

void tick(uint32_t now) {
    // The note: only a real time is worth writing, and not too often --
    // flash has a life, and ten minutes of doubt is nothing next to a
    // night with the power off.
    if (!s_begun || !trusted()) return;
    if (now - s_lastNote < NOTE_EVERY_MS) return;
    s_lastNote = now;
    s_prefs.putUInt("last", nowEpoch());
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
        // Locked means locked here too -- otherwise the lock screen is a door
        // with a USB cable propped against it.
        if (Security::locked()) {
            Serial.println("[security] locked -- unlock it on the screen first.");
            continue;
        }

        if (strncasecmp(line, "FLOOD ", 6) == 0) {
            // A thousand a second is five times the loudest room measured;
            // a typo like -1 would be sixty-five thousand in one burst.
            int n = atoi(line + 6);
            if (n < 0) n = 0;
            if (n > 1000) n = 1000;
            floodSet((uint16_t)n);
            continue;
        }
        if (strncasecmp(line, "SCAN ", 5) == 0) {
            const char* a = line + 5;
            while (*a == ' ') a++;
            if      (strcasecmp(a, "ACTIVE")  == 0) { setScanPin(1); Serial.println("[scan] pinned ACTIVE"); }
            else if (strcasecmp(a, "PASSIVE") == 0) { setScanPin(2); Serial.println("[scan] pinned PASSIVE"); }
            else if (strcasecmp(a, "AUTO")    == 0) { setScanPin(0); Serial.println("[scan] back to AUTO"); }
            else Serial.println("[scan] unknown. One of: ACTIVE, PASSIVE, AUTO");
            continue;
        }
        if (strncasecmp(line, "INTERVAL ", 9) == 0) {
            unsigned ms = 0, win = 0;
            if (sscanf(line + 9, "%u %u", &ms, &win) == 2) {
                setScanInterval((uint16_t)ms, (uint8_t)win);
                Serial.printf("[scan] interval %u ms, window %u ms from the next restart\n", ms, win);
            }
            continue;
        }
        if (strncasecmp(line, "WINDOW ", 7) == 0) {
            setScanWindow((uint8_t)atoi(line + 7));
            continue;
        }
        if (strncasecmp(line, "CLOCK ", 6) == 0) {
            // CLOCK <font 0-1> <size 0-2> <backdrop 0-6>: the desk clock's
            // look, set from the bench, so its frame time can be measured for
            // each without a finger on the glass.
            int f = -1, z = -1, b = -1;
            if (sscanf(line + 6, "%d %d %d", &f, &z, &b) == 3 && f >= 0 && f < 2 && z >= 0 && z < 3 && b >= 0 && b < 7) {
                for (int g = 0; g < 2 && Settings::clockFont() != f; g++)     Settings::cycleClockFont();
                for (int g = 0; g < 3 && Settings::clockSize() != z; g++)     Settings::cycleClockSize();
                for (int g = 0; g < 7 && Settings::clockBackdrop() != b; g++) Settings::cycleClockBackdrop();
                Serial.printf("[clock] look: %s, %s, %s\n", Settings::clockFontName(),
                              Settings::clockSizeName(), Settings::clockBackdropName());
            } else {
                Serial.println("[clock] CLOCK <font 0-1> <size 0-2> <backdrop 0-6>");
            }
            continue;
        }
        if (strcasecmp(line, "WIFI") == 0) {
            OtaWifi::printSaved();
            continue;
        }
        if (strcasecmp(line, "INVERT") == 0) { g_consoleInvert = true; continue; }
        if (strcasecmp(line, "WATCHTEST") == 0) { g_consoleWatchTest = true; continue; }
        if (strcasecmp(line, "ROT") == 0)    { g_consoleRotate = true; continue; }
        if (strcasecmp(line, "BATT") == 0)    { g_consoleBatt = true; continue; }
        if (strcasecmp(line, "BATTLOG") == 0) { g_consoleBattLog = true; continue; }
        if (strcasecmp(line, "RADIO TEST") == 0) { g_consoleRadioTest = !g_consoleRadioTest; Serial.printf("[radio] bench test %s\n", g_consoleRadioTest ? "ON: cycling on the cable, screen or not" : "OFF"); continue; }
        if (strcasecmp(line, "PMU") == 0)    { g_consolePmu = true; continue; }
        if (strcasecmp(line, "RTC") == 0)    { g_consoleRtc = true; continue; }
        if (strcasecmp(line, "BUZZ") == 0)   { g_consoleBuzz = true; continue; }
        if (strcasecmp(line, "MOTION") == 0) { g_consoleMotion = true; continue; }
        if (strcasecmp(line, "XTAL") == 0)   { g_consoleXtal = true; continue; }
        if (strcasecmp(line, "RADIO DUTY") == 0) {
            Settings::cycleRadioDuty();
            Serial.printf("[radio] duty -> %s\n", Settings::radioDutyName(Settings::radioDutyRaw()));
            continue;
        }
        if (Lora::console(line)) continue;
#if defined(ARDUINO_ARCH_ESP32)   // the radios themselves: nothing to ask in the emulator
        if (strcasecmp(line, "RADIO HEAL") == 0) { g_consoleHeal = true; continue; }
        if (strcasecmp(line, "RADIO FULLCAL") == 0) {
            // Throw away the radio's saved tuning and restart: the next boot
            // has nothing to load, so it calibrates from scratch.
            Serial.printf("[radio] erasing saved RF calibration: err %d; restarting\n", (int)esp_phy_erase_cal_data_in_nvs());
            delay(300);
            ESP.restart();
        }
        if (strncasecmp(line, "RADIO", 5) == 0) { radioReport(strcasestr(line, "SCAN") != nullptr); continue; }
#endif
        if (strncasecmp(line, "ZONE ", 5) == 0) {
            // ZONE US EASTERN, or ZONE 4: the flasher sends the name it
            // worked out from the browser's own zone.
            const char* arg = line + 5;
            while (*arg == ' ') arg++;
            int found = -1;
            if (*arg >= '0' && *arg <= '9') {
                const long i = strtol(arg, nullptr, 10);
                if (i >= 0 && i < (long)zoneCount()) found = (int)i;
            } else {
                for (uint8_t i = 0; i < zoneCount(); i++)
                    if (strcasecmp(arg, zoneName(i)) == 0) { found = i; break; }
            }
            if (found >= 0) {
                Settings::setTimeZone((uint8_t)found);
                char buf[32];
                formatClock(buf, sizeof buf);
                Serial.printf("[zone] %s (%s)\n", zoneName((uint8_t)found), buf);
            } else {
                Serial.print("[zone] unknown. One of:");
                for (uint8_t i = 0; i < zoneCount(); i++) Serial.printf(" %s,", zoneName(i));
                Serial.println();
            }
        } else if (strncasecmp(line, "TIME ", 5) == 0) {
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
        } else if (strncasecmp(line, "PRIM", 4) == 0) {
            // Times every drawing primitive on the real frame buffer; see
            // runPrimBench() in main.cpp. Runs on the next pass of loop().
            g_benchPrimNow = true;
            Serial.println("[prim] on the next frame");
        } else if (strncasecmp(line, "BG ", 3) == 0) {
            // BG N: show background N until the next boot, without saving it.
            // For timing them one after another off the [frame] line.
            const long n = strtol(line + 3, nullptr, 10);
            if (n >= 0 && Settings::previewBackground((Settings::Background)n))
                Serial.printf("[bg] %ld %s\n", n, Settings::backgroundName((Settings::Background)n));
            else
                Serial.printf("[bg] no background %ld\n", n);
        } else if (strncasecmp(line, "BAND", 4) == 0) {
            // BAND ON/OFF: the 3.5in's per-pass row gate. Nothing anywhere
            // else -- the other boards draw one pass over a whole frame, and
            // the gate is not compiled into them at all.
            const char* arg = line + 4;
            while (*arg == ' ') arg++;
            if (strncasecmp(arg, "ON", 2) == 0)       DrawBand::setEnabled(true);
            else if (strncasecmp(arg, "OFF", 3) == 0) DrawBand::setEnabled(false);
            Serial.printf("[band] row gate %s\n", DrawBand::enabled() ? "on" : "off (whole screen, both passes)");
        } else if (strncasecmp(line, "TEMPO", 5) == 0) {
            // TEMPO P: every one of Squachy's durations at P percent, kept.
            const char* arg = line + 5;
            while (*arg == ' ') arg++;
            if (*arg >= '0' && *arg <= '9') {
                Settings::setMascotTempoPct((uint8_t)strtoul(arg, nullptr, 10));
                Squachy::setTempo(Settings::mascotTempoPct());
            }
            Serial.printf("[tempo] Squachy at %u%% -- a 2 s mood lasts %lu ms\n",
                          (unsigned)Settings::mascotTempoPct(), (unsigned long)(2000UL * Settings::mascotTempoPct() / 100));
        } else if (strncasecmp(line, "PACE", 4) == 0) {
            // PACE N: the mascot steps every N ms from now on (this boot).
            // PACE alone reports. For choosing the number by eye.
            const char* arg = line + 4;
            while (*arg == ' ') arg++;
            if (*arg >= '0' && *arg <= '9') uiMascotStepSet((uint32_t)strtoul(arg, nullptr, 10));
            Serial.printf("[pace] mascot steps every %lu ms (%lu a second)\n",
                          (unsigned long)uiMascotStepMs(), (unsigned long)(1000 / uiMascotStepMs()));
        } else if (strncasecmp(line, "FAST", 4) == 0) {
            // FAST ON / FAST OFF: the sprite's fast primitives, or the library's.
#if defined(ARDUINO_ARCH_ESP32)
            const char* arg = line + 4;
            while (*arg == ' ') arg++;
            if (strncasecmp(arg, "OFF", 3) == 0) FastSprite::setFast(false);
            else if (strncasecmp(arg, "ON", 2) == 0) FastSprite::setFast(true);
            Serial.printf("[fast] %s\n", FastSprite::fast() ? "on" : "off -- the library's primitives");
#else
            Serial.println("[fast] not on this build");
#endif
        } else if (strncasecmp(line, "PUSH", 4) == 0) {
            // PUSH / PUSH ON / PUSH OFF. The switch for the overlapped frame
            // push. Not behind BENCH_TOOLS on purpose: its whole job is to
            // rule the new path out on a board that is drawing strangely,
            // which will not be happening at a bench.
            const char* arg = line + 4;
            while (*arg == ' ') arg++;
            if (!FramePush::available()) {
                Serial.println("[push] not built for this board -- the ordinary push is the only one");
            } else if (strncasecmp(arg, "ON", 2) == 0) {
                FramePush::setEnabled(true);
                Serial.println("[push] overlapped push on");
            } else if (strncasecmp(arg, "OFF", 3) == 0) {
                FramePush::setEnabled(false);
                Serial.println("[push] off -- the ordinary push until the next boot");
            } else {
                Serial.printf("[push] %s. PUSH ON / PUSH OFF to change it\n",
                              FramePush::enabled() ? "on" : "off");
            }
        } else if (strncasecmp(line, "BLACKBOX", 8) == 0) {
            BlackBox::dump();
        } else if (strncasecmp(line, "LOG", 3) == 0) {
            logDump();
#if defined(ARDUINO_ARCH_ESP32)
        } else if (strncasecmp(line, "MEM", 3) == 0) {
            // Where the RAM actually is: the heap, the spare room at the
            // bottom of every task's stack, and how full the settings store
            // is. The stacks are fixed sizes picked years ago and never
            // measured; a store that fills up stops saving settings quietly.
            Serial.printf("[mem] heap %lu free, %lu largest\n",
                          (unsigned long)ESP.getFreeHeap(),
                          (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
            // uxTaskGetSystemState needs a trace facility this build does
            // not carry, so the tasks are asked for by name -- the ones with
            // a stack size somebody picked, which is the question here.
            static const char* TASKS[] = { "loopTask", "nimble_host", "btController",
                                           "wifi", "tiT", "esp_timer", "arduino_events",
                                           "sys_evt", "otawifi", "IDLE" };
            for (uint8_t i = 0; i < sizeof TASKS / sizeof TASKS[0]; i++) {
                TaskHandle_t th = xTaskGetHandle(TASKS[i]);
                if (!th) continue;
                Serial.printf("[mem] task %-16s spare %lu bytes\n", TASKS[i],
                              (unsigned long)uxTaskGetStackHighWaterMark(th));
            }
            nvs_stats_t ns;
            if (nvs_get_stats(nullptr, &ns) == ESP_OK)
                Serial.printf("[mem] settings store %u of %u entries used (%u free), %u namespaces\n",
                              (unsigned)ns.used_entries, (unsigned)ns.total_entries,
                              (unsigned)ns.free_entries, (unsigned)ns.namespace_count);
            else
                Serial.println("[mem] settings store: no stats");
#endif
#ifdef BENCH_TOOLS
        } else if (strncasecmp(line, "VERTEST ", 8) == 0) {
            Serial.printf("[ota] %s\n", OtaCore::testVersionDecision(line + 8));
        } else if (strncasecmp(line, "SIGTEST", 7) == 0) {
            Serial.printf("[ota] %s\n", OtaCore::testSignature());
        } else if (strncasecmp(line, "UPDATE NOW", 10) == 0) {
            // A whole WiFi update, start to finish, with nobody at the board.
            // main.cpp picks this up on its next pass; see g_benchUpdateNow.
            g_benchUpdateNow = true;
            Serial.println("[bench] asking for a WiFi update");
        } else if (strncasecmp(line, "UPDATE STOP", 11) == 0) {
            g_benchUpdateStop = true;
            Serial.println("[bench] cancelling the update");
        } else if (strncasecmp(line, "CRASH ME", 8) == 0) {
            // Bench builds only (-DBENCH_TOOLS=1): a deliberate panic, to
            // prove the crash history catches one.
            Serial.println("[bench] crashing on purpose");
            serialFlush();
            volatile int* p = nullptr;
            *p = 1;
#endif
#if CROWD_BENCH
        } else if (strncasecmp(line, "CROWD", 5) == 0) {
            CrowdBench::command(line + 5);
#endif
        } else {
            Serial.printf("[clock] unknown command. TIME <epoch seconds> sets the clock, "
                          "ZONE <name> the zone.\n");
        }
    }
}

}  // namespace Clock
