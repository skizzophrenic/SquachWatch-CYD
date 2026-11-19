#pragma once
#include <TFT_eSPI.h>
#include "round_layout.h"

// Reflow one row into the available chord. The row's existing measurement,
// wrapping and right alignment see its actual width; nothing is stretched.
class RoundRow {
    TFT_eSPI& t;
    int& width;
    int oldWidth, vx, vy, vw, vh;
    bool datum = false, changed = false;
public:
    RoundRow(TFT_eSPI& display, int& w, int y, int h) : t(display), width(w), oldWidth(w) {
#if defined(M5_STOPWATCH)
        if (w != RoundLayout::SIZE || t.getViewportX() != 0) return;
        const int margin = RoundLayout::inset(y, h, w);
        vx = t.getViewportX(); vy = t.getViewportY();
        vw = t.getViewportWidth(); vh = t.getViewportHeight(); datum = t.getViewportDatum();
        width -= 2 * margin;
        t.setViewport(margin, 0, width, t.height(), true);
        changed = true;
#endif
    }
    ~RoundRow() {
        if (changed) { t.setViewport(vx, vy, vw, vh, datum); width = oldWidth; }
    }
    RoundRow(const RoundRow&) = delete;
    RoundRow& operator=(const RoundRow&) = delete;
};
