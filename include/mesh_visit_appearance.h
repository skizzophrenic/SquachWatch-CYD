// SquachMesh CLEAR-screen hosted guest appearance refresh.
//
// UI holds a copy so an arriving/leaving visitor stays visually stable and a
// different peer does not replace them mid-animation. Once the *same* visitor
// changes clothes/name, however, CLEAR must use the current on-air appearance.
// Shared for 2.8", 3.2" and emulator; no Bluetooth, heap or flash work.
#pragma once
#include <stdint.h>
#include <string.h>
#include "squachmesh.h"

namespace MeshVisitAppearance {
inline bool refresh(SquachMesh::Peer& hosting, uint32_t hostingId,
                    uint32_t presentId, const SquachMesh::Peer* present,
                    bool leaving) {
    // Identity comes from the visit's existing MAC-based identity key.
    // A stale/absent peer or an exiting guest must retain its hosted copy.
    if (!present || !hostingId || presentId != hostingId || leaving) return false;
    if (hosting.nick == present->nick && hosting.outfit == present->outfit &&
        hosting.shade == present->shade && hosting.custom == present->custom &&
        strncmp(hosting.name, present->name, SquachMesh::NAME_LEN + 1) == 0)
        return false;
    hosting = *present;
    return true;
}
} // namespace MeshVisitAppearance
