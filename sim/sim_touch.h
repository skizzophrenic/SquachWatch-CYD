// SquachWatch-Sim — injected touch state.
//
// The firmware's pollTouch() reads the XPT2046 driver and maps raw ADC
// values into screen coordinates itself (rotation, axis swap, flip, the
// clamp/sanity rules). To exercise that real code rather than bypass
// it, the harness injects values in *raw driver space* and lets
// pollTouch() do its own conversion -- so gesture handling, calibration
// mapping and clamping all run exactly as they do on hardware.
//
// The screen->raw inversion lives in the harness (main_live.cpp), which
// has the screen dimensions and rotation to do it with; the shimmed
// driver below just hands back whatever was put here.
#pragma once
#include <cstdint>

namespace SimTouch {
    inline bool     down = false;   // is a touch currently held
    inline uint16_t rawX = 0;
    inline uint16_t rawY = 0;
}
