// SquachWatch-CYD — GNSS sentence parser. See gnss.h.
#include "gnss.h"
#include <string.h>
#include <stdlib.h>

namespace Gnss {

static bool s_phone = false;   // the fix came from a phone's FIX frame, not the UART

namespace {

Fix      s_fix;
uint32_t s_rmcEpoch = 0;       // date + time from the newest valid RMC
uint32_t s_rmcDay   = 0;       // the date alone, for dating a GGA time
bool     s_faked    = false;
uint32_t s_good = 0, s_bad = 0;

// In view and heard, per talker, from the GSV cycle in progress and the last
// complete one. A cycle is messages 1..n of one talker; message 1 restarts it.
enum { T_GP, T_GL, T_GA, T_GB, T_GQ, T_OTHER, T_N };
uint8_t s_viewDone[T_N], s_heardDone[T_N], s_heardNow[T_N];

char    s_line[96];
uint8_t s_len = 0;

int hexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

// Splits a sentence body (between '$' and '*') into comma fields, in place.
uint8_t split(char* body, char** f, uint8_t max) {
    uint8_t n = 0;
    f[n++] = body;
    for (char* p = body; *p && n < max; p++)
        if (*p == ',') { *p = 0; f[n++] = p + 1; }
    return n;
}

// ddmm.mmmm (or dddmm.mmmm) and a hemisphere to degrees x 10^7, exactly in
// integers: a float here would lose the metre the format carries.
bool coord(const char* v, const char* hemi, int32_t& out) {
    if (!v[0] || !hemi[0]) return false;
    const char* dot = strchr(v, '.');
    const int intLen = dot ? (int)(dot - v) : (int)strlen(v);
    if (intLen < 3) return false;
    long deg = 0;
    for (int i = 0; i < intLen - 2; i++) { if (v[i] < '0' || v[i] > '9') return false; deg = deg * 10 + (v[i] - '0'); }
    // Minutes as an integer in 10^-7 minute units.
    long long minE7 = 0;
    for (int i = intLen - 2; i < intLen; i++) minE7 = minE7 * 10 + (v[i] - '0');
    long long frac = 0; int digits = 0;
    if (dot) for (const char* p = dot + 1; *p && digits < 7; p++, digits++) {
        if (*p < '0' || *p > '9') return false;
        frac = frac * 10 + (*p - '0');
    }
    for (; digits < 7; digits++) frac *= 10;
    minE7 = minE7 * 10000000LL + frac;
    long long e7 = (long long)deg * 10000000LL + minE7 / 60;
    if (hemi[0] == 'S' || hemi[0] == 'W') e7 = -e7;
    out = (int32_t)e7;
    return true;
}

// hhmmss(.ss) to seconds of the day, or -1.
long timeOfDay(const char* v) {
    if (strlen(v) < 6) return -1;
    for (int i = 0; i < 6; i++) if (v[i] < '0' || v[i] > '9') return -1;
    const int h = (v[0] - '0') * 10 + (v[1] - '0');
    const int m = (v[2] - '0') * 10 + (v[3] - '0');
    const int s = (v[4] - '0') * 10 + (v[5] - '0');
    if (h > 23 || m > 59 || s > 60) return -1;
    return h * 3600L + m * 60L + s;
}

uint8_t talker(const char* id) {
    if (!strncmp(id, "GP", 2)) return T_GP;
    if (!strncmp(id, "GL", 2)) return T_GL;
    if (!strncmp(id, "GA", 2)) return T_GA;
    if (!strncmp(id, "GB", 2) || !strncmp(id, "BD", 2)) return T_GB;
    if (!strncmp(id, "GQ", 2)) return T_GQ;
    return T_OTHER;
}

void onGga(char** f, uint8_t n, uint32_t nowMs) {
    if (n < 10) return;
    const int q = atoi(f[6]);
    s_fix.used = (uint8_t)atoi(f[7]);
    // No fix from the module: keep a fake or a phone's position rather than drop it.
    if (q <= 0) { s_fix.valid = (s_faked || s_phone) && s_fix.valid; return; }
    int32_t la, lo;
    if (!coord(f[2], f[3], la) || !coord(f[4], f[5], lo)) return;
    s_faked = false;
    s_phone = false;
    s_fix.valid = true;
    s_fix.lat7 = la; s_fix.lon7 = lo;
    s_fix.altM = (int16_t)atof(f[9]);
    const float hdop = atof(f[8]);
    int acc = (int)(hdop * 4.0f + 0.5f);
    s_fix.accM = (uint8_t)(acc < 1 ? 1 : acc > 255 ? 255 : acc);
    s_fix.atMs = nowMs;
    // The time of this fix, on the date the last RMC gave.
    const long tod = timeOfDay(f[1]);
    s_fix.epoch = (s_rmcDay && tod >= 0) ? s_rmcDay + (uint32_t)tod : 0;
}

void onRmc(char** f, uint8_t n) {
    if (n < 10 || f[2][0] != 'A') return;
    const long tod = timeOfDay(f[1]);
    const char* d = f[9];
    if (tod < 0 || strlen(d) != 6) return;
    for (int i = 0; i < 6; i++) if (d[i] < '0' || d[i] > '9') return;
    const int day = (d[0] - '0') * 10 + (d[1] - '0');
    const int mon = (d[2] - '0') * 10 + (d[3] - '0');
    const int yr  = 2000 + (d[4] - '0') * 10 + (d[5] - '0');
    if (mon < 1 || mon > 12 || day < 1 || day > 31) return;
    s_rmcDay   = toEpoch(yr, mon, day, 0, 0, 0);
    s_rmcEpoch = s_rmcDay + (uint32_t)tod;
}

void onGsv(const char* id, char** f, uint8_t n) {
    if (n < 4) return;
    const uint8_t t = talker(id);
    const int total = atoi(f[1]), num = atoi(f[2]);
    if (num == 1) s_heardNow[t] = 0;
    // Four satellites a message at most: PRN, elevation, azimuth, SNR. A
    // trailing signal-ID field (NMEA 4.10) makes the count odd; ignored.
    for (uint8_t k = 4; k + 3 < n; k += 4)
        if (f[k][0] && f[k + 3][0] && atoi(f[k + 3]) > 0) s_heardNow[t]++;
    if (num == total) {
        s_viewDone[t]  = (uint8_t)atoi(f[3]);
        s_heardDone[t] = s_heardNow[t];
    }
}

}  // namespace

bool sentence(const char* s, uint32_t nowMs) {
    if (!s || s[0] != '$') return false;
    char buf[96];
    size_t len = strlen(s);
    while (len && (s[len - 1] == '\r' || s[len - 1] == '\n')) len--;
    if (len < 9 || len >= sizeof buf) { s_bad++; return false; }
    memcpy(buf, s, len); buf[len] = 0;
    char* star = strrchr(buf, '*');
    if (!star || star[1] == 0 || star[2] == 0) { s_bad++; return false; }
    const int h1 = hexVal(star[1]), h2 = hexVal(star[2]);
    uint8_t sum = 0;
    for (char* p = buf + 1; p < star; p++) sum ^= (uint8_t)*p;
    if (h1 < 0 || h2 < 0 || sum != (uint8_t)(h1 * 16 + h2)) { s_bad++; return false; }
    *star = 0;
    s_good++;
    char* f[24];
    const uint8_t n = split(buf + 1, f, 24);
    const char* type = f[0] + 2;           // after the talker
    if (strlen(f[0]) != 5) return true;
    if (!strcmp(type, "GGA")) onGga(f, n, nowMs);
    else if (!strcmp(type, "RMC")) onRmc(f, n);
    else if (!strcmp(type, "GSV")) onGsv(f[0], f, n);
    return true;
}

void feed(char c, uint32_t nowMs) {
    if (c == '$') s_len = 0;
    if (c == '\n' || c == '\r') {
        if (s_len) { s_line[s_len] = 0; sentence(s_line, nowMs); }
        s_len = 0;
        return;
    }
    if (s_len < sizeof s_line - 1) s_line[s_len++] = c;
    else s_len = 0;                          // overlong: not NMEA, drop it
}

const Fix& fix() { return s_fix; }

Sky sky() {
    Sky k;
    for (uint8_t t = 0; t < T_N; t++) { k.view += s_viewDone[t]; k.heard += s_heardDone[t]; }
    return k;
}

uint32_t utcEpoch() { return s_rmcEpoch; }
uint32_t good() { return s_good; }
uint32_t bad()  { return s_bad; }

// Signed, because `nowMs` is often a frame's start and the fix can land later
// in the same frame: unsigned, a fix 3 ms "in the future" wrapped to an age of
// 49 days and read as stale -- once a second, on the second the GPS spoke,
// which flickered GPS SEARCHING over the LORA page's GPS line (2026-10-16).
bool fresh(uint32_t nowMs) { return s_fix.valid && (int32_t)(nowMs - s_fix.atMs) < (int32_t)(s_phone ? PHONE_FRESH_MS : FRESH_MS); }

void reset() {
    s_fix = Fix();
    s_rmcEpoch = s_rmcDay = 0;
    s_faked = false;
    s_phone = false;
    s_good = s_bad = 0;
    memset(s_viewDone, 0, sizeof s_viewDone);
    memset(s_heardDone, 0, sizeof s_heardDone);
    memset(s_heardNow, 0, sizeof s_heardNow);
    s_len = 0;
}

uint32_t toEpoch(int y, int m, int d, int h, int mi, int s) {
    // Howard Hinnant's days_from_civil.
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (unsigned)((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const long days = (long)era * 146097 + (long)doe - 719468;
    return (uint32_t)(days * 86400L + h * 3600L + mi * 60L + s);
}

void fake(int32_t lat7, int32_t lon7, uint32_t epoch, uint32_t nowMs) {
    s_faked = true;
    s_phone = false;
    s_fix.valid = true;
    s_fix.lat7 = lat7; s_fix.lon7 = lon7;
    s_fix.altM = 0; s_fix.accM = 50; s_fix.used = 0;
    s_fix.epoch = epoch;
    s_fix.atMs = nowMs;
}
bool faked() { return s_faked && s_fix.valid; }

void phone(int32_t lat7, int32_t lon7, uint8_t accM, uint32_t epoch, uint32_t nowMs) {
    if (s_fix.valid && !s_phone && !s_faked && (int32_t)(nowMs - s_fix.atMs) < (int32_t)FRESH_MS) return;   // our own GPS wins
    s_phone = true;
    s_faked = false;
    s_fix.valid = true;
    s_fix.lat7 = lat7; s_fix.lon7 = lon7;
    s_fix.altM = 0; s_fix.accM = accM ? accM : 1; s_fix.used = 0;
    s_fix.epoch = epoch;
    s_fix.atMs = nowMs;
}

bool fromPhone() { return s_phone && s_fix.valid; }
#if defined(WAVESHARE_206)
void phoneAlt(int16_t altM) { if (s_phone) s_fix.altM = altM; }
#endif

}  // namespace Gnss
