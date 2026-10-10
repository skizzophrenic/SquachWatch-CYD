// Backoff for retrying an advertiser that did not start. The old code cached
// the requested (not actual) TX state and skipped reattempts until a frame
// changed or a ten-second refresh. Never let one dropped start swallow SEND.
#pragma once
#include <stdint.h>

namespace MeshTxPolicy {
constexpr uint32_t RETRY_MS = 500;
constexpr bool attempt(bool consent, bool needsUpdate,
                       uint32_t now, uint32_t lastTry) {
    return consent && needsUpdate &&
           (lastTry == 0 || (uint32_t)(now - lastTry) >= RETRY_MS);
}
} // namespace MeshTxPolicy
