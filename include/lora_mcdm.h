// SquachWatch-CYD — MeshCore direct messages: our identity, the contacts we
// have heard advertise, the outbox, and the receipts owed.
//
// HOW A MESHCORE DM WORKS
//
//   Every node is an Ed25519 key pair, announced in a signed advert. A node
//   only opens a DM from a CONTACT -- a key it has heard advertise (or was
//   given) -- because the one byte of sender in the frame only narrows the
//   search. So before our first DM anybody can read, our advert has to have
//   reached them, and their companion app has to have kept it (auto-add, on
//   by default). We send ours before the first DM after boot, and again
//   before a DM when three hours have passed.
//
//   The DM (MeshCore::buildDirectText) goes by flood with no path. The
//   receiver answers with a PATH return carrying the path it came by and a
//   4-byte ACK (sha256 of the plaintext and our key). No ACK in time: the
//   same text again with the attempt number in its flags (a different ACK
//   each attempt, all of them accepted), three sends in all.
//
//   A DM to us from a contact opens with our shared secret; we answer the
//   way BaseChatMesh does -- a PATH return with the ACK inside when it came
//   by flood, a bare ACK otherwise -- so the sender's tick goes green.
//
//   There is no NAK in MeshCore. A DM to somebody who never kept our advert
//   just goes unanswered, and fails after three tries.
//
// Pure: time, randomness, our name and storage come from outside, so
// test/lora_mcdm_test.cpp walks an exchange against a stand-in built from
// lora_meshcore.cpp's own pieces.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "lora_meshcore.h"

namespace Lora {
namespace McDm {

static const uint8_t  CONTACTS  = 32;
static const uint8_t  OUTBOX    = 6;
static const uint8_t  TRIES     = 3;
static const uint32_t RETRY_MS  = 12000;
static const uint32_t ADVERT_EVERY_MS = 3u * 3600u * 1000u;

// The same numbers as Lora::Dm::State, so one chat mark serves both networks.
enum State : uint8_t { ST_NONE = 0, ST_SENDING = 1, ST_DELIVERED = 2, ST_FAILED = 3 };

typedef void (*RandomFn)(uint8_t* out, size_t n);

// savedSeed: the 32 bytes seed() gave out last time, or nullptr on a first
// boot -- a fresh identity, and true so the caller stores it.
bool           begin(const uint8_t* savedSeed, RandomFn rnd);
bool           ready();
const uint8_t* publicKey();
const uint8_t* seed();          // for saving and nothing else

// The 32-bit name a conversation goes by: the first four bytes of the
// public key, big-endian -- the top half of the node table's 64-bit id.
uint32_t peerId(const uint8_t pub[32]);

// ---- contacts ------------------------------------------------------------------
// From an advert: a new key, or a known key's new name. Our own is ignored.
bool    learn(const uint8_t pub[32], const char* name);
bool    hasContact(uint32_t peer);
bool    contactName(uint32_t peer, char* out, size_t cap);
uint8_t contactCount();
bool    contactsDirty();
void    contactsSaved();
size_t  contactsBytesNeeded();
size_t  saveContacts(uint8_t* out, size_t cap);
bool    loadContacts(const uint8_t* in, size_t len);
void    clearContacts();

// ---- what the caller transmits ---------------------------------------------------
enum Job : uint8_t { J_NONE, J_DM, J_ACK, J_PATH, J_ADVERT };
struct Out {
    Job      job;
    uint32_t to;          // J_DM: the conversation
    uint32_t id;          // J_DM: its id (ours, not on the air)
    uint16_t len;
    uint8_t  frame[256];  // sealed, signed, ready
};
// The next job due. `epoch` is the wall clock's seconds (or the caller's best
// guess), stamped into DMs and adverts; `name` is ours, for the advert.
bool next(uint32_t now, uint32_t epoch, const char* name, Out& o);
// The radio did not get it out: a DM is tried again in a few seconds
// without spending a try.
void sent(const Out& o, bool ok, uint32_t now);
// Ask for our advert now (LORA SETTINGS, or the console).
void advertNow();

// ---- what came in -------------------------------------------------------------------
// A DM to us that opened: who (the conversation), their name as we know it,
// the text, and its own timestamp. For the chat ring.
typedef void (*TextFn)(uint32_t peer, const char* name, const MeshCore::DirectText& t, uint32_t now);
void onText(TextFn fn);
// Every MeshCore frame, decoded once: adverts (contacts), DMs to us (opened,
// handed to TextFn, the receipt owed), PATHs and ACKs that settle ours.
void onFrame(const MeshCore::Decoded& d, uint32_t now);

// ---- the owner's DMs ------------------------------------------------------------------
// Queued; 0 when the text is empty, `to` is not a contact, or the outbox is full.
uint32_t send(uint32_t to, const char* text, uint32_t id, uint32_t now);
State    state(uint32_t id);
typedef void (*StateFn)(uint32_t id, uint32_t to, State s);
void     onState(StateFn fn);
uint8_t  pending();

void     reset();     // for tests
}
}
