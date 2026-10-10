// Contract check for the *firmware's* actual airtime constants.
// This does not replace two-board radio verification.
#include "mesh_airtime.h"
#include <cassert>
#include <cstdio>

int main() {
    using namespace MeshAirtime;
    assert(IDLE_ADVERTISING_MS == 1500);  // unchanged idle radio budget
    assert(MESSAGE_ADVERTISING_MS < IDLE_ADVERTISING_MS);
    assert(FRAME_DWELL_MS == 1600);  // same per-part duration/codec
    assert(FRAME_DWELL_MS / MESSAGE_ADVERTISING_MS >= 3);
    assert(INVITE_PARTS == 4);
    assert(INVITE_OFFER_MS / (FRAME_DWELL_MS * INVITE_PARTS) >= 8);
    std::puts("PASS: fast message beacons with preserved idle airtime");
}
