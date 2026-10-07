// SquachWatch-CYD: the CYD GPS builds' repeat table, src/wardrive_seen.cpp.
//
// What this guards: without it, a driver parked beside a cafe could upload
// hundreds of copies of the cafe's network, or a board that has passed 256
// devices could stop recording anything new.
#include "wardrive_capture.h"
#include "wardrive.h"
#include "test_util.h"

using Wardrive::Seen;
using Wardrive::SeenTable;
using Wardrive::seenWorthWriting;

static void mac(uint8_t* m, uint32_t k) { m[0] = 0x02; m[1] = 0xAA; m[2] = (uint8_t)(k >> 24); m[3] = (uint8_t)(k >> 16); m[4] = (uint8_t)(k >> 8); m[5] = (uint8_t)k; }

static void fresh(SeenTable& t) {
    for (uint8_t b = 0; b < SeenTable::BLOCKS; b++) { delete[] t.block[b]; t.block[b] = new Seen[SeenTable::PER](); }
}

int main() {
    const uint32_t T0 = 1790553600u;             // 2026-09-28 00:00 UTC
    const int32_t LA = 407128000, LO = -740060000;
    const uint8_t W = Wardrive::KIND_WIFI, B = Wardrive::KIND_BLE;
    SeenTable t = {};
    uint8_t m[6];

    suite("Standing still");
    fresh(t);
    mac(m, 1);
    int rows = 0;
    for (int i = 0; i < 10; i++) rows += seenWorthWriting(t, W, m, LA, LO, T0 + (uint32_t)i * 10) ? 1 : 0;
    ck("ten hearings in a minute from one spot: one row", rows == 1);

    suite("Moving, or waiting");
    ck("about 67 m away: a row", seenWorthWriting(t, W, m, LA + 6000, LO, T0 + 100));
    ck("same spot, 301 s after that row: a row", seenWorthWriting(t, W, m, LA + 6000, LO, T0 + 401));
    ck("same spot, soon after: no row", !seenWorthWriting(t, W, m, LA + 6000, LO, T0 + 402));

    suite("Bluetooth and WiFi kept apart");
    ck("the same address over Bluetooth: its own row", seenWorthWriting(t, B, m, LA + 6000, LO, T0 + 403));
    ck("and then a repeat of its own", !seenWorthWriting(t, B, m, LA + 6000, LO, T0 + 404));

    suite("A full table");
    fresh(t);
    int first = 0, again = 0;
    for (uint32_t k = 0; k < 256; k++) { mac(m, 1000 + k); first += seenWorthWriting(t, W, m, LA, LO, T0 + k) ? 1 : 0; }
    for (uint32_t k = 0; k < 256; k++) { mac(m, 1000 + k); again += seenWorthWriting(t, W, m, LA, LO, T0 + 260) ? 1 : 0; }
    ck("256 devices: 256 rows", first == 256);
    ck("all 256 heard again at once: no rows", again == 0);

    suite("Past 256, the oldest goes");
    // Device 1000 was written at T0, 1001 at T0 + 1: the oldest two. The
    // second pass did not move them, since it wrote nothing.
    mac(m, 5000);
    ck("a 257th device: a row", seenWorthWriting(t, W, m, LA, LO, T0 + 270));
    mac(m, 1001);
    ck("the second-oldest heard again: still no row", !seenWorthWriting(t, W, m, LA, LO, T0 + 271));
    mac(m, 1000);
    ck("the oldest heard again: a row, it was replaced", seenWorthWriting(t, W, m, LA, LO, T0 + 272));

    for (uint8_t b = 0; b < SeenTable::BLOCKS; b++) delete[] t.block[b];
    return report();
}
