// SquachWatch-CYD — places and the trail.
//
// What this guards: a place begins with the first network heard and ends
// only when its fingerprint is gone for two checks and there is something new
// to fingerprint; a GPS fix 300 m away is a new place on its own; a device
// heard at every one of three stops over a quarter of an hour is FOLLOWING,
// one heard at two of five is not; the signal ring keeps the last minute;
// AirPods are not AirTags; the status byte reads out of the right slot.
#include "test_util.h"
#include "places.h"
#include "trail.h"
#include "signatures.h"
#include <cstring>

static void bssid(uint8_t* m, uint8_t n) { memset(m, 0, 6); m[0] = 0x02; m[5] = n; }
// Eight networks of a room, heard at once.
static void room(uint8_t base, uint32_t now) {
    for (uint8_t i = 0; i < 8; i++) { uint8_t m[6]; bssid(m, (uint8_t)(base + i)); Places::noteBssid(m, (int8_t)(-50 - i), "", now); }
}
static bool homeNet(const char* s) { return strcmp(s, "HomeNet") == 0; }

int main() {
    Places::begin();
    Places::setHomeCheck(homeNet);
    uint32_t now = 1000;

    suite("The first network starts the first place");
    room(0x10, now);
    ck("place 1", Places::current() == 1);
    ck("not home yet", !Places::isHome(1));
    { uint8_t m[6]; bssid(m, 0x10); Places::noteBssid(m, -40, "HomeNet", now); }
    ck("the saved network makes it home", Places::isHome(1));

    suite("The same room, checked for an hour, stays one place");
    for (int k = 0; k < 240; k++) { now += 15000; room(0x10, now); Places::tick(now); }
    ck("still place 1", Places::current() == 1);

    suite("A new set of networks is a new place, after two checks");
    int checks = 0;
    for (; checks < 6 && Places::current() == 1; checks++) { now += 15000; room(0x40, now); Places::tick(now); }
    ck("not on the first check", checks > 1);
    ck("place 2 within a minute", Places::current() == 2 && checks <= 4);
    ck("place 2 is not home", !Places::isHome(2));

    suite("A lift with nothing to hear does not end a place");
    now += 15000; Places::tick(now); now += 15000; Places::tick(now); now += 15000; Places::tick(now);
    ck("no networks: still place 2", Places::current() == 2);

    suite("GPS: 300 m away is a new place");
    Places::noteFix(400000000, -740000000, now);
    ck("first fix stays put", Places::current() == 2 && Places::hasPos(2));
    Places::noteFix(400010000, -740000000, now);          // ~111 m north
    ck("111 m: same place", Places::current() == 2);
    Places::noteFix(400040000, -740000000, now);          // ~444 m
    ck("444 m: place 3", Places::current() == 3 && Places::hasPos(3));
    const uint16_t d = Places::distanceM(2, 3);
    ck("about 444 m apart", d > 430 && d < 460);
    ck("no position, no distance", Places::distanceM(1, 2) == 0);

    suite("The trail: a tag heard at every stop is FOLLOWING");
    Places::reset(); Places::setHomeCheck(homeNet);
    Detection tag; memset(&tag, 0, sizeof tag);
    now = 60000;                                   // minute 1
    Places::forceNew(now, true);                   // home
    Trail::noteSighting(tag, -60, now);
    ck("one stop", tag.stops == 1 && tag.stopPlace[0] == 1);
    ck("not following on one stop", !Trail::following(tag));
    now += 8 * 60000; Places::forceNew(now);       // minute 9: place 2
    Trail::noteSighting(tag, -62, now);
    now += 9 * 60000; Places::forceNew(now);       // minute 18: place 3
    Trail::noteSighting(tag, -58, now);
    ck("three stops", tag.stops == 3);
    ck("span is 17 min", Trail::spanMin(tag) == 17);
    ck("at every stop", Trail::atEveryStop(tag));
    ck("FOLLOWING", Trail::following(tag));

    suite("A tag heard at two of five stops is not");
    Detection bystander; memset(&bystander, 0, sizeof bystander);
    Places::reset();
    now = 60000; Places::forceNew(now); Trail::noteSighting(bystander, -70, now);
    now += 5 * 60000; Places::forceNew(now);       // not heard here
    now += 5 * 60000; Places::forceNew(now);       // nor here
    now += 5 * 60000; Places::forceNew(now);       // nor here
    now += 5 * 60000; Places::forceNew(now); Trail::noteSighting(bystander, -66, now);
    ck("two stops", bystander.stops == 2);
    ck("five places passed", Places::countBetweenMin(1, 21) == 5);
    ck("not at every stop", !Trail::atEveryStop(bystander));
    ck("not following", !Trail::following(bystander));

    suite("Three stops too quickly is not following either");
    Detection quick; memset(&quick, 0, sizeof quick);
    Places::reset();
    now = 60000; Places::forceNew(now); Trail::noteSighting(quick, -60, now);
    now += 60000; Places::forceNew(now); Trail::noteSighting(quick, -60, now);
    now += 60000; Places::forceNew(now); Trail::noteSighting(quick, -60, now);
    ck("three stops in two minutes: no", quick.stops == 3 && !Trail::following(quick));

    suite("The signal ring keeps the last readings, four seconds apart");
    Detection r; memset(&r, 0, sizeof r);
    now = 100000;
    for (int i = 0; i < 20; i++) { Trail::noteSighting(r, (int8_t)(-80 + 2 * i), now); now += 4100; }
    ck("full", r.sparkCount == Detection::SPARK_N);
    ck("newest is the last reading", Trail::sparkAt(r, 0) == -42);
    ck("oldest is SPARK_N back", Trail::sparkAt(r, Detection::SPARK_N - 1) == -42 - 2 * (Detection::SPARK_N - 1));
    ck("getting closer", Trail::trend(r) == 1);
    Detection same; memset(&same, 0, sizeof same);
    Trail::noteSighting(same, -60, 200000); Trail::noteSighting(same, -59, 200500);
    ck("two readings in one second are one sample", same.sparkCount == 1);

    suite("A fourth stop drops the oldest");
    Detection four; memset(&four, 0, sizeof four);
    Places::reset(); now = 60000;
    for (int i = 0; i < 4; i++) { Places::forceNew(now); Trail::noteSighting(four, -60, now); now += 6 * 60000; }
    ck("three kept", four.stops == 3);
    ck("they are places 2, 3, 4", four.stopPlace[0] == 2 && four.stopPlace[2] == 4);

    suite("AirPods are not AirTags");
    static const uint8_t AIRPODS[] = { 0x1E, 0xFF, 0x4C, 0x00, 0x07, 0x19, 0x01, 0x0E, 0x20, 0x2B, 0xB8, 0x8F, 0x00, 0x00, 0x00, 0x00 };
    static const uint8_t AIRTAG[]  = { 0x1E, 0xFF, 0x4C, 0x00, 0x12, 0x19, 0x10, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11, 0x22 };
    static const uint8_t AIRTAG_WITH[] = { 0x1E, 0xFF, 0x4C, 0x00, 0x12, 0x19, 0x30, 0xAA, 0xBB };
    ck("proximity pairing is not a tag", !isAirTagPayload(AIRPODS, sizeof AIRPODS));
    ck("offline finding is", isAirTagPayload(AIRTAG, sizeof AIRTAG));
    ck("status byte read", airTagStatusByte(AIRTAG, sizeof AIRTAG) == 0x10);
    ck("maintained bit", (airTagStatusByte(AIRTAG_WITH, sizeof AIRTAG_WITH) & 0x20) != 0);
    ck("no block, no byte", airTagStatusByte(AIRPODS, sizeof AIRPODS) == 0);
    static const uint8_t NEARBY[] = { 0x02, 0x01, 0x1A, 0x07, 0xFF, 0x4C, 0x00, 0x12, 0x02, 0x10, 0x07 };
    ck("the nearby frame is a tag too", isAirTagPayload(NEARBY, sizeof NEARBY));
    uint8_t st = 0xEE;
    ck("...read as frame 0x02", airTagFrame(NEARBY, sizeof NEARBY, &st) == 0x02 && st == 0x10);
    ck("the separated frame reads as 0x19", airTagFrame(AIRTAG, sizeof AIRTAG, &st) == 0x19 && st == 0x10);
    ck("proximity pairing is neither", airTagFrame(AIRPODS, sizeof AIRPODS, &st) == 0);
    // The bench capture of a tag that is not set up (2026-11-20).
    static const uint8_t UNPAIRED[] = { 0x1E, 0xFF, 0x4C, 0x00, 0x07, 0x19, 0x05, 0x00, 0x55, 0x10, 0x00, 0x00, 0x01, 0x17, 0x17, 0xCC, 0x48 };
    ck("an AirTag out of the box is a tag", isAirTagPayload(UNPAIRED, sizeof UNPAIRED));
    ck("...read as frame 0x07", airTagFrame(UNPAIRED, sizeof UNPAIRED, &st) == 0x07 && st == 0x05);
    static const uint8_t PODS2[] = { 0x1E, 0xFF, 0x4C, 0x00, 0x07, 0x19, 0x01, 0x0F, 0x20, 0x55, 0x00, 0x00 };
    ck("AirPods with the model where a tag's would be are still not", !isAirTagPayload(PODS2, sizeof PODS2));

    return report();
}
