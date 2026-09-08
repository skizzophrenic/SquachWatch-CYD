// iBeacon matcher — isIBeacon() in src/signatures.cpp
//
// The header is fixed by Apple, so the match is exact and the interesting
// half of the testing is the near-misses: this fires on Apple's company
// ID, which every Apple device in the room also advertises.
#include "signatures.h"
#include "test_util.h"
#include <cstring>

int main() {
    // A textbook advert: Apple, type 02, length 15, then 21 bytes.
    uint8_t ib[25] = {
        0x4C, 0x00,             // Apple, little endian
        0x02, 0x15,             // iBeacon, 21 bytes to follow
        0xB9, 0x40, 0x7F, 0x30, 0xF5, 0xF8, 0x46, 0x6E,
        0xAF, 0xF9, 0x25, 0x55, 0x6B, 0x57, 0xFE, 0x6D,   // proximity UUID
        0x00, 0x0A,             // major 10
        0x00, 0x2A,             // minor 42
        0xC5                    // measured power
    };

    suite("iBeacon");
    ck("textbook advert matches", isIBeacon(ib, sizeof(ib)));

    // Major and minor are big endian INSIDE the block, while the company
    // ID two bytes earlier is little endian. Reading them the wrong way
    // round still detects the beacon and silently reports the wrong unit,
    // so it is worth pinning here rather than noticing in the field.
    ck("major reads 10 big-endian", (unsigned)((ib[20] << 8) | ib[21]) == 10);
    ck("minor reads 42 big-endian", (unsigned)((ib[22] << 8) | ib[23]) == 42);

    suite("Near misses");
    uint8_t t[25];

    memcpy(t, ib, 25); t[0] = 0x4D;
    ck("wrong company ID low byte", !isIBeacon(t, 25));
    memcpy(t, ib, 25); t[1] = 0x01;
    ck("wrong company ID high byte", !isIBeacon(t, 25));
    memcpy(t, ib, 25); t[2] = 0x01;
    ck("wrong beacon type byte", !isIBeacon(t, 25));
    memcpy(t, ib, 25); t[3] = 0x14;
    ck("wrong inner length byte", !isIBeacon(t, 25));

    memcpy(t, ib, 25);
    ck("24 bytes — one short", !isIBeacon(t, 24));
    ck("4 bytes — header only", !isIBeacon(t, 4));
    ck("empty", !isIBeacon(t, 0));
    ck("null", !isIBeacon(nullptr, 25));

    suite("Other Apple adverts must not match");
    // These are the ones that actually turn up: continuity and Find My
    // both lead with Apple's company ID and a type byte that is not 0x02,
    // which is exactly what the check keys on.
    uint8_t continuity[25] = { 0x4C, 0x00, 0x07, 0x19, 0x01, 0x0E, 0x20 };
    ck("continuity advert (AirPods and friends)", !isIBeacon(continuity, 25));
    uint8_t findmy[25] = { 0x4C, 0x00, 0x12, 0x19, 0x00 };
    ck("Find My advert", !isIBeacon(findmy, 25));
    uint8_t nearby[25] = { 0x4C, 0x00, 0x10, 0x05, 0x01 };
    ck("nearby-info advert", !isIBeacon(nearby, 25));

    return report();
}
