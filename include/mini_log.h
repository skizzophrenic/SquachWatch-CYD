#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
namespace MiniLog {
// Coarse signal bands, not a distance measurement. Antennas and transmit
// power differ between devices; RSSI orders received strength only.
inline uint8_t bars(int rssi) {
    return rssi >= -50 ? 4 : rssi >= -65 ? 3 : rssi >= -80 ? 2 : rssi >= -90 ? 1 : 0;
}
inline void age(uint32_t now, uint32_t seen, char* out, size_t n) {
    // A radio task may timestamp a record just after the UI snapshots now.
    int32_t elapsed = int32_t(now - seen);
    uint32_t seconds = elapsed > 0 ? uint32_t(elapsed) / 1000 : 0;
    if (seconds / 60 < 1000)
        snprintf(out, n, "%lu:%02lu", (unsigned long)(seconds / 60), (unsigned long)(seconds % 60));
    else if (seconds / 3600 < 100)
        snprintf(out, n, "%luh%02lum", (unsigned long)(seconds / 3600), (unsigned long)(seconds / 60 % 60));
    else snprintf(out, n, "%lud%02luh", (unsigned long)(seconds / 86400), (unsigned long)(seconds / 3600 % 24));
}
// Sort a view index only; preserve the engine's log order and readable page.
// Equal signals retain original order for predictable navigation.
template<class Engine>
uint8_t strongestFirst(const Engine& engine, uint8_t* order, size_t capacity) {
    unsigned count = engine.logCount();
    if (count > capacity) count = capacity;
    for (unsigned i = 0; i < count; ++i) {
        unsigned j = i;
        const auto* row = engine.logAt(i);
        while (j && row->rssi > engine.logAt(order[j-1])->rssi) {
            order[j] = order[j-1]; --j;
        }
        order[j] = i;
    }
    return uint8_t(count);
}
}
