// SquachWatch-CM0 -- battery unit test: the power_supply status -> the
// lava lamp's "on the cable" mode. Status strings are the kernel's
// (drivers/power/supply/power_supply_sysfs.c, POWER_SUPPLY_STATUS_*).
#include "battery.h"

#include <cstdio>

static int g_failures = 0, g_checks = 0;
#define CHECK(cond, msg) do { \
    g_checks++; \
    if (cond) std::printf("ok  : %s\n", msg); \
    else { std::printf("FAIL: %s\n", msg); g_failures++; } \
} while (0)

int main() {
    CHECK(cm0::batteryOnCable("Charging", -5),        "Charging -> on the cable");
    CHECK(cm0::batteryOnCable("Full", 0),             "Full -> on the cable");
    CHECK(cm0::batteryOnCable("Not charging", 0),     "Not charging (full on USB, as the CM0 reads) -> on the cable");
    CHECK(!cm0::batteryOnCable("Discharging", 250000), "Discharging -> on battery, whatever the current says");
    CHECK(cm0::batteryOnCable("Unknown", 120000),     "Unknown + positive current -> on the cable");
    CHECK(!cm0::batteryOnCable("Unknown", -300000),   "Unknown + negative current -> on battery");
    CHECK(!cm0::batteryOnCable("", 0),                "empty status, no current -> on battery");
    CHECK(!cm0::batteryOnCable(nullptr, 5),           "null status -> on battery");
    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
