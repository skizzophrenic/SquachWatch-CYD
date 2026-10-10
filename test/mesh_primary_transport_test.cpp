// Passive-compatible universal mesh transport, no NimBLE or display needed.
// The sender emits only ONE manufacturer field per broadcast; both active
// and passive listeners use the same existing onManufacturerData decoder.
#include "mesh_primary_transport.h"
#include "mesh_airtime.h"
#include "meshmsg.h"
#include "squachmesh.h"
#include <cassert>
#include <cstdio>

int main() {
    using namespace MeshPrimaryTransport;
    static_assert(FRAME_MS == MeshAirtime::FRAME_DWELL_MS, "slot/dwell match");
    static_assert(MAX_FRAME_BYTES == 27, "legacy ADV data budget");
    static_assert(MAX_PARTS == MeshMsg::OUT_PARTS_MAX, "all send types included");
    static_assert(MeshMsg::FRAME_MAX == MAX_FRAME_BYTES, "entire frame fits");
    static_assert((MAX_PARTS + 1u) * FRAME_MS < 20000u, "presence before stale timeout");

    for (uint8_t n = 1; n <= MAX_PARTS; ++n) {
        // Deterministically transmit EVERY fragment before the presence
        // slot, including all seven parts of a firmware update nudge.
        for (uint8_t i = 0; i < n; ++i) {
            assert(slot((uint32_t)i * FRAME_MS, n) == i);
            assert(slot((uint32_t)i * FRAME_MS + FRAME_MS - 1u, n) == i);
            assert(generation(1, i) != 0);
            assert(generation(1, i) != generation(2, i));
        }
        assert(slot((uint32_t)n * FRAME_MS, n) == n); // outfit broadcast
        assert(slot((uint32_t)(n+1u) * FRAME_MS, n) == 0); // repeat
        assert(slot((uint32_t)(n+1u) * FRAME_MS * 3u, n) == 0);
    }
    assert(!fitsPrimary(0));
    assert(fitsPrimary(MeshMsg::FRAME_MAX));
    assert(!fitsPrimary(MeshMsg::FRAME_MAX + 1u));

    // Real manufacturer AD wire layout: 1 length + 1 AD type +
    // 2-byte company ID + max 27-byte authenticated encrypted frame.
    uint8_t wire[LEGACY_ADV_BYTES] = {};
    wire[0] = (uint8_t)(1u + COMPANY_BYTES + MeshMsg::FRAME_MAX);
    wire[1] = 0xFF;
    wire[2] = (uint8_t)SquachMesh::COMPANY_ID;
    wire[3] = (uint8_t)(SquachMesh::COMPANY_ID >> 8);
    wire[4] = MeshMsg::MAGIC[0];
    wire[5] = MeshMsg::MAGIC[1];
    wire[6] = (uint8_t)((MeshMsg::VERSION << 4) | MeshMsg::KIND_TEXT);
    assert((size_t)wire[0] + 1u == LEGACY_ADV_BYTES);
    assert(MeshMsg::isFrame(wire + 4, MeshMsg::FRAME_MAX));
    uint32_t ctr = 0; uint8_t kind = 0;
    assert(MeshMsg::parseHeader(wire + 4, MeshMsg::FRAME_MAX, ctr, kind));
    assert(kind == MeshMsg::KIND_TEXT);

    // Same receiver still recognizes its original appearance format;
    // it is never mistaken for an encrypted message.
    SquachMesh::Peer me{2, 4, 1, false, ""}, decoded{};
    uint8_t look[SquachMesh::LEN_MAX] = {};
    const size_t lookLen = SquachMesh::encode(me, look);
    assert(lookLen == SquachMesh::LEN_INDEXED);
    assert(!MeshMsg::isFrame(look, lookLen));
    assert(SquachMesh::decode(look, lookLen, decoded));
    assert(decoded.outfit == 4);

    // Period slot computation also tolerates 32-bit millis() wrap.
    assert(slot((uint32_t)(0x00000540u - 0xFFFFFF00u), 4) == 1);
    std::puts("PASS: primary BLE frame budget, all 7 parts, periodic presence, old codec");
}
