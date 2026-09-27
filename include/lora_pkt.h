// SquachWatch-CYD — what one LoRa packet is, once it is off the air.
//
// Standalone: <stdint.h> only, so the decoders and their host tests build
// without a radio or an Arduino core. The radio side (lora_sniffer.cpp) fills
// a Packet in; everything else reads it. docs/LORA.md is the background.
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace Lora {

// Which network a packet turned out to belong to. The order is the colour
// chip's order on the LORA screen; UNKNOWN is "a LoRa frame, and that is all
// we know", which is still a sighting.
enum class Proto : uint8_t {
    UNKNOWN = 0,
    MESHTASTIC,
    MESHCORE,
    LORAWAN,
    APRS,        // LoRa APRS, the <FF 01 TNC2 kind
    MESHCOM,     // the OE1KBC ham mesh
    FANET,
    RETICULUM,   // an RNode frame carrying Reticulum
    COUNT
};
const char* protoName(Proto p);     // "MESHTASTIC"
const char* protoShort(Proto p);    // "MT", for a four-character column

// A receive profile: everything the radio must be told before it can hear a
// network. CR is not in here on purpose -- the explicit LoRa header carries
// it, so a receiver decodes any CR -- but it is what a transmitter would use.
enum ProfileFlags : uint8_t {
    PF_CRC      = 0x01,   // the transmitter appends a payload CRC (uplinks do; LoRaWAN downlinks do not)
    PF_INVERT   = 0x02,   // inverted IQ (LoRaWAN downlinks, TS011 relays)
    PF_IMPLICIT = 0x04,   // implicit header: the length is fixed and known (Class B beacons)
    PF_LDRO     = 0x08,   // force low-data-rate optimisation on (balloon modes); otherwise automatic
    PF_FSK      = 0x10,   // not LoRa at all: GFSK (OGN, wM-Bus). Reserved for the FSK phase
    PF_HAM      = 0x20,   // an amateur allocation: plaintext by law, and only a licence transmits
    PF_433      = 0x40,   // below the module's band: hears strong signals only
};

struct Profile {
    const char* name;        // "MT EU LongFast" -- fits a 14-character column
    uint32_t    freqHz;
    uint16_t    bwKhz10;     // bandwidth in units of 100 Hz: 2500 = 250 kHz, 625 = 62.5 kHz
    uint8_t     sf;
    uint8_t     cr;          // 5..8, the denominator, for transmit and time-on-air
    uint8_t     sync;        // the one-byte form RadioLib takes: 0x12, 0x34, 0x2B, 0xF1
    uint16_t    preamble;    // symbols the RECEIVER should expect (the longest a sender uses)
    uint8_t     flags;       // ProfileFlags
    uint8_t     implicitLen; // with PF_IMPLICIT
    Proto       hint;        // what usually lives here; the classifier checks the bytes anyway
    uint8_t     group;       // ProfileGroup, for the picker
};

enum ProfileGroup : uint8_t {
    PG_MESH_EU = 0,   // Meshtastic and MeshCore, 869 MHz
    PG_LORAWAN,       // uplinks, RX2, beacon
    PG_AIR,           // FANET
    PG_HAM_433,       // APRS, MeshCom, the 433 mesh presets
    PG_OTHER,
    PG_COUNT
};
const char* profileGroupName(uint8_t g);

enum PacketFlags : uint8_t {
    PK_CRC_PRESENT = 0x01,   // the header said a CRC follows
    PK_CRC_OK      = 0x02,   // ...and it checked (meaningless without PK_CRC_PRESENT)
    PK_CRC_ERR     = 0x04,   // ...and it did not: kept, flagged, never decoded
    PK_IMPLICIT    = 0x08,   // received in implicit mode, so no header fields
    PK_DECODED     = 0x10,   // the classifier's decoder accepted it
    PK_PLAINTEXT   = 0x20,   // ...and read it in full (open network, public key)
    PK_LIVE        = 0x40,   // the sniffer's own reception, not a replay
};

// One frame. About 290 bytes; the ring keeps a few hundred of these in PSRAM.
// The radio settings it was heard with travel in the record rather than
// through the profile index alone: a LoRaWAN downlink chased on an uplink's
// own channel has no table entry, and a LoRaTap export wants the numbers.
struct Packet {
    uint32_t ms;         // millis() at reception
    uint32_t epoch;      // the wall clock's second when it was trusted, else 0
    uint32_t toaUs;      // time on air, from the formula below
    int32_t  ferrHz;     // the chip's frequency error estimate
    uint32_t freqHz;
    uint16_t bwKhz10;
    int16_t  rssi;       // dBm
    int8_t   snr4;       // SNR x 4, the way Meshtastic and MeshCore carry it
    uint8_t  sf;
    uint8_t  sync;
    uint8_t  pflags;     // the ProfileFlags in force (IQ, implicit)
    uint8_t  profile;    // index into the profile table, or PROFILE_CUSTOM
    uint8_t  cr;         // 5..8 from the header; 0 in implicit mode
    uint8_t  flags;      // PacketFlags
    Proto    proto;      // the classifier's answer
    uint8_t  len;
    uint8_t  data[255];
};
static const uint8_t PROFILE_CUSTOM = 0xFF;

// Time on air in microseconds, Semtech's formula (SX1261/2 datasheet 6.1.4),
// checked against the reference values in docs/LORA.md. `cr` is the
// denominator (5..8). LDRO follows the chip's automatic rule unless forced.
uint32_t timeOnAirUs(uint8_t sf, uint16_t bwKhz10, uint8_t cr, uint16_t preamble,
                     uint8_t len, bool crc, bool implicit, bool forceLdro);
// The symbol time, which the scheduler paces CAD rounds by.
uint32_t symbolUs(uint8_t sf, uint16_t bwKhz10);
// True when the chip would switch LDRO on by itself (symbol time >= 16.38 ms).
bool autoLdro(uint8_t sf, uint16_t bwKhz10);

// The two-byte register value RadioLib programs for a one-byte sync word:
// 0x12 -> 0x1424, 0x34 -> 0x3444, 0x2B -> 0x24B4. For the LoRaTap header.
inline uint16_t syncRegister(uint8_t sync) {
    return (uint16_t)((((sync & 0xF0) | 0x04) << 8) | (((sync & 0x0F) << 4) | 0x04));
}

}
