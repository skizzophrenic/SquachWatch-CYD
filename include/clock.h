// SquachWatch-CYD — wall-clock time
//
// Everything with a timestamp on it used to be counting from boot. A log
// row reading "71581:47" is 71581 minutes since the board powered up,
// which tells you the order things happened in and nothing else -- and the
// diary, which is supposed to be a record of days, could not name one.
//
// HOW IT GETS SET. The board is a passive sniffer and never associates with
// an access point on its own -- except for the two moments it already
// does: the update check at boot and UPDATE OVER WIFI. Both join the saved
// network anyway, so the clock rides along: SNTP starts the moment the join
// succeeds and is stopped again with the radio. A board with no saved
// network can still be told the time over serial (TIME <epoch>, at the
// documented 2,000,000 baud). Either way the clock is SET rather than
// continuously synced, and it is honest about not knowing: unset,
// everything falls back to uptime exactly as before.
//
// It survives a reboot, which is the case that matters most: the ESP32
// keeps system time across a software reset, so a watchdog or a panic (or
// the SD-card boot loop) does not lose it. Pulling the power does, until
// the next boot check puts it back.
//
// The zone is the user's to pick (TIME ZONE on the DESK MODE page): a POSIX
// rule per zone, so daylight saving flips itself.
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace Clock {

// Whether the clock has been told what time it is. Everything below
// degrades to uptime when this is false rather than inventing a date.
bool isSet();

// The note to self. Every ten minutes the time is written to flash, and
// a cold boot with no clock starts from that note: not the right time,
// since nobody knows how long the power was off, but not earlier than
// the note, which keeps the day count and the anniversaries honest. Such
// a clock is set but not trusted: the calendar helpers use it, the hour
// helpers (night, the LOG stamps, the hour lines, the desk digits) wait
// for a real answer. Any real set replaces the guess.
bool trusted();
bool guessed();
// Cold boots that started from the note since a real source last set the
// clock: each one hides an unknown stretch with the power off. 0 once a real
// time arrives. (The December look stops believing a guess after a week of
// them; see december.cpp.)
uint8_t guessBoots();
// From loop(): writes the note when it is due.
void tick(uint32_t now);

// Seconds since the Unix epoch, or 0 when unset.
uint32_t nowEpoch();

// Sets the clock. Rejects anything before 2025, which is what an unset
// ESP32 reports and what a mistyped command usually looks like.
bool setEpoch(uint32_t epoch);
// Called after every accepted setEpoch(), with the new time. The T-Watch
// hands it to its battery-backed clock chip so the time survives a flat
// battery; nothing else registers one.
void onSet(void (*fn)(uint32_t epoch));

uint32_t uptimeSec();

// "3d 04:11:52", or "04:11:52" under a day.
void formatUptime(char* out, size_t n);

// "2026-09-08 14:32", or "not set" -- for the diagnostics screen.
void formatClock(char* out, size_t n);

// One millis() stamp, as a log row wants it: "14:32" wall-clock when the
// clock is set and the stamp is from today, "9/13" when it is from another
// day, and the old "MMMM:SS" since boot when the clock is not set.
void formatStamp(uint32_t ms, char* out, size_t n);
// The same for a wall-clock second kept from an earlier boot (the black
// box): "14:32" today, "9/13" another day, "--:--" when it was never known.
void formatEpochStamp(uint32_t epoch, char* out, size_t n);

// ---- the calendar, once the clock is set ----------------------------
// Local hour 0..23, weekday 0..6 (Sunday first), and the local day as a
// count of days since the epoch -- the thing to compare to know whether it
// is still the same day. All 0 when unset.
uint8_t  hour();
uint8_t  minute();
uint8_t  weekday();
uint32_t localDay();
bool     weekend();
// Eleven at night to five in the morning. What "at night" means on the
// alert card.
bool     night();
// "SUN SEP 14" and "14:32" / "2:32" with the half of the day, for the desk.
void formatDate(char* out, size_t n);
void formatTime(char* out, size_t n, bool twelveHour, bool* pm = nullptr);

// ---- the zone ---------------------------------------------------------
uint8_t     zoneCount();
const char* zoneName(uint8_t i);
uint8_t     zoneStep(uint8_t i, int dir);   // the zone before (-1) or after (+1) i, west to east
void        applyZone(uint8_t i);   // Settings calls this at boot and on change

// ---- network time -----------------------------------------------------
// Around a WiFi join that is happening anyway. start() kicks SNTP off in
// the background; wait() blocks up to ms for the first answer and returns
// whether the clock is set; stop() takes SNTP down with the radio.
void syncStart();
bool syncWait(uint32_t ms);
void syncStop();
// True once this boot has heard a time from the network, as opposed to
// carrying one across a soft reset or being told over serial.
bool synced();

// ---- the board's own history ------------------------------------------
// The first day this board ever knew the date, remembered in NVS the first
// time the clock is set. 0 until then. daysTogether() counts from it.
uint32_t bornEpoch();
uint32_t daysTogether();
// Small persisted markers for once-a-day and once-ever lines. Both are the
// value last stored; the caller decides what to compare it to.
uint32_t greetedDay();            void setGreetedDay(uint32_t day);
uint32_t milestoneSaid();         void setMilestoneSaid(uint32_t days);

void begin();   // once in setup(), after Settings -- loads the NVS bits

// Polled from loop(). Reads a line and understands one command:
//
//   TIME <seconds since epoch>
//
// One line, no handshake, no menu. It is meant to be typed into a serial
// monitor or piped from `date +%s`, both of which somebody debugging this
// board already has open.
void pollSerial();

}  // namespace Clock
