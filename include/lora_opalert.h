// SquachWatch-CYD — a LoRaWAN operator within earshot, as one Detection.
//
// The sniffer already names who runs a LoRaWAN device: the DevAddr's prefix is
// the operator's NetID and the registry is in flash (lora_lorawan.h). This is
// the step from a row in NODES to an alert: the first time an operator's
// network is heard, the board says so -- "Oresundskraft" on the card, the way
// an AirTag says "Apple".
//
// One Detection per OPERATOR, not per device. A utility's meters all share a
// NetID and a street holds hundreds of them, so a row per DevAddr would be a
// flood of alerts about one fact. The address the Detection carries is made up
// from the NetID (see macFor), which is what makes the LOG, IGNORE, SNOOZE,
// WATCH and HUNT work on it unchanged: they all go by address.
//
// What does not count:
//  - join requests, join accepts, proprietary frames and Class B beacons: none
//    of them carries a DevAddr;
//  - NetID 0 and 1, "private/experimental". That is not an operator, and it is
//    where an all-zero or garbage header lands;
//  - a DevAddr the registry has no row for.
//
// A downlink has no payload CRC (RP002: the gateway leaves it off), so a frame
// off RX2 is only as good as its header checksum. One of those names nobody:
// the same operator has to be heard again within the hour before it counts. An
// uplink whose CRC checked counts at once.
//
// Standalone, for the host tests.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "lora_pkt.h"
#include "lora_lorawan.h"

namespace Lora {
namespace OpAlert {

// How long a frame without a CRC waits for a second one from its operator.
static const uint32_t CONFIRM_MS = 60UL * 60UL * 1000UL;
// Operators kept track of at once. Past this the one unheard for longest
// gives way, and would have to be heard again to count again.
static const uint8_t  CAP = 16;

struct Hit {
    uint8_t  mac[6];      // macFor(netId)
    uint32_t netId;
    char     name[20];    // the operator, in ASCII, cut to fit Detection::name
    int8_t   rssi;        // dBm, clamped
    bool     exact;       // DevAddr types 0-2, where the prefix is the whole NetID
};

// A frame the LoRaWAN decoder accepted. True when it names an operator that
// now counts, with `out` filled in; it stays true for every later frame of
// that operator, so the caller can keep the signal fresh.
bool note(const LoRaWAN::Decoded& d, const Packet& pk, uint32_t now, Hit& out);

// 02:4C:57 ("LW" under the locally-administered bit) and the three NetID
// bytes: an address no radio was ever assigned, the same for one operator on
// every board and across reboots.
void macFor(uint32_t netId, uint8_t mac[6]);

// "Öresundskraft" -> "Oresundskraft". The panel's font is ASCII: a Latin-1
// letter becomes its base letter, anything else outside ASCII is dropped.
void asciiName(const char* utf8, char* out, size_t cap);

void clear();

}
}
