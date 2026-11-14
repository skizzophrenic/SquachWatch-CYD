// SquachWatch-CYD — moving a frame into the Tab5's framebuffer.
//
// The CrowPanel's CrowBlit, with the quarter turn added. The app's 8-bit frame
// is landscape (640x360); the framebuffer is the glass as wired, 720 wide and
// 1280 tall. One logical column becomes TAB5_SCALE native rows, written
// sequentially (the cache-friendly direction for a buffer in PSRAM), and only
// the columns that changed since the last push are written, with a full
// push every so often so a missed hash can never leave a stale line.
//
// RGB332 -> RGB565 goes through a table built from FramePush::rgb332Wire(),
// so this board's colours are every other board's.
#pragma once
#include <stdint.h>

namespace Tab5Blit {
    // Builds the table. After Tab5Panel::begin().
    void begin();

    // Forget what the panel shows; the next push writes every column.
    void invalidate();

    // Ships h rows of w 8-bit pixels from src into the framebuffer. x,y are
    // in the app's logical coordinates. Returns false only if the panel is
    // not up.
    bool push(const uint8_t* src, int32_t w, int32_t h, int32_t x, int32_t y);

    // Native rows actually written by the last push, for the frame profiler.
    int32_t lastRows();
}
