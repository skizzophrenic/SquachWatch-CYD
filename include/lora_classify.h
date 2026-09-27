// SquachWatch-CYD — which network a LoRa frame belongs to, and one line
// about it. Standalone, for the host tests; the protocol decoders sit behind
// it (lora_meshtastic.h and friends) and it asks them in turn.
#pragma once
#include "lora_pkt.h"

namespace Lora {

// Sets pk.proto and the PK_DECODED / PK_PLAINTEXT flags from the bytes and
// the radio settings they came in on. The profile's hint goes first; a frame
// that no decoder claims stays UNKNOWN, which is still a sighting.
void classify(Packet& pk);

// One line for the list and the console, e.g.
//   "MT  !3b1c9a2e > all  hop 2/3  ch 08  TEXT hello"
// Never longer than cap-1; always NUL-terminated.
void summary(const Packet& pk, char* out, size_t cap);

// The bytes as hex, for the PACKET screen and the console. `perLine` bytes
// per row; returns the number of rows it would take.
uint8_t hexDump(const Packet& pk, uint8_t perLine, char* out, size_t cap);

}
