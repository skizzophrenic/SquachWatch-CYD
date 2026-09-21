#pragma once
#include <stdint.h>
#include "state.h"

namespace Cardputer {
// Keep the policy independent of the hardware so timeout/wrap and parsing
// can be exercised on the host as well as on the board.
inline bool dimDue(uint32_t now, uint32_t lastInput, uint16_t seconds) {
    return seconds && uint32_t(now - lastInput) >= uint32_t(seconds) * 1000;
}
inline uint32_t rate(uint32_t current, uint32_t previous, uint32_t elapsed) {
    return elapsed ? uint64_t(uint32_t(current - previous)) * 1000 / elapsed : 0;
}
inline bool parseEpoch(const char* text, uint32_t& value) {
    uint32_t result = 0;
    if (!text || !*text) return false;
    for (; *text; ++text) {
        if (*text < '0' || *text > '9') return false;
        const uint32_t digit = *text - '0';
        if (result > (UINT32_MAX - digit) / 10) return false;
        result = result * 10 + digit;
    }
    // Same lower bound as Clock; 32-bit time_t must not wrap in 2038.
    if (result <= 1735689600UL || result > 2147483647UL) return false;
    value = result;
    return true;
}
inline bool mayAlert(const Detection& d, Confidence minimum, bool silenced) {
    return !d.restored && d.active && d.conf >= minimum && !silenced;
}

// DetectionEngine::latest() is retained between loops. A rising sighting
// is identified by MAC + type + firstSeen, not by a changing RSSI/hit count.
class Sightings {
public:
    bool take(const Detection* d) {
        if (!d || d->restored || !d->active) return false;
        bool same = valid && stamp == d->firstSeen && type == d->type;
        for (unsigned i = 0; i < 6; ++i) same = same && mac[i] == d->mac[i];
        if (same) return false;
        valid = true; stamp = d->firstSeen; type = d->type;
        for (unsigned i = 0; i < 6; ++i) mac[i] = d->mac[i];
        return true;
    }
private:
    bool valid = false;
    uint32_t stamp = 0;
    DetectionType type = DetectionType::UNKNOWN;
    uint8_t mac[6] = {};
};
}
