// SquachWatch-CYD — MeshCore, off the air.
//
// Adverts are plaintext and signed; the relay path is in the clear on every
// packet; the Public channel and any hashtag channel have derivable keys.
// Direct messages are between two nodes' keys and stay shut. docs/LORA.md
// section 3.2 has the layout and the sources.
//
// Standalone, for the host tests.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "lora_pkt.h"

namespace MeshCore {

enum Route : uint8_t { ROUTE_TRANSPORT_FLOOD = 0, ROUTE_FLOOD = 1, ROUTE_DIRECT = 2, ROUTE_TRANSPORT_DIRECT = 3 };
enum Type : uint8_t {
    TYPE_REQ = 0, TYPE_RESPONSE = 1, TYPE_TXT_MSG = 2, TYPE_ACK = 3, TYPE_ADVERT = 4, TYPE_GRP_TXT = 5,
    TYPE_GRP_DATA = 6, TYPE_ANON_REQ = 7, TYPE_PATH = 8, TYPE_TRACE = 9, TYPE_MULTIPART = 10,
    TYPE_CONTROL = 11, TYPE_RAW_CUSTOM = 15
};
const char* typeName(uint8_t t);
const char* routeName(uint8_t r);

struct Frame {
    uint8_t        route, type, version;
    bool           hasTransport;
    uint16_t       transport1, transport2;
    uint8_t        hashSize;       // bytes per path entry, 1 or 2
    uint8_t        hops;           // entries in the path
    const uint8_t* path;
    const uint8_t* payload;
    uint8_t        payloadLen;
};
bool parse(const uint8_t* d, uint8_t len, Frame& f);

// ---- adverts ---------------------------------------------------------------
enum NodeType : uint8_t { NODE_CHAT = 1, NODE_REPEATER = 2, NODE_ROOM = 3, NODE_SENSOR = 4 };
const char* nodeTypeName(uint8_t t);

struct Advert {
    uint8_t  pubkey[32];
    uint32_t timestamp;
    uint8_t  signature[64];
    uint8_t  nodeType;
    bool     hasLatLon;
    int32_t  latE6, lonE6;
    bool     hasName;
    char     name[33];
};
// The layout only; the Ed25519 signature is carried, not checked.
bool parseAdvert(const Frame& f, Advert& a);

// ---- group channels ---------------------------------------------------------
struct Channel {
    char    name[24];
    uint8_t key[16];
    uint8_t hash;        // SHA256(key)[0]
    // Whether the key was DERIVED from the name (a hashtag channel) or
    // handed over as sixteen bytes. It decides what gets written to the
    // settings store: a derived key is not stored at all, only the tag it
    // comes from -- see lora_channels.h for why.
    bool    derived;
    // Muted: the channel stays in the list and stays stored, but decode()
    // will not try its key. The panel has no keyboard and cannot type a
    // channel name, so switching a known one off is the one useful thing a
    // finger can do to this list.
    bool    enabled;
};
// SHA256("#name")[:16]: a hashtag channel's key is its name.
void    hashtagKey(const char* nameWithHash, uint8_t key[16]);
uint8_t channelHash(const uint8_t key[16]);
// The Public channel is built in; the user may add hashtags and keys.
uint8_t        channelCount();
// A copy, not a reference: the screens read this from loop() while the radio
// task decodes on the other core, and a shared buffer between them is a race
// nobody needs. Past the end gives a zeroed, disabled channel.
Channel        channel(uint8_t i);
bool           addChannel(const char* name, const char* keyTextOrNull);   // no key: derived from "#name"
void           clearUserChannels();
// How many of the list are the user's, and how many there is room for. The
// built-in ones are the first channelCount() - userChannelCount() entries.
uint8_t        userChannelCount();
uint8_t        maxUserChannels();
// The channel of that name, or -1. Case-insensitive, because a tag typed at
// a console is typed by a person.
int            findChannel(const char* name);
// Forget one. Refuses a built-in; the entries after it shift down, counters
// and all, so an index is only good until the next call.
bool           removeChannel(uint8_t i);
// Mute or unmute. A built-in is always on: Public is the channel everything
// is on, and a list with it switched off would be a sniffer that hears less
// than a stock node.
void           setChannelEnabled(uint8_t i, bool on);
// What each channel's key has actually opened, index-for-index with
// channel(). Fed from Lora::Nodes::note(), which is the one place a frame is
// decoded exactly once -- decode() also runs for every row the screen
// redraws, so counting in there would count redraws, not traffic.
void           noteChannelHeard(uint8_t i, uint32_t nowMs);
uint32_t       channelFrames(uint8_t i);
uint32_t       channelLastMs(uint8_t i);     // millis() of the last one; 0 = never

// AES-128-ECB with a 2-byte HMAC-SHA256 in front, the way every MeshCore
// channel and message is sealed. The MAC is checked before anything is
// decrypted, so a wrong key is a clean "no" and never a wrong answer.
bool openSealed(const uint8_t key[16], const uint8_t* mac2, const uint8_t* cipher, uint8_t cipherLen,
                uint8_t* plain, uint8_t& plainLen);

struct GroupText {
    uint32_t timestamp;
    uint8_t  flags;
    char     sender[40];   // whatever came before ": " -- claimed, not verified
    char     text[160];
};
bool parseGroupText(const uint8_t* plain, uint8_t len, GroupText& g);

// A group text from us on channel `c`, sealed the way openSealed() opens one:
// a flood GRP_TXT header (0x15), no path, the channel's hash, two bytes of
// HMAC-SHA256 over the ciphertext, then AES-128-ECB over {timestamp, flags 0,
// "name: text"} zero-padded to a block. Hashtag and keyed channels alike --
// both are sixteen bytes of key by the time they are a Channel. `f` needs 5 +
// 192 bytes; a long text is cut to fit. `echo`, when given, gets what a relay's
// copy decodes to (the chat's dedupe key is made from it). Returns the frame's
// length, 0 for a muted or empty channel. Pure, for test/lora_meshcore_test.cpp.
uint16_t sealGroupText(const Channel& c, uint32_t stamp, const char* name, const char* text, uint8_t* f,
                       GroupText* echo = nullptr);

// ---- trace and control ----------------------------------------------------
// ---- direct messages (2026-10-23) ----------------------------------------------------
// MeshCore's DMs, from src/Mesh.cpp and src/helpers/BaseChatMesh.cpp there:
//
//   The two nodes' shared secret is orlp's ed25519_key_exchange of their
//   identities (LoraCrypto::ed25519KeyExchange), 32 bytes. Utils::
//   encryptThenMAC seals with it: AES-128-ECB under the first 16 bytes,
//   zero-padded, then two bytes of HMAC-SHA256 under all 32, in front.
//   A TXT_MSG's payload is dest hash, src hash (one byte of each public key),
//   then the seal of: timestamp (4, LE), (txt type << 2) | attempt, the text.
//   The receiver answers with sha256(those bytes || SENDER's public key),
//   first four bytes, plus an attempt byte and a random one: inside a PATH
//   return (dest, src, seal of path length byte, path, 0x03, the ACK) when
//   the message came by flood, or as a bare ACK frame when it came direct.
//   A node only opens a DM from a contact: it learns contacts from adverts.
//
// The secret-based seal and its opening. `out` gets mac(2) | cipher; open
// does not trim the padding (an ACK inside a PATH can end in zero bytes).
uint16_t sealSecret(const uint8_t secret[32], const uint8_t* plain, uint16_t len, uint8_t* out);
bool     openSecret(const uint8_t secret[32], const uint8_t* in, uint8_t len, uint8_t* plain, uint8_t& plainLen);
// The ACK a receiver sends for one TXT_MSG plaintext (stamp, flags, text --
// no terminator), from the sender's public key.
void     ackFor(const uint8_t* plain, size_t len, const uint8_t senderPub[32], uint8_t ack[4]);
// A TXT_MSG by flood, no path yet. The text is cut at 160 (MAX_TEXT_LEN).
// `ack` gets the four bytes the recipient will answer with. Returns the
// frame's length.
uint16_t buildDirectText(const uint8_t secret[32], const uint8_t destPub[32], const uint8_t ourPub[32],
                         uint32_t stamp, uint8_t attempt, const char* text, uint8_t* f, uint8_t ack[4]);
// The receipt for a DM that came by flood: a PATH return by flood carrying
// the path it took (pathLenByte as the frame had it) and the 6-byte ACK.
uint16_t buildPathReturn(const uint8_t secret[32], uint8_t destHash, uint8_t ourHash, uint8_t pathLenByte,
                         const uint8_t* path, const uint8_t ack[6], uint8_t* f);
// The receipt for a DM that came direct: a bare ACK, by flood (we keep no
// path to answer along).
uint16_t buildAck(const uint8_t ack[6], uint8_t* f);
// Our advert by flood: public key, timestamp, Ed25519 signature over the
// three, app data = flags 0x81 (a chat node, with a name) and the name.
uint16_t buildAdvert(const uint8_t pub[32], const uint8_t prv[64], uint32_t stamp, const char* name, uint8_t* f);
// An opened TXT_MSG plaintext.
struct DirectText { uint32_t timestamp; uint8_t txtType, attempt; char text[161]; uint8_t textLen; };
bool     parseDirectText(const uint8_t* plain, uint8_t len, DirectText& t);
// An opened PATH plaintext: the path back to its sender, and what rode along.
struct PathReturn { uint8_t pathLenByte; uint8_t hops, hashSize; const uint8_t* path; uint8_t extraType;
                    const uint8_t* extra; uint8_t extraLen; };
bool     parsePathReturn(const uint8_t* plain, uint8_t len, PathReturn& p);

struct Trace {
    uint32_t tag, auth;
    uint8_t  flags;
    uint8_t  nHashes;
    uint8_t  hashes[32];   // the repeaters asked to relay, hashSize bytes each
    uint8_t  nSnr;
    int8_t   snr4[32];     // SNR x 4 per hop so far, from the path
};
bool parseTrace(const Frame& f, Trace& t);

// ---- the whole thing ----------------------------------------------------------
struct Decoded {
    Frame     f;
    bool      haveAdvert;
    Advert    adv;
    bool      haveGroup;
    // haveChannel: a key opened the seal, whatever the payload then was.
    // haveGroup: it opened AND parsed into a sender and a line of text.
    // The two differ for a GRP_DATA, and they must, because channelIdx 0 is
    // the Public channel and not a "no channel" sentinel - a zeroed Decoded
    // would otherwise credit every undecryptable frame to Public.
    bool      haveChannel;
    uint8_t   channelIdx;
    GroupText grp;
    bool      haveTrace;
    Trace     trace;
    uint8_t   plain[192];
    // For the sealed types: the one-byte destination and source hashes.
    uint8_t   destHash, srcHash;
    uint8_t   ackCrc[4];
};
bool decode(const Lora::Packet& pk, Decoded& out);
void summary(const Lora::Packet& pk, char* out, size_t cap);

}
