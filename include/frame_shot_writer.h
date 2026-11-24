#pragma once
#include <stddef.h>
#include <stdint.h>

namespace FrameShot {

// Native USB may stop draining even while the port is open. The caller keeps
// the serial TX timeout at zero and uses the same start time for the whole shot.
template <typename Port, typename Now, typename Idle>
bool write(Port& port, const char* data, size_t len, uint32_t startedAt, Now now, Idle idle) {
    constexpr uint32_t maxShotMs = 12000;
    constexpr uint32_t maxIdleMs = 250;
    constexpr size_t chunkSize = 64;
    uint32_t progressAt = now();
    size_t sent = 0;
    while (sent < len) {
        const uint32_t stamp = now();
        if (stamp - startedAt >= maxShotMs || stamp - progressAt >= maxIdleMs) return false;

        const int room = port.availableForWrite();
        if (room > 0) {
            size_t amount = len - sent;
            if (amount > chunkSize) amount = chunkSize;
            if (amount > (size_t)room) amount = (size_t)room;
            const size_t wrote = port.write(reinterpret_cast<const uint8_t*>(data + sent), amount);
            if (wrote) {
                sent += wrote;
                progressAt = now();
                continue;
            }
        }
        idle();
    }
    return true;
}

}
