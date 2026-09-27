// SquachWatch-CYD — Meshtastic, off the air.
//
// The 16-byte header is always in the clear; the payload is AES-CTR under a
// channel key, and the default key is published, so a default-channel packet
// reads in full. Everything else is a header and a size. docs/LORA.md
// section 3.1 has the byte layout and where each rule was read.
//
// Standalone: the host test builds this against constructed packets.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "lora_pkt.h"

namespace Meshtastic {

static const uint32_t BROADCAST = 0xFFFFFFFFu;

struct Header {
    uint32_t to, from, id;
    uint8_t  hopLimit, hopStart;
    bool     wantAck, viaMqtt;
    uint8_t  channelHash;     // 0x00 = a PKI direct message
    uint8_t  nextHop, relayNode;
};
bool parseHeader(const uint8_t* d, uint8_t len, Header& h);
// hop_start - hop_limit, or 0xFF when hop_start is 0 (firmware before 2.3).
uint8_t hopsAway(const Header& h);

// ---- channels ---------------------------------------------------------------
// A channel is a name and a key; the hash in the header is XOR(name bytes)
// XOR XOR(key bytes). The presets on the default key are built in. A user
// may add a few of their own (name + key, base64 or hex); those are kept
// across a reboot by lora_channels.cpp.
//
// Nothing here is derivable the way a MeshCore hashtag is: a Meshtastic
// channel's key is a secret somebody hands over, so the stored form is the
// key itself.
struct Channel {
    char    name[24];
    uint8_t key[32];
    uint8_t keyLen;   // 0 = no encryption (ham mode), 16 or 32
    uint8_t hash;
    // Muted: still listed and still stored, but decode() will not try it.
    // The built-in presets are always on.
    bool    enabled;
};
uint8_t  channelHash(const char* name, const uint8_t* key, uint8_t keyLen);
// A one-byte PSK the way the apps carry it: 0 = none, 1 = the default key,
// 2..255 = the default key with its last byte raised.
void     expandPsk(uint8_t psk, uint8_t out[16]);
// Channels known, built-in first. The presets' default-key hashes are
// LongFast 0x08, MediumFast 0x1F, ShortSlow 0x77 and so on.
uint8_t        channelCount();
// A copy, not a reference -- see the note in lora_meshcore.h. Past the end
// gives a zeroed, disabled channel.
Channel        channel(uint8_t i);
// The user's own; false when the table is full, the name is already held, or
// the key does not parse.
bool           addChannel(const char* name, const char* keyText);
void           clearUserChannels();
// How many of the list are the user's, and how many there is room for. The
// built-in presets are the first channelCount() - userChannelCount() entries.
uint8_t        userChannelCount();
uint8_t        maxUserChannels();
// The channel of that name, or -1; case-insensitive.
int            findChannel(const char* name);
// Forget one. Refuses a preset; the entries after it shift down, counters and
// all, so an index is only good until the next call.
bool           removeChannel(uint8_t i);
// Mute or unmute one of the user's. A preset is always on.
void           setChannelEnabled(uint8_t i, bool on);
// What each channel's key has actually opened, index-for-index with
// channel(). Fed from Lora::Nodes::note(), the one place a frame is decoded
// exactly once -- decode() also runs for every row the screen redraws.
void           noteChannelHeard(uint8_t i, uint32_t nowMs);
uint32_t       channelFrames(uint8_t i);
uint32_t       channelLastMs(uint8_t i);    // millis() of the last one; 0 = never

// ---- the payload ------------------------------------------------------------
// Decrypts payload (the bytes after the header) in place with the channel's
// key. A wrong key gives noise, and there is no MAC: parseData() is the
// check, as it is in the firmware.
void decrypt(const Header& h, const Channel& c, uint8_t* payload, uint8_t len);

struct Data {
    uint32_t       portnum;
    const uint8_t* payload;
    uint8_t        payloadLen;
    bool           wantResponse;
    uint32_t       dest, source, requestId, replyId, emoji, bitfield;
    bool           signed_;     // 2.8's XEdDSA signature was present
};
bool parseData(const uint8_t* p, uint8_t len, Data& out);

// The ports that matter, and their payloads.
enum Port : uint32_t {
    PORT_TEXT = 1, PORT_REMOTE_HW = 2, PORT_POSITION = 3, PORT_NODEINFO = 4, PORT_ROUTING = 5,
    PORT_ADMIN = 6, PORT_TEXT_COMPRESSED = 7, PORT_WAYPOINT = 8, PORT_AUDIO = 9, PORT_DETECTION = 10,
    PORT_ALERT = 11, PORT_KEY_VERIFY = 12, PORT_REPLY = 32, PORT_IP_TUNNEL = 33, PORT_PAXCOUNTER = 34,
    PORT_SERIAL = 64, PORT_STORE_FORWARD = 65, PORT_RANGE_TEST = 66, PORT_TELEMETRY = 67,
    PORT_ZPS = 68, PORT_SIMULATOR = 69, PORT_TRACEROUTE = 70, PORT_NEIGHBORINFO = 71,
    PORT_ATAK_PLUGIN = 72, PORT_MAP_REPORT = 73, PORT_POWERSTRESS = 74, PORT_PRIVATE = 256, PORT_ATAK_FORWARDER = 257,
};
const char* portName(uint32_t port);

struct User {
    char     id[12];
    char     longName[40];
    char     shortName[5];
    uint32_t hwModel;
    uint8_t  role;
    bool     licensed;
    bool     hasPubkey;
    uint8_t  pubkey[32];
};
bool parseUser(const uint8_t* p, uint8_t len, User& u);
const char* roleName(uint8_t role);
const char* hwModelName(uint32_t model, char* buf, size_t cap);   // the common boards; "HW n" into buf otherwise

struct Position {
    bool     hasLatLon;
    int32_t  latI, lonI;     // 1e-7 degrees
    int32_t  altitude;
    uint32_t time;
    uint32_t satsInView;
    uint32_t precisionBits;  // 32 = exact; the default channel sends less
    uint32_t groundSpeed;
};
bool parsePosition(const uint8_t* p, uint8_t len, Position& out);

struct Telemetry {
    uint32_t time;
    uint8_t  kind;          // 2 device, 3 environment, 4 air quality, 5 power, 6 local stats, 7 health, 8 host
    // device
    uint32_t batteryLevel;  // percent, 101 = powered
    float    voltage, channelUtil, airUtilTx;
    uint32_t uptime;
    // environment
    float    temperature, humidity, pressure;
    bool     hasTemp, hasHumidity, hasPressure;
    // local stats
    uint32_t packetsTx, packetsRx, packetsRxBad, onlineNodes, totalNodes, rxDupe, txRelay, txRelayCanceled;
};
bool parseTelemetry(const uint8_t* p, uint8_t len, Telemetry& out);

struct RouteDiscovery {
    uint8_t  n, nBack;
    uint32_t route[8], routeBack[8];
    int8_t   snrTowards[9], snrBack[9];   // SNR x 4, INT8_MIN unknown; one more than hops
    uint8_t  nSnr, nSnrBack;
};
bool parseRouteDiscovery(const uint8_t* p, uint8_t len, RouteDiscovery& out);

struct Routing {
    uint8_t kind;          // 1 request, 2 reply, 3 error
    uint8_t errorReason;   // 0 = NONE: an ACK
    RouteDiscovery rd;
};
bool parseRouting(const uint8_t* p, uint8_t len, Routing& out);
const char* routingErrorName(uint8_t reason);

struct NeighborInfo {
    uint32_t nodeId, lastSentBy, interval;
    uint8_t  n;
    struct { uint32_t id; int8_t snr4; } nb[12];
};
bool parseNeighborInfo(const uint8_t* p, uint8_t len, NeighborInfo& out);

// ---- the whole thing at once ------------------------------------------------
// Everything a screen wants from one frame. `plain` is the decrypted Data
// area, kept in the struct so the payload pointers stay valid.
struct Decoded {
    Header   hdr;
    bool     haveData;      // a channel key opened it
    uint8_t  channelIdx;    // which Channel did
    Data     data;
    uint8_t  plain[240];
    // The payload, by port; only the one matching data.portnum is filled.
    User           user;
    Position       pos;
    Telemetry      tel;
    RouteDiscovery route;
    Routing        routing;
    NeighborInfo   nbr;
    char           text[200];
};
bool decode(const Lora::Packet& pk, Decoded& out);

// One line, e.g. "!3b1c9a2e>all hop1/3 ch08 TEXT hello" -- for the list.
void summary(const Lora::Packet& pk, char* out, size_t cap);

// The node-number string, "!3b1c9a2e".
void nodeId(uint32_t num, char out[12]);

}
