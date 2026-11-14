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

// ---- sending ------------------------------------------------------------------
// Whether a frame can be built for this channel: switched on, and a key this
// file can seal with -- 16 bytes, or none at all (ham mode, in the clear). A
// 32-byte key is refused here for the reason decrypt() cannot open one: there
// is no AES-256 in lora_crypto, and a frame sealed wrong is worse than none.
bool canSend(const Channel& c);
// One broadcast frame from `from`, the way a stock node builds it: the 16-byte
// header in the clear (to all, from, id, hop limit = hop start = `hops`, the
// channel's hash, next hop 0, relay = our last byte), then Data{portnum,
// payload} under the channel's key with the same nonce decrypt() uses -- CTR,
// so sealing is that same operation. `f` needs room for 16 + 4 + 200 bytes;
// the payload is cut at 200. Returns the length, 0 when !canSend(c).
// Pure, so test/lora_meshtastic_test.cpp builds one and opens it with decode().
uint16_t buildFrame(const Channel& c, uint32_t from, uint32_t id, uint8_t hops, uint8_t port,
                    const uint8_t* payload, size_t plen, uint8_t* f);
// The pieces buildFrame is made of, for frames that are not a plain
// broadcast. encodeData: Data{portnum, payload (cut at 200), want_response
// when asked, request_id when non-zero} into `out` (room for 212); returns
// its length. buildChannelFrame: that Data under the channel's key, to `to`,
// want_ack as asked -- an ACK, or a NodeInfo sent to one node.
size_t   encodeData(uint8_t port, const uint8_t* payload, size_t plen, bool wantResponse, uint32_t requestId,
                    uint8_t* out);
uint16_t buildChannelFrame(const Channel& c, uint32_t from, uint32_t to, uint32_t id, uint8_t hops, bool wantAck,
                           const uint8_t* data, size_t dlen, uint8_t* f);

// ---- direct messages (PKI, firmware 2.5 and later) ------------------------------
// A direct message is sealed to the two nodes' X25519 keys: the AES-256 key
// is SHA-256 of the shared secret, the cipher AES-CCM with an 8-byte tag and
// a 13-byte nonce of the packet id, a random "extra nonce" and the sender.
// On the air: header (channel byte 0), ciphertext, tag, extra nonce. Since
// 2.5 the firmware refuses a text DM sealed under a channel key ("Rejecting
// legacy DM", Router.cpp), so this is the only kind that arrives.
// CryptoEngine::encryptCurve25519 / decryptCurve25519 are the reference.
static const uint8_t PKI_OVERHEAD = 12;     // tag 8 + extra nonce 4
static const uint8_t PKI_CHANNEL  = 0xFF;   // Decoded::channelIdx of an opened direct message
// False when their key is a bad point (the shared secret came out zero).
bool     pkiKey(const uint8_t myPriv[32], const uint8_t theirPub[32], uint8_t key[32]);
// out gets ciphertext | tag | extra nonce; returns len + 12 (0 for nothing to seal).
size_t   pkiSeal(const uint8_t key[32], uint32_t from, uint32_t id, uint32_t extraNonce,
                 const uint8_t* plain, size_t len, uint8_t* out);
// The plaintext's length, or -1 when the tag does not hold (wrong key, or tampered).
int      pkiOpen(const uint8_t key[32], uint32_t from, uint32_t id, const uint8_t* in, size_t len, uint8_t* out);
// Header to `to` on channel 0, then pkiSeal of the encoded Data. 0 when it
// would not fit in 255 bytes, or `to` is everyone (a DM is to one node).
uint16_t buildPkiFrame(const uint8_t key[32], uint32_t from, uint32_t to, uint32_t id, uint8_t hops, bool wantAck,
                       uint32_t extraNonce, const uint8_t* data, size_t dlen, uint8_t* f);
// decode() hands a channel-0 frame to this when one is set: true and the
// plaintext in `out` (room for 240) only when the frame is to this board and
// the sender's key is known (Lora::Dm holds both). Without one, a DM stays
// shut -- every board but the T-Deck, and the host tests that do not set one.
typedef bool (*PkiOpener)(const Header& h, const uint8_t* in, uint8_t len, uint8_t* out, uint8_t& outLen);
void     setPkiOpener(PkiOpener fn);

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
    bool     haveData;      // a channel key opened it -- or, with pki, our own key pair
    uint8_t  channelIdx;    // which Channel did; PKI_CHANNEL for a direct message
    bool     pki;           // a direct message to this board, opened by the PkiOpener
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
