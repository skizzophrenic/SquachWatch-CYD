#pragma once
#include <math.h>

namespace RoundLayout {
constexpr int SIZE = 466;
constexpr int HEADER = 72;
constexpr int FOOTER = 122;

// The narrowest chord occupied by a whole rectangular control, with a
// twelve-pixel clearance from the glass. Coordinates and glyphs stay 1:1.
inline int inset(int y, int height, int width = SIZE, int padding = 12) {
#if defined(M5_STOPWATCH)
    if (width != SIZE) return 0;
    const float center = (SIZE - 1) * 0.5f;
    const float radius = SIZE * 0.5f - padding;
    float d = fabsf(y - center);
    const float end = fabsf(y + height - 1 - center);
    if (end > d) d = end;
    if (d >= radius) return SIZE / 2;
    return (int)ceilf(center - sqrtf(radius * radius - d * d));
#else
    (void)y; (void)height; (void)width; (void)padding;
    return 0;
#endif
}
template<class Key>
inline void keys(Key* keys, int count, int width) {
#if defined(M5_STOPWATCH)
    if (width != SIZE) return;
    for (int i = 0; i < count; ) {
        int end = i + 1;
        while (end < count && keys[end].y == keys[i].y) ++end;
        const int left = keys[i].x;
        const int span = keys[end - 1].x + keys[end - 1].w - left;
        const int margin = inset(keys[i].y, keys[i].h, width);
        const int room = width - 2 * margin;
        for (int j = i; j < end; ++j) {
            const int x0 = margin + (keys[j].x - left) * room / span;
            const int x1 = margin + (keys[j].x + keys[j].w - left) * room / span;
            keys[j].x = x0; keys[j].w = x1 - x0;
        }
        i = end;
    }
#else
    (void)keys; (void)count; (void)width;
#endif
}
}
