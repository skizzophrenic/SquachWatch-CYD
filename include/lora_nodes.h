// SquachWatch-CYD — who is on the air: one row per LoRa transmitter.
//
// Every decoder names its sender somehow -- a node number, a public key's
// first bytes, a DevAddr, a callsign, a FANET address -- and this keeps one
// row per (network, name) with what has been learned about it: the name it
// gave, its role, its last position, how loud, how often, how much airtime,
// and the things a sysop would want flagged. Standalone, for the host tests;
// the sniffer feeds it and the NODES view reads it.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "lora_pkt.h"

namespace Lora {
namespace Nodes {

enum NodeFlags : uint8_t {
    NF_HOPS_HIGH  = 0x01,   // Meshtastic: a hop limit above the default 3
    NF_DEPRECATED = 0x02,   // Meshtastic: ROUTER_CLIENT or REPEATER
    NF_MQTT       = 0x04,   // Meshtastic: relaying MQTT traffic onto the air
    NF_CHATTY     = 0x08,   // telemetry or positions more often than the defaults
    NF_DUTY       = 0x10,   // over the duty ceiling of the band it was heard on, over the last hour
    NF_BAD_CLOCK  = 0x20,   // MeshCore: an advert timestamp far from our clock
    NF_RESETS     = 0x40,   // LoRaWAN: the frame counter went backwards
    NF_OLD_FW     = 0x80,   // Meshtastic: no hop_start (before 2.3) or no relay byte (before 2.6)
};

// How a frame reached us, and therefore whose transmitter the RSSI, the SNR
// and the airtime in it were measured off.
//
// DIRECT means the protocol establishes that we received this node's own
// transmission. VIA means it does not: either the frame says it was relayed,
// or the network gives no way to tell. The row keeps those two apart instead
// of averaging them, because the numbers mean different things -- a relayed
// copy measures the last repeater's link to us, which is a fact about the
// repeater and says nothing about how far away the node is. Which of the two
// kinds of VIA it was is not kept: neither is a measurement of this node, and
// the row has nothing to do with the difference.
enum Link : uint8_t { LINK_DIRECT = 0, LINK_VIA = 1 };

struct Node {
    Proto    proto;
    uint64_t id;
    char     tag[12];       // the short handle: "!3b1c9a2e", "40 DL Rep", "26011b2c", "DH5DAX-7", "11:A3F0"
    char     name[24];      // what it called itself, or who runs it
    uint8_t  role;          // the network's own role byte
    bool     hasPos;
    int32_t  latE7, lonE7;
    // Measured off this node's own transmitter: a link to here. -200 until a
    // frame arrives that the protocol says is direct -- which for a node only
    // ever heard through repeaters is never, and that row must not show an
    // RSSI as if it were a distance.
    int16_t  rssi;
    int8_t   snr4;
    // Measured off whoever transmitted the copy we heard: the last relay, or
    // an unattributable sender. A number worth keeping -- it is what we
    // actually received -- but not this node's.
    int16_t  viaRssi;
    int8_t   viaSnr4;
    uint16_t packets;       // = directPackets + viaPackets
    uint16_t directPackets, viaPackets;
    uint32_t firstMs, lastMs;
    // Time on air of the frames this node transmitted itself. Relayed copies
    // are airtime the RELAY spent, so they are counted on the relay's row when
    // it has one and nowhere when it does not -- which makes this a floor, and
    // makes it the figure NF_DUTY can be built on. It does not add up to
    // Stats::airtimeMs, which counts every frame read including CRC failures
    // and every relayed copy.
    uint32_t airtimeMs;
    uint32_t hourAirMs;     // in the current hour bucket, direct frames only
    uint32_t hourStartMs;
    uint16_t dutyLimit;     // the band's ceiling in per-mille where it was last heard; 0 = no figure
    uint8_t  hops;          // last hops away / path length
    uint8_t  hopLimit;      // last hop limit seen (Meshtastic)
    uint16_t counter;       // LoRaWAN FCnt; MeshCore advert count
    uint16_t lost;          // LoRaWAN: frames the counter skipped
    uint32_t lastTelemMs;   // for NF_CHATTY
    uint8_t  flags;
};

// How many rows the table holds. Named here because a caller that copies the
// whole table out (Lora::nodeSnapshot) has to size a buffer for it.
static const uint8_t CAP = 96;

// Feed a classified packet in. `now` is millis(); `epoch` the wall clock
// or 0. Decodes what it needs itself.
void note(const Packet& pk, uint32_t now, uint32_t epoch);

uint8_t     count();
const Node* at(uint8_t i);
// Indices sorted newest-heard first, into idx (cap entries). Returns how many.
uint8_t     order(uint8_t* idx, uint8_t cap);
void        clear();
// Airtime over the last hour as a per-mille of the hour so far. The bucket
// tumbles, so early in a fresh one this is a ratio over a window of seconds
// and it sawtooths: a single 0.7 s frame 1.5 s in reads 466 per-mille. Ask
// dutyKnown() before showing it or acting on it.
uint16_t    dutyPermille(const Node& n, uint32_t now);
// Whether the current bucket holds enough of an hour for the ratio above to
// mean anything: five minutes, the same floor NF_DUTY has always used.
bool        dutyKnown(const Node& n, uint32_t now);
const char* roleText(const Node& n);    // the network's word for its role
const char* flagsText(const Node& n, char* out, size_t cap);   // "hops mqtt duty"

}
}
