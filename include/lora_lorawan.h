// SquachWatch-CYD — LoRaWAN, off the air.
//
// Every frame has a readable envelope: who (DevAddr, and so which operator),
// how often (FCnt), confirmed or not, ADR, and on a 1.0.x network the MAC
// commands ride in the clear. A join request names the device and its maker
// outright. The payload is under the AppSKey and stays shut unless the user
// holds it. docs/LORA.md section 3.3 has the sources; TS001-1.0.4 the bytes.
//
// Standalone, for the host tests.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "lora_pkt.h"

namespace LoRaWAN {

enum MType : uint8_t {
    JOIN_REQUEST = 0, JOIN_ACCEPT = 1, UNCONF_UP = 2, UNCONF_DOWN = 3,
    CONF_UP = 4, CONF_DOWN = 5, RFU = 6, PROPRIETARY = 7
};
const char* mtypeName(uint8_t mtype);

struct Frame {
    uint8_t  mtype, major;
    bool     uplink;          // by MType; a downlink also arrives IQ-inverted
    // data frames
    uint32_t devAddr;
    bool     adr, adrAckReq, ack, fPending, classB;
    uint8_t  fOptsLen;
    uint16_t fCnt;
    const uint8_t* fOpts;
    bool     hasFPort;
    uint8_t  fPort;
    const uint8_t* frm;
    uint8_t  frmLen;
    uint32_t mic;             // as four bytes, big-endian for display
    // join request; EUIs in display order (big-endian), the way people write them
    uint8_t  joinEui[8], devEui[8];
    uint16_t devNonce;
    // the bytes the MIC covers, for verifying it
    const uint8_t* msg;
    uint8_t  msgLen;
};
bool parse(const uint8_t* d, uint8_t len, Frame& f);

// ---- who runs it -------------------------------------------------------------
struct NetInfo {
    uint8_t  type;        // 0..7 from the DevAddr's leading ones
    uint32_t nwkId;
    uint32_t netId;       // the matching registry entry, 0xFFFFFF when none
    const char* op;       // "The Things Network", or nullptr
    bool     ambiguous;   // types 3-7: several NetIDs share this prefix
};
void lookupDevAddr(uint32_t devAddr, NetInfo& out);
// The maker behind an EUI's OUI, or nullptr.
const char* ouiMaker(const uint8_t eui[8]);
// A JoinEUI that names a known join server, or nullptr.
const char* joinServerName(const uint8_t joinEui[8]);
void euiText(const uint8_t eui[8], char out[17]);

// ---- MAC commands ---------------------------------------------------------------
struct MacCmd { uint8_t cid; const uint8_t* payload; uint8_t len; };
// Walks FOpts (or a port-0 payload once decrypted). Stops at the first
// unknown CID, since its length is unknowable.
uint8_t parseMacCommands(const uint8_t* p, uint8_t len, bool uplink, MacCmd* out, uint8_t cap);
const char* macCmdName(uint8_t cid, bool uplink);
void macCmdText(const MacCmd& c, bool uplink, char* out, size_t cap);

// ---- with the keys -------------------------------------------------------------
// How to read an opened payload. AUTO takes Cayenne when it reads cleanly
// as two channels or more, hex otherwise.
enum Codec : uint8_t { CODEC_AUTO = 0, CODEC_CAYENNE = 1, CODEC_TEXT = 2, CODEC_HEX = 3 };
struct Session {
    char     name[12];
    uint32_t devAddr;
    uint8_t  nwkSKey[16], appSKey[16];
    uint8_t  codec;
};
uint8_t        sessionCount();
const Session& session(uint8_t i);
bool           addSession(const char* name, uint32_t devAddr, const char* nwkHex, const char* appHex, uint8_t codec = CODEC_AUTO);
void           clearSessions();
// The MIC with the 32-bit counter's upper half guessed at `fCntMsb`.
bool verifyMic(const Frame& f, const uint8_t nwkSKey[16], uint16_t fCntMsb);
// FRMPayload into `out` (frmLen bytes). The key is the NwkSKey on port 0.
void decryptFrm(const Frame& f, const uint8_t key[16], uint16_t fCntMsb, uint8_t* out);

// A join-accept opened with the AppKey, and the 1.0.x session it starts.
struct JoinAccept {
    uint32_t joinNonce, netId, devAddr;
    uint8_t  rx1DrOffset, rx2Dr, rxDelay;
    bool     hasCfList;
    uint8_t  cfList[16];
    bool     micOk;
};
bool openJoinAccept(const uint8_t* d, uint8_t len, const uint8_t appKey[16], JoinAccept& out);
void deriveSession(const uint8_t appKey[16], uint32_t joinNonce, uint32_t netId, uint16_t devNonce,
                   uint8_t nwkSKey[16], uint8_t appSKey[16]);

// ---- Class B beacon ---------------------------------------------------------------
struct Beacon {
    uint32_t gpsTime;     // seconds since 1980-01-06, mod 2^32
    uint8_t  param;
    bool     crc1Ok, crc2Ok;
    uint8_t  infoDesc;    // 0-2: a GPS position (antenna 1-3); 3: NetID and gateway ID
    int32_t  latE7, lonE7;
    uint32_t netId, gwId;
};
bool parseBeacon(const uint8_t* d, uint8_t len, Beacon& out);   // the 17-byte EU868 form

// ---- Cayenne LPP ----------------------------------------------------------------
// "1 temp 23.4C  2 hum 40%  3 gps 50.73,7.09 120m" -- the channels it could
// read, into out. Returns how many.
uint8_t cayenneText(const uint8_t* p, uint8_t len, char* out, size_t cap);
bool    cayenneConsumed(const uint8_t* p, uint8_t len);

// ---- the whole thing --------------------------------------------------------------
struct Decoded {
    Frame       f;
    NetInfo     net;
    const char* maker;        // join requests
    const char* joinServer;
    uint8_t     nMac;
    MacCmd      mac[8];
    bool        isBeacon;
    Beacon      beacon;
    bool        haveKeys;     // a session matched the DevAddr
    bool        micOk;
    uint8_t     codec;        // the session's
    uint8_t     plain[224];
    uint8_t     plainLen;
};
// The opened payload, as the codec reads it.
void payloadText(const Decoded& d, char* out, size_t cap);
bool decode(const Lora::Packet& pk, Decoded& out);
void summary(const Lora::Packet& pk, char* out, size_t cap);

}
