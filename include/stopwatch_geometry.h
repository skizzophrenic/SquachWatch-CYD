// Pure geometry shared by the display, touch reader and host tests.
#pragma once
#include <stdint.h>

namespace StopWatchGeometry {
constexpr int PANEL = 466;
constexpr int WIDTH = PANEL;
constexpr int HEIGHT = PANEL;
constexpr int LEFT = (PANEL - WIDTH) / 2;
constexpr int TOP = (PANEL - HEIGHT) / 2;
inline bool visible(int x, int y) {
    if (x < 0 || y < 0 || x >= PANEL || y >= PANEL) return false;
    const int dx = 2 * x - (PANEL - 1), dy = 2 * y - (PANEL - 1);
    return dx * dx + dy * dy < PANEL * PANEL;
}
inline int driverPadding(int extent) { return (extent - PANEL) / 2; }
inline bool toLogical(int px, int py, int16_t& x, int16_t& y,
                      int driverWidth = PANEL, int driverHeight = PANEL) {
    px -= driverPadding(driverWidth);
    py -= driverPadding(driverHeight);
    if (!visible(px, py)) return false;
    x = static_cast<int16_t>(px - LEFT);
    y = static_cast<int16_t>(py - TOP);
    return true;
}
}
