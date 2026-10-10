// SquachWatch-CYD — BLE on-air cadence for SquachMesh.
// A sender changes scan-response frames only by stop/restarting advertising.
// A frame held for 1,600 ms with the *same* 1,500 ms advertising interval
// has little scheduling margin: one missed event can drop a multipart piece.
// When actively sending, reduce the advertising interval to 400 ms, leaving
// several transmission opportunities per part. Idle remains at 1,500 ms.
// No protocol/codec changes or alterations to the Wi-Fi/BLE OTA path.
#pragma once
#include <stdint.h>

namespace MeshAirtime {
constexpr uint16_t IDLE_ADVERTISING_MS    = 1500;
constexpr uint16_t MESSAGE_ADVERTISING_MS = 400;
constexpr uint32_t FRAME_DWELL_MS         = 1600;
constexpr uint32_t INVITE_OFFER_MS        = 60000;
constexpr uint8_t  INVITE_PARTS           = 4;

// Allow at least three nominal beacon opportunities in each window;
// retains capacity for 9 full invite rotations within the 60s offer.
static_assert(FRAME_DWELL_MS >= 3 * MESSAGE_ADVERTISING_MS,
              "each message fragment needs multiple adverts");
static_assert(INVITE_OFFER_MS / (FRAME_DWELL_MS * INVITE_PARTS) >= 8,
              "invitation must repeat all four fragments several times");
}  // namespace MeshAirtime
