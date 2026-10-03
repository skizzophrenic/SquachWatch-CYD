// SquachWatch-CYD — the WigleWifi-1.6 file. See wardrive.h.
//
// Format: https://api.wigle.net/csvFormat.html
//   WigleWifi-1.6,appRelease=...,model=...,release=...,device=...,display=...,board=...,brand=...,star=Sol,body=3,subBody=0
//   MAC,SSID,AuthMode,FirstSeen,Channel,Frequency,RSSI,CurrentLatitude,CurrentLongitude,AltitudeMeters,AccuracyMeters,RCOIs,MfgrId,Type
#include "wardrive.h"
#include "wifi_auth.h"
#include <stdio.h>
#include <string.h>

namespace Wardrive {

namespace {

// Seven decimal places from degrees x 10^7, sign and all, with no float.
int coord(char* out, size_t n, int32_t e7) {
    const bool neg = e7 < 0;
    const uint32_t a = neg ? (uint32_t)(-(int64_t)e7) : (uint32_t)e7;
    return snprintf(out, n, "%s%lu.%07lu", neg ? "-" : "", (unsigned long)(a / 10000000u), (unsigned long)(a % 10000000u));
}

// A name as a CSV field. Quoted when it holds a comma or a quote, quotes
// doubled; control characters (a hidden SSID's zero bytes, a newline in a
// Bluetooth name) become nothing, since WiGLE has no use for them and a
// newline would end the row.
size_t field(const char* s, uint8_t len, char* out, size_t n) {
    bool quote = false;
    for (uint8_t i = 0; i < len; i++) if (s[i] == ',' || s[i] == '"') quote = true;
    size_t at = 0;
    if (quote && at + 1 < n) out[at++] = '"';
    for (uint8_t i = 0; i < len && at + 2 < n; i++) {
        const unsigned char c = (unsigned char)s[i];
        if (c < 0x20 || c == 0x7F) continue;
        if (c == '"') out[at++] = '"';
        out[at++] = (char)c;
    }
    if (quote && at + 1 < n) out[at++] = '"';
    out[at < n ? at : n - 1] = 0;
    return at;
}

}  // namespace

uint16_t channelMhz(uint8_t ch) {
    if (ch >= 1 && ch <= 13) return (uint16_t)(2407 + 5 * ch);
    if (ch == 14) return 2484;
#if SQW_WIFI_5G
    if (ch >= 32 && ch <= 177) return (uint16_t)(5000 + 5 * ch);   // 5 GHz, for WiGLE's frequency column
#endif
    return 0;
}

void utcStamp(uint32_t epoch, char* out, size_t n) {
    // Howard Hinnant's civil_from_days.
    long z = (long)(epoch / 86400u) + 719468;
    const uint32_t sod = epoch % 86400u;
    const long era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = (unsigned)(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long y = (long)yoe + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const unsigned d = doy - (153 * mp + 2) / 5 + 1;
    const unsigned m = mp < 10 ? mp + 3 : mp - 9;
    y += (m <= 2);
    snprintf(out, n, "%04ld-%02u-%02u %02lu:%02lu:%02lu", y, m, d,
             (unsigned long)(sod / 3600), (unsigned long)(sod / 60 % 60), (unsigned long)(sod % 60));
}

size_t headerLines(char* out, size_t n, const char* version, const char* board, const char* model) {
    const int w = snprintf(out, n,
        "WigleWifi-1.6,appRelease=%s,model=%s,release=%s,device=SquachWatch,display=SquachWatch,board=%s,brand=LilyGo,star=Sol,body=3,subBody=0\n"
        "MAC,SSID,AuthMode,FirstSeen,Channel,Frequency,RSSI,CurrentLatitude,CurrentLongitude,AltitudeMeters,AccuracyMeters,RCOIs,MfgrId,Type\n",
        version, model, version, board);
    return (w > 0 && (size_t)w < n) ? (size_t)w : 0;
}

size_t csvRow(const Record& r, char* out, size_t n) {
    char name[80], auth[96], when[24], lat[20], lon[20], mfgr[8] = "", freq[8] = "";
    const uint8_t nl = r.nameLen > sizeof r.name ? sizeof r.name : r.nameLen;
    field(r.name, nl, name, sizeof name);
    utcStamp(r.epoch, when, sizeof when);
    coord(lat, sizeof lat, r.lat7);
    coord(lon, sizeof lon, r.lon7);
    const bool ble = r.kind == KIND_BLE;
    if (ble) {
        // WiGLE's own app writes a Bluetooth LE device's capabilities this way.
        snprintf(auth, sizeof auth, "Misc [LE]");
        if (r.flags & F_HAS_MFGR) snprintf(mfgr, sizeof mfgr, "%u", (unsigned)r.auth);
    } else {
        WifiAuth::wigle(r.auth, auth, sizeof auth);
        const uint16_t mhz = channelMhz(r.channel);
        if (mhz) snprintf(freq, sizeof freq, "%u", (unsigned)mhz);
    }
    const int w = snprintf(out, n, "%02x:%02x:%02x:%02x:%02x:%02x,%s,%s,%s,%u,%s,%d,%s,%s,%d,%u,,%s,%s\n",
                           r.mac[0], r.mac[1], r.mac[2], r.mac[3], r.mac[4], r.mac[5],
                           name, auth, when, ble ? 0u : (unsigned)r.channel, freq, (int)r.rssi,
                           lat, lon, (int)r.altM, (unsigned)r.accM, mfgr, ble ? "BLE" : "WIFI");
    return (w > 0 && (size_t)w < n) ? (size_t)w : 0;
}

}  // namespace Wardrive
