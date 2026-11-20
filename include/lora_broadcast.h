// SquachWatch-CYD — what this board broadcasts about itself on Meshtastic, and
// the rules it keeps on the public channel.
//
// On the default channel (the published key, or no key) a stock node holds its
// own broadcasts to minimum intervals and keeps exact positions and Neighbor
// Info off it: the default channel is shared by everyone in range, and its
// airtime is everyone's. The figures are the firmware's (src/mesh/Default.h:
// min_default_broadcast_interval_secs, min_default_broadcast_smart_minimum_
// interval_secs, min_default_telemetry_interval_secs). Header-only and pure, so
// the host tests check the rules without a radio.
#pragma once
#include <stdint.h>

namespace Lora {
namespace Broadcast {

static const uint32_t PUBLIC_MIN_POSITION_SECS = 60 * 60;   // position, at most hourly
static const uint32_t PUBLIC_MIN_SMART_SECS    = 5 * 60;    // smart position, at most every 5 min
static const uint32_t PUBLIC_MIN_TELEMETRY_SECS = 30 * 60;  // device telemetry, at most every 30 min

// The interval that applies: the setting, or the minimum on the public channel.
inline uint32_t effective(uint32_t setting, bool onPublic, uint32_t publicMin) {
    return onPublic && setting < publicMin ? publicMin : setting;
}
inline uint32_t positionSecs(uint32_t setting, bool onPublic)  { return effective(setting, onPublic, PUBLIC_MIN_POSITION_SECS); }
inline uint32_t smartMinSecs(uint32_t setting, bool onPublic)  { return effective(setting, onPublic, PUBLIC_MIN_SMART_SECS); }
inline uint32_t telemetrySecs(uint32_t setting, bool onPublic) { return effective(setting, onPublic, PUBLIC_MIN_TELEMETRY_SECS); }

// What may go out on the public channel at all.
inline bool positionAllowed(uint8_t precisionBits, bool onPublic) { return !(onPublic && precisionBits >= 32); }
inline bool neighborInfoAllowed(bool onPublic) { return !onPublic; }

}  // namespace Broadcast
}  // namespace Lora
