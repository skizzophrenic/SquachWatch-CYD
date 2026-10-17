// SquachWatch-CYD — GNSS: the T-Watch S3 Plus's GPS, as a position and a time.
//
// The parser is plain C++ with no hardware in it, so the host tests feed it
// sentences. The part that powers the module and reads its UART lives behind
// TWATCH_S3 (see gnss_hw in main.cpp's watch section); on every other board
// nothing ever calls feed() and fix().valid stays false.
//
// NMEA 0183 as the u-blox M10 sends it by default: GGA for the fix, the
// satellites used and the height; RMC for the date, which GGA does not carry;
// GSV for what is in view. Talker IDs (GP, GN, GL, GA, GB, BD, GQ) are all
// accepted -- the M10 reports its combined solution as GN.
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace Gnss {

struct Fix {
    bool     valid = false;    // a position from a GGA with quality > 0
    int32_t  lat7  = 0;        // degrees x 10^7, north positive
    int32_t  lon7  = 0;        // degrees x 10^7, east positive
    int16_t  altM  = 0;        // above mean sea level, metres
    uint8_t  accM  = 0;        // horizontal accuracy estimate: HDOP x 4 m, 1..255
    uint8_t  used  = 0;        // satellites in the solution
    uint32_t epoch = 0;        // UTC seconds of the fix; 0 until an RMC gave the date
    uint32_t atMs  = 0;        // millis() the fix arrived, for staleness
};

struct Sky {
    uint8_t view  = 0;         // satellites listed in view, every constellation
    uint8_t heard = 0;         // of those, the ones with a signal (SNR given)
};

// One character off the UART. Lines are assembled internally; a complete,
// checksum-valid sentence updates the state. `nowMs` stamps a new fix.
void feed(char c, uint32_t nowMs);
// A whole sentence at once, with or without its line ending. For tests.
bool sentence(const char* s, uint32_t nowMs);

const Fix& fix();
Sky sky();
// UTC from the last RMC, or 0: set the clock from it once it has a fix.
uint32_t utcEpoch();
// Sentences parsed and rejected (bad checksum), since the last reset.
uint32_t good();
uint32_t bad();

// A fix older than this is not a position any more (the watch has moved on,
// or the sky closed). Rows are only written against a fresh one.
constexpr uint32_t FRESH_MS = 5000;
bool fresh(uint32_t nowMs);

void reset();

// Days-from-civil: a UTC calendar date and time to Unix seconds. Exposed for
// the tests; nothing about it is GNSS-specific.
uint32_t toEpoch(int year, int month, int day, int h, int m, int s);

// Bench only: pretend to have a fix here, for testing the wardrive path on a
// desk. Marked, so an export can leave those rows out.
void fake(int32_t lat7, int32_t lon7, uint32_t epoch, uint32_t nowMs);
bool faked();

// A position from a phone in the squad (the companion app's FIX frame), for a
// board with no GPS of its own. A real position, so nothing leaves it out; it
// stays fresh for PHONE_FRESH_MS, because the phone sends one every ten
// seconds or so rather than every second. A board's own GPS, when it has a
// fresh fix, wins: the phone is somewhere near, the module is right here.
constexpr uint32_t PHONE_FRESH_MS = 60000;
void phone(int32_t lat7, int32_t lon7, uint8_t accM, uint32_t epoch, uint32_t nowMs);
bool fromPhone();
#if defined(WAVESHARE_206)
// The phone's altitude for the position just given (phone_gps.h).
void phoneAlt(int16_t altM);
#endif

}  // namespace Gnss
