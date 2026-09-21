#include "cardputer_runtime.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>

int main() {
    using namespace Cardputer;
    assert(!dimDue(100000, 0, 0));
    assert(!dimDue(29999, 0, 30));
    assert(dimDue(30000, 0, 30));
    assert(dimDue(999, UINT32_MAX - 1000, 2));
    assert(!dimDue(998, UINT32_MAX - 1000, 2));
    assert(rate(20, 10, 500) == 20);
    assert(rate(4, UINT32_MAX - 5, 1000) == 10);
    assert(rate(1, 0, 0) == 0);
    uint32_t epoch = 123;
    assert(parseEpoch("1735689601", epoch) && epoch == 1735689601);
    assert(parseEpoch("2147483647", epoch));
    for (const char* bad : {"", "-1", "1735689600", "2147483648", "42949672960", "1790000000x", " 1790000000"}) {
        const uint32_t before = epoch;
        assert(!parseEpoch(bad, epoch) && epoch == before);
    }
    Sightings seen;
    Detection d = {};
    d.active = true; d.type = DetectionType::AIRTAG; d.conf = Confidence::MED_CONF;
    d.firstSeen = UINT32_MAX - 100; d.mac[5] = 1;
    assert(!seen.take(nullptr));
    d.restored = 1; assert(!seen.take(&d)); d.restored = 0;
    assert(seen.take(&d)); assert(!seen.take(&d));
    d.rssi = -30; d.hits++; assert(!seen.take(&d));
    d.firstSeen = 50; assert(seen.take(&d)); // return after millis wrap
    d.mac[5] = 2; assert(seen.take(&d));
    d.type = DetectionType::CAMERA; assert(seen.take(&d));
    assert(mayAlert(d, Confidence::MED_CONF, false));
    assert(!mayAlert(d, Confidence::HIGH_CONF, false));
    assert(!mayAlert(d, Confidence::LOW_CONF, true));
    d.active = false; assert(!mayAlert(d, Confidence::LOW_CONF, false));
    d.active = true; d.restored = 1; assert(!mayAlert(d, Confidence::LOW_CONF, false));
    puts("cardputer runtime: dim/wrap, rates, epoch bounds, sighting edges and alert policy passed");
}
