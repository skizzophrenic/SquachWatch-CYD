// SquachWatch-CYD — Meshtastic direct messages: our key pair, the keys we
// have heard, the outbox, and the ACKs owed.
//
// HOW A MESHTASTIC DM WORKS (firmware 2.5 and later)
//
//   Every node has an X25519 key pair and puts the public half in its
//   NodeInfo. A DM is sealed with AES-256-CCM under SHA-256 of the two
//   nodes' shared secret (Meshtastic::pkiSeal), sent on channel byte 0 to
//   one node with want_ack set. The receiver answers with a Routing ACK on
//   the primary channel, Data.request_id naming the packet. No ACK in time
//   and the sender sends the same packet again, three times in all.
//   A receiver that does not know the sender's key cannot open it, and
//   answers PKI_UNKNOWN_PUBKEY; since 2.5 a text DM under a channel key is
//   refused outright, so there is no unsealed fallback to offer.
//
// WHAT THIS FILE DECIDES, AND WHAT IT LEAVES
//
//   This is the bookkeeping: which key belongs to which node (first one
//   heard wins, as in NodeDB::updateUser -- a later NodeInfo with a
//   different key is ignored, so one spoofed NodeInfo cannot take over a
//   conversation), when a DM is due again, when it has failed, which ACKs
//   and NodeInfos are owed and to whom. It seals the DMs itself because it
//   holds the keys. The ACKs and NodeInfos it only asks for: the caller has
//   the channel and our name, and builds them (src/lora_sniffer.cpp).
//
//   Pure -- time, randomness and storage come in from outside -- so
//   test/lora_dm_test.cpp walks a whole exchange on a desktop.
//
//   Nothing here transmits on its own account. Every job it asks for answers
//   something: a DM the owner wrote, an ACK for a DM someone sent us, our
//   NodeInfo for a node that asked or that needs our key to read a DM we are
//   sending. The caller sends none of it while LORA TX is off.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "lora_meshtastic.h"

namespace Lora {
namespace Dm {

static const uint8_t  PEERS     = 48;          // keys kept, the same as Names
static const uint8_t  OUTBOX    = 6;           // DMs in flight at once
static const uint8_t  TRIES     = 3;           // sends of one DM, as the firmware's NUM_RELIABLE_RETX
static const uint32_t RETRY_MS  = 15000;       // between them: a LongFast frame and its ACK over three hops
static const uint32_t KEY_WAIT_MS = 60000;     // a DM waiting for the other node's key gives up after this
static const uint16_t MAX_TEXT  = 200;

enum State : uint8_t { ST_NONE = 0, ST_SENDING = 1, ST_DELIVERED = 2, ST_FAILED = 3, ST_WAIT_KEY = 4 };

// Routing error reasons that matter here (mesh.proto Routing.Error).
static const uint8_t ERR_NONE = 0, ERR_TIMEOUT = 3, ERR_MAX_RETRANSMIT = 5, ERR_NO_CHANNEL = 6,
                     ERR_PKI_FAILED = 34, ERR_PKI_UNKNOWN_PUBKEY = 35, ERR_NO_KEY = 200;   // 200: ours, "never got their key"

typedef void (*RandomFn)(uint8_t* out, size_t n);

// Our identity. savedPriv is the 32 bytes saveIdentity() gave out last
// time, or nullptr for a first boot -- then a fresh pair comes from rnd and
// begin() returns true so the caller stores it. rnd is kept for the extra
// nonces.
bool           begin(uint32_t myNum, const uint8_t* savedPriv, RandomFn rnd);
bool           ready();
uint32_t       myNum();
const uint8_t* publicKey();          // 32 bytes, for our NodeInfo
const uint8_t* privateKey();         // 32 bytes, for saving and nothing else

// ---- other nodes' keys -----------------------------------------------------------
enum Learn : uint8_t { L_NEW, L_SAME, L_MISMATCH, L_BAD, L_FULL };
Learn   learnKey(uint32_t num, const uint8_t pub[32]);
bool    hasKey(uint32_t num);
bool    forgetKey(uint32_t num);
uint8_t keyCount();
bool    keysDirty();
void    keysSaved();
size_t  keysBytesNeeded();
size_t  saveKeys(uint8_t* out, size_t cap);
bool    loadKeys(const uint8_t* in, size_t len);
void    clearKeys();

// Meshtastic::setPkiOpener's function: a channel-0 frame to us from a node
// whose key we hold.
bool open(const Meshtastic::Header& h, const uint8_t* in, uint8_t len, uint8_t* out, uint8_t& outLen);

// ---- what the caller transmits -------------------------------------------------------
enum Job : uint8_t { J_NONE, J_DM, J_ACK, J_NAK, J_NODEINFO };
struct Out {
    Job      job;
    uint32_t to;
    uint32_t id;            // J_DM: its packet id. J_ACK / J_NAK: the packet answered.
    uint8_t  hops;
    uint8_t  err;           // J_NAK's Routing error
    bool     wantResponse;  // J_NODEINFO: ask for theirs back
    uint16_t len;           // J_DM: the frame, sealed and ready
    uint8_t  frame[256];
};
// The next job due at `now`, owed answers first. hops is LORA SETTINGS'
// HOPS. False when there is nothing to do.
bool next(uint32_t now, uint8_t hops, Out& o);
// What happened to a job next() handed out: a DM the radio did not get out
// is tried again in a few seconds without spending one of its three tries.
void sent(const Out& o, bool ok, uint32_t now);

// After Meshtastic::decode(), for every frame off the air: keys from
// NodeInfos, ACKs and NAKs that settle our DMs, DMs to us that want an ACK,
// NodeInfo requests to us, and DMs to us we could not open.
void onFrame(const Meshtastic::Decoded& d, uint32_t now, uint8_t hops);

// ---- the owner's DMs ---------------------------------------------------------------------
// Queues one. `id` is its packet id (random, from the caller); returns it,
// or 0 when the text is empty, `to` is everyone or us, or the outbox is full.
// With no key for `to` yet the DM waits (ST_WAIT_KEY) while our NodeInfo
// goes to them asking for theirs.
uint32_t send(uint32_t to, const char* text, uint32_t id, uint32_t now);
State    state(uint32_t id, uint8_t* err = nullptr);
// Every change of a DM's state, for the chat row's mark. Called from inside
// whatever call caused it -- under the caller's lock.
typedef void (*StateFn)(uint32_t id, uint32_t to, State s, uint8_t err);
void     onState(StateFn fn);
uint8_t  pending();      // DMs not yet delivered or failed

// The hop limit an answer should carry: RoutingModule::getHopLimitForResponse.
uint8_t  responseHops(const Meshtastic::Header& h, uint8_t hops);

void     reset();        // for tests: no identity, no keys, nothing queued
}
}
