// Universal SquachMesh radio schedule for all supported board profiles.
// Existing encrypted packets move from scan responses to PRIMARY legacy
// advertising, which passive or active BLE scanners can both receive.
// Keep the normal appearance/identity advertisement every (N+1)th slot.
// N<=7: peer presence repeats within 12.8s, under the 20s stale timeout.
#pragma once
#include <stddef.h>
#include <stdint.h>
namespace MeshPrimaryTransport {
constexpr uint32_t FRAME_MS = 1600u;
constexpr uint8_t MAX_PARTS = 7u;
constexpr size_t LEGACY_ADV_BYTES = 31u;
constexpr size_t COMPANY_BYTES = 2u, AD_HEADER_BYTES = 2u;
constexpr size_t MAX_FRAME_BYTES = LEGACY_ADV_BYTES - COMPANY_BYTES - AD_HEADER_BYTES;
inline bool fitsPrimary(size_t len) {
    return len > 0 && len <= MAX_FRAME_BYTES;
}
inline uint8_t slot(uint32_t elapsed, uint8_t parts) {
    if (!parts || parts > MAX_PARTS) return MAX_PARTS;
    return (uint8_t)((elapsed / FRAME_MS) % (parts + 1u));
}
inline uint32_t generation(uint32_t messageGeneration, uint8_t part) {
    // 4 bits needed because update nudges can contain up to 7 fragments.
    return (messageGeneration << 4) | (part + 1u);
}
} // namespace MeshPrimaryTransport
