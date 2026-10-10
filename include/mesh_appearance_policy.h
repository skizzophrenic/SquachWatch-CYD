// Fast, allocation-free appearance comparison for SquachMesh advertising.
// The live codec's 8- or 20-byte payload includes nickname, outfit, shades,
// and (if present) custom name. Refresh only if those bytes really differ.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace MeshAppearancePolicy {
inline bool changed(const uint8_t* current, size_t currentLen,
                    const uint8_t* advertised, size_t advertisedLen) {
    if (currentLen != advertisedLen) return true;
    if (currentLen == 0) return false;
    return memcmp(current, advertised, currentLen) != 0;
}
} // namespace MeshAppearancePolicy
