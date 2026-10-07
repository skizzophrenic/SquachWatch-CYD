// SquachWatch-CYD — the SD log's file name and CSV row, as plain text work.
//
// No card and no Arduino here, so the host tests can check a row to the last
// digit. sd_log.cpp owns the card and calls these.
//
// A row is: time, type, rssi, mac, channel, vendor, name. The time is
// "YYYY-MM-DDTHH:MM:SSZ" in UTC when the clock is trusted, else millis()
// since boot. A GPS build adds three columns at the end: latitude, longitude
// (signed decimal degrees, 7 places) and the accuracy in meters, or FAKE for
// a bench fix typed in on the console. All three stay empty without a fresh
// fix. Other builds write the seven columns only.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "state.h"
#include "gnss.h"

namespace SdRow {

// The buffer a caller gives line().
constexpr size_t ROW_MAX = 144;

// "/squachwatch-YYYYMMDD.log" for a local day number (days since 1970), or
// "/squachwatch-nodate.log" for 0. False if it did not fit.
bool fileName(char* out, size_t n, uint32_t localDay);

// Signed decimal degrees from degrees x 10^7, with `decimals` places (1..7).
// Integer arithmetic only. Returns the length written.
int degreesText(char* out, size_t n, int32_t v7, uint8_t decimals);

// One CSV row with its newline. utcEpoch 0 writes `ms` in the first column.
// fix null writes no position columns; otherwise three, filled only when the
// fix is valid and no older than Gnss::FRESH_MS at nowMs. Returns the length.
int line(char* out, size_t n, const Detection& d, uint32_t utcEpoch, uint32_t ms,
         const Gnss::Fix* fix, bool fake, uint32_t nowMs);

}  // namespace SdRow
