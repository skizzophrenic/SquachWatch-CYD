#include "battery_status.h"
#include <cassert>
#include <cstdio>
using namespace BatteryStatus;
int main() {
    assert(classify(4200, 1800, true, 0, Level::UNKNOWN) == Level::CHARGING);
    assert(classify(4200, 1800, false, 0, Level::UNKNOWN) == Level::FULL);
    assert(classify(3600, 0, true, 0, Level::UNKNOWN) == Level::MID);
    assert(classify(3240, 0, false, 0, Level::MID) == Level::LOW_BATTERY);
    assert(classify(3270, 0, false, 0, Level::LOW_BATTERY) == Level::LOW_BATTERY);
    assert(classify(3300, 0, false, 0, Level::LOW_BATTERY) == Level::MID);
    assert(classify(3930, 0, false, 0, Level::FULL) == Level::FULL);
    assert(classify(3900, 0, false, 0, Level::FULL) == Level::MID);
    assert(classify(4200, 1800, true, 6, Level::FULL) == Level::UNKNOWN);
    assert(classify(0, 1800, true, 0, Level::FULL) == Level::UNKNOWN);
    assert(classify(4800, 1800, true, 0, Level::FULL) == Level::UNKNOWN);
    puts("battery_status_test PASS");
}
