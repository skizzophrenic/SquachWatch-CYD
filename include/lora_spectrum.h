#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace LoraSpectrum {

template <size_t Cols, size_t Rows>
void advance(float (&heat)[Cols][Rows], uint8_t (&kind)[Cols][Rows],
             uint32_t& lastColumnAt, uint32_t now, uint32_t columnMs) {
    const uint32_t elapsed = now - lastColumnAt;
    // The page's setup can run after the loop sampled its frame timestamp.
    if (elapsed >= 0x80000000u || elapsed < columnMs) return;
    const uint32_t steps = elapsed / columnMs;
    lastColumnAt += steps * columnMs;
    if (steps >= Cols) {
        memset(heat, 0, sizeof heat);
        memset(kind, 0, sizeof kind);
        return;
    }

    const size_t kept = Cols - steps;
    memmove(heat, heat + steps, kept * sizeof heat[0]);
    memmove(kind, kind + steps, kept * sizeof kind[0]);
    memset(heat + kept, 0, steps * sizeof heat[0]);
    memset(kind + kept, 0, steps * sizeof kind[0]);
}

}
