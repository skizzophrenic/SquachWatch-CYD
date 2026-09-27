// SquachWatch-CYD — LoRa APRS and MeshCom, the amateur networks on 433 MHz.
//
// Both are plaintext by law and by design. LoRa APRS is three marker bytes
// and a TNC2 line; MeshCom is its own small frame around an APRS-shaped
// position or a text. Both carry callsigns, so both are the operator's own
// identification. docs/LORA.md sections 3.4 and 3.5.
//
// Standalone, for the host tests.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "lora_pkt.h"

namespace Aprs {

// ---- the APRS information field, shared by both -----------------------------
enum InfoKind : uint8_t {
    INFO_OTHER = 0, INFO_POSITION, INFO_POSITION_TS, INFO_MICE, INFO_MESSAGE, INFO_STATUS,
    INFO_OBJECT, INFO_ITEM, INFO_TELEMETRY
};
struct Info {
    uint8_t  kind;
    bool     hasPos;
    int32_t  latE7, lonE7;    // 1e-7 degrees
    char     symTable, symCode;
    bool     hasCourseSpeed;
    uint16_t course;          // degrees
    uint16_t speedKmh;
    bool     hasAlt;
    int32_t  altM;
    char     addressee[10];   // messages
    char     text[128];       // the message, the status, the comment
    bool     ambiguous;       // Mic-E with digits blanked
};
// Reads an information field. `dest` is needed for Mic-E, where half the
// position is in the destination callsign; nullptr otherwise.
bool parseInfo(const char* info, size_t len, const char* dest, Info& out);
// "DDMM.mmN/DDDMM.mmE" with the symbol table between and the code after.
bool parseUncompressed(const char* p, size_t len, int32_t& latE7, int32_t& lonE7, char& table, char& code);

// ---- LoRa APRS ---------------------------------------------------------------
struct Frame {
    char src[10], dest[10];
    char path[48];           // "WIDE1-1,DB0XYZ*"
    uint8_t digipeated;      // how many path entries carry a '*'
    Info info;
    const char* infoRaw;
    size_t infoLen;
};
bool parse(const uint8_t* d, uint8_t len, Frame& f);   // the whole LoRa payload, marker bytes included
void summary(const Lora::Packet& pk, char* out, size_t cap);

}

namespace MeshCom {

struct Frame {
    char     type;           // ':' text, '!' position, '@' HEY, 'A' ack
    uint32_t msgId;
    uint8_t  hopsLeft;
    bool     viaServer;      // 0x80: already went through a server
    bool     track;          // 0x40: relays append their call
    char     src[10];
    char     path[48];       // the relays so far, comma-separated
    char     dest[10];       // "*", a group number, or a callsign
    const char* payload;     // after the type char
    size_t   payloadLen;
    uint8_t  hwId, modId, fwVersion, lastHopHw;
    char     subVersion;
    bool     fcsOk;
    // ACKs
    uint32_t ackFor;
    // Positions, through the APRS parser
    Aprs::Info info;
    bool     hasInfo;
    // The /B= battery and /A= altitude riders on a position
    int16_t  batteryPct;     // -1 if absent
};
bool parse(const uint8_t* d, uint8_t len, Frame& f);
const char* hwName(uint8_t id);
const char* modName(uint8_t id);
void summary(const Lora::Packet& pk, char* out, size_t cap);

}
