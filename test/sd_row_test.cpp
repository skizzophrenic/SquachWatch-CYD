// SquachWatch-CYD — the SD log's row, src/sd_row.cpp.
//
// A wrong sign puts a detection south of the equator or west of Greenwich on
// the other side of the map. A position from an old fix puts it where the
// board was a while ago. A position typed in on the bench must never read as
// a real one. Each of those is a plausible row on the card, and nothing on the
// board would show it.
#include "sd_row.h"
#include "test_util.h"
#include <cstdio>
#include <cstring>

static Detection sample() {
    Detection d{};
    const uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0x01, 0x02, 0x03};
    memcpy(d.mac, mac, 6);
    d.rssi = -61;
    d.channel = 6;
    d.type = DetectionType::FLOCK;
    d.vendor = "Flock";
    strcpy(d.name, "Falcon");
    return d;
}

// The text after the seventh comma: the position columns, newline included.
static const char* tail(const char* row) {
    int commas = 0;
    for (const char* p = row; *p; p++)
        if (*p == ',' && ++commas == 7) return p + 1;
    return nullptr;
}

static int commas(const char* s) {
    int n = 0;
    for (; *s; s++) if (*s == ',') n++;
    return n;
}

int main() {
    char row[SdRow::LINE_MAX];
    const Detection d = sample();

    suite("Seven columns without GPS");
    SdRow::line(row, sizeof row, d, 0, 71581, nullptr, false, 0);
    ck("today's format", strcmp(row, "71581,FLOCK,-61,AA:BB:CC:01:02:03,6,Flock,Falcon\n") == 0);
    ck("no trailing commas", commas(row) == 6);

    suite("Wall-clock time");
    SdRow::line(row, sizeof row, d, 1791289925, 71581, nullptr, false, 0);
    ck("UTC first, when the clock is trusted", strncmp(row, "2026-10-06T12:32:05Z,FLOCK,", 27) == 0);

    suite("Position from a fresh fix");
    Gnss::Fix f;
    f.valid = true;
    f.lat7 = -5000000;      // half a degree south
    f.lon7 = -1234567;      // an eighth of a degree west
    f.accM = 12;
    f.atMs = 10000;
    SdRow::line(row, sizeof row, d, 0, 71581, &f, false, 11000);
    const char* t = tail(row);
    ck("south and west keep their sign under one degree", t && strcmp(t, "-0.5000000,-0.1234567,12\n") == 0);
    ck("the first seven columns are unchanged", strncmp(row, "71581,FLOCK,-61,AA:BB:CC:01:02:03,6,Flock,Falcon,", 49) == 0);

    suite("A bench fix");
    SdRow::line(row, sizeof row, d, 0, 71581, &f, true, 11000);
    t = tail(row);
    ck("FAKE in place of the accuracy", t && strcmp(t, "-0.5000000,-0.1234567,FAKE\n") == 0);

    suite("A stale fix");
    SdRow::line(row, sizeof row, d, 0, 71581, &f, false, 10000 + 6000);
    t = tail(row);
    ck("three empty columns", t && strcmp(t, ",,\n") == 0);
    ck("the first seven columns are unchanged", strncmp(row, "71581,FLOCK,-61,AA:BB:CC:01:02:03,6,Flock,Falcon,", 49) == 0);
    Gnss::Fix none;
    SdRow::line(row, sizeof row, d, 0, 71581, &none, false, 11000);
    t = tail(row);
    ck("no fix at all is also empty", t && strcmp(t, ",,\n") == 0);

    suite("A comma in the name");
    Detection c = sample();
    strcpy(c.name, "Cam,Front");
    SdRow::line(row, sizeof row, c, 0, 71581, &f, false, 11000);
    ck("becomes a dot", strstr(row, ",Cam.Front,") != nullptr);
    t = tail(row);
    ck("the position stays in its own columns", t && strcmp(t, "-0.5000000,-0.1234567,12\n") == 0);

    return report();
}
