// SquachWatch-CYD — moving a frame into the CrowPanel 7's framebuffer.
//
// The replacement for FramePush on this board. FramePush's transport pokes
// the ESP32's SPI registers directly; there is no SPI in this display path,
// so the transport goes and the idea stays: hash each row, send only the rows
// that changed, and force a full push every so often so a missed hash can
// never leave a stale line on the panel forever.
//
// The framebuffer is 16-bit and the app's frame is 8-bit RGB332, so this also
// does the depth conversion, through a 256-entry table built at boot from
// FramePush::rgb332Wire() — the library's own arithmetic, so the two can
// never drift apart and this board's colours stay identical to every other
// board's.
#pragma once
#include <stdint.h>

namespace CrowBlit {
    // Builds the lookup table. After crowPanelBegin().
    void begin();

    // Forget what the panel shows; the next push sends every row.
    void invalidate();

    // Ships h rows of w 8-bit pixels from src into the framebuffer. x,y are
    // in the app's logical coordinates. Returns false only if the panel is
    // not up.
    bool push(const uint8_t* src, int32_t w, int32_t h, int32_t x, int32_t y);

    // Rows actually written by the last push, for the frame profiler.
    int32_t lastRows();
}
