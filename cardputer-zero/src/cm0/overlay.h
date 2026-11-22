// SquachWatch-CM0 -- the harness's own overlay: the ESC-hold exit modal.
//
// Holding ESC for ESC_EXIT_MS exits the app (main_linux.cpp). Once it has
// been held for EXIT_HINT_MS a modal says so -- "HOLD ESC TO EXIT" -- with a
// bar filling towards the exit; letting go removes it. A tap (ESC is also
// the firmware's BACK key) is shorter than the delay and shows nothing.
//
// It is drawn on a COPY of the firmware's frame at output time, never into
// tft's buffer: the firmware pushes only the rows that changed, so anything
// drawn there would stay on screen after the modal closed.
#pragma once
#include <cstdint>
#include <vector>

namespace cm0 {

const uint32_t EXIT_HINT_MS = 250;

// `in` (w x h RGB565) with the modal on top, into `out`. `progress` 0..1 is
// how far the hold is towards the exit.
void composeExitHint(const std::vector<uint16_t>& in, int w, int h, float progress,
                     std::vector<uint16_t>& out);

}   // namespace cm0
