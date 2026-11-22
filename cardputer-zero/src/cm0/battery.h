// SquachWatch-CM0 -- battery: the home screen's lava lamp, from the
// kernel's power_supply class.
//
// The firmware draws its battery gauge (ui_clear.cpp's lava lamp) only for
// a board that registers uiClearSetBatterySource(); the Cardputer ADV build
// registers none. On the Cardputer Zero the cell is behind a TI bq27220
// fuel gauge whose driver publishes /sys/class/power_supply/bq27220-0, so
// main_linux registers batteryState() after setup() -- firmware untouched.
#pragma once
#include <cstdint>

namespace cm0 {

// The BatterySource signature (ui_clear.h). False when no battery is
// present (the lamp is then not drawn, as on a board without a cell).
// `charging` is the lamp's "on the cable" mode, not strictly "charging":
// a full cell on USB reports "Not charging" / "Full" and still counts.
bool batteryState(uint8_t& pct, bool& charging);

// The status -> "on the cable" rule, for the unit test. `status` is the
// power_supply `status` text; `currentUa` the signed `current_now`
// (negative = discharging), used only when the status says Unknown.
bool batteryOnCable(const char* status, long currentUa);

}   // namespace cm0
