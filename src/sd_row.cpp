// SquachWatch-CYD: the SD log's file name and CSV row. See sd_row.h.
#include "sd_row.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

namespace SdRow {

bool fileName(char* out, size_t n, uint32_t localDay) {
    int len;
    if (localDay == 0) {
        len = snprintf(out, n, "/squachwatch-nodate.log");
    } else {
        time_t t = (time_t)localDay * 86400;
        struct tm tm;
        gmtime_r(&t, &tm);
        len = snprintf(out, n, "/squachwatch-%04d%02d%02d.log",
                       tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    }
    return len > 0 && (size_t)len < n;
}

int degreesText(char* out, size_t n, int32_t v7, uint8_t decimals) {
    if (decimals < 1) decimals = 1;
    if (decimals > 7) decimals = 7;
    // The sign comes from the value itself: -0.5 has a whole part of 0, and
    // integer division would print it without the minus.
    const bool neg = v7 < 0;
    const uint32_t a = neg ? (uint32_t)(-(int64_t)v7) : (uint32_t)v7;
    uint32_t div = 1;
    for (uint8_t i = decimals; i < 7; i++) div *= 10;
    return snprintf(out, n, "%s%lu.%0*lu", neg ? "-" : "",
                    (unsigned long)(a / 10000000UL), (int)decimals,
                    (unsigned long)((a % 10000000UL) / div));
}

// Copies at most n-1 characters, with commas turned into dots so a vendor or
// a name never shifts the columns after it.
static void safeCopy(char* out, size_t n, const char* in) {
    size_t i = 0;
    for (; in && in[i] && i + 1 < n; i++) out[i] = in[i] == ',' ? '.' : in[i];
    out[i] = 0;
}

int line(char* out, size_t n, const Detection& d, uint32_t utcEpoch, uint32_t ms,
         const Gnss::Fix* fix, bool fake, uint32_t nowMs) {
    char when[24];
    if (utcEpoch) {
        time_t t = (time_t)utcEpoch;
        struct tm tm;
        gmtime_r(&t, &tm);
        snprintf(when, sizeof when, "%04d-%02d-%02dT%02d:%02d:%02dZ",
                 tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                 tm.tm_hour, tm.tm_min, tm.tm_sec);
    } else {
        snprintf(when, sizeof when, "%lu", (unsigned long)ms);
    }
    char mac[18];
    snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
             d.mac[0], d.mac[1], d.mac[2], d.mac[3], d.mac[4], d.mac[5]);
    // The same widths as the row always had: 11 characters of vendor, 19 of name.
    char vendorSafe[12], nameSafe[20];
    safeCopy(vendorSafe, sizeof vendorSafe, vendorText(d));
    safeCopy(nameSafe, sizeof nameSafe, d.name);

    int len = snprintf(out, n, "%s,%s,%d,%s,%u,%s,%s",
                       when, detectionTypeName(d.type), d.rssi, mac,
                       d.channel, vendorSafe, nameSafe);
    if (len < 0) return 0;
    if ((size_t)len >= n) len = (int)n - 1;

    if (fix) {
        char lat[16] = "", lon[16] = "", acc[8] = "";
        if (fix->valid && (uint32_t)(nowMs - fix->atMs) <= Gnss::FRESH_MS) {
            degreesText(lat, sizeof lat, fix->lat7, 7);
            degreesText(lon, sizeof lon, fix->lon7, 7);
            if (fake) snprintf(acc, sizeof acc, "FAKE");
            else      snprintf(acc, sizeof acc, "%u", fix->accM);
        }
        int k = snprintf(out + len, n - len, ",%s,%s,%s", lat, lon, acc);
        if (k > 0) len += k;
        if ((size_t)len >= n) len = (int)n - 1;
    }
    int k = snprintf(out + len, n - len, "\n");
    if (k > 0) len += k;
    if ((size_t)len >= n) len = (int)n - 1;
    return len;
}

}  // namespace SdRow
