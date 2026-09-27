// SquachWatch-CYD — the channel list, kept across a reboot.
//
// A sniffer that forgets its keys is a sniffer nobody carries into the field:
// thirteen hashtag channels typed in by hand at a bench are thirteen typed in
// again after every power cycle, and the first frames after a boot are the
// ones worth reading. So the user's channels go to the settings store and
// come back before the radio task exists.
//
// WHAT GETS STORED, AND WHY IT DIFFERS BY KIND:
//
//   A MeshCore HASHTAG channel is stored as its tag and nothing else. The key
//   is not a secret and not an input -- it IS the name: SHA256("#name")[0..15],
//   with the channel hash SHA256(key)[0] on top (docs/LORA.md section 3.2,
//   checked on the bench against #test giving hash 0xD9). Storing the tag
//   keeps the entry human-readable in a hex dump of NVS, keeps it short
//   (`#gelsenkirchen` is fourteen bytes against sixteen of key plus no way to
//   tell what it opens), and -- the reason that matters most -- survives a
//   change in the derivation. If MeshCore ever salts that hash, or if this
//   firmware's reading of it turns out to be wrong, a stored tag re-derives
//   correctly on the next boot while a stored key would be a fossil that
//   quietly decrypts nothing.
//
//   An EXPLICITLY KEYED channel -- MeshCore with a key pasted in, or any
//   Meshtastic channel -- has no derivation to fall back on. Its bytes are
//   the only thing worth keeping, so they are what is kept.
//
// The kind byte in front of each record is what tells the two apart on the
// way back in.
//
// encode() and replay() are pure byte work over the decoders' own tables, so
// the format is checked on a desktop (test/lora_channels_test.cpp) instead of
// being trusted to a power cycle at a bench.
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace Lora {
namespace Chan {

// The worst case, with both tables full: one format byte, then 24 MeshCore
// records of at most 42 bytes (kind, flags, name length, 23 name bytes, 16
// key bytes) and 6 Meshtastic ones of at most 59 (the same, with a key-length
// byte and 32 key bytes) -- 1363. Rounded up so a table that grows by a few
// entries does not silently start dropping the tail.
static const size_t BLOB_MAX = 1536;

// Both tables' user channels into out; returns the bytes written, which is 1
// (the format byte alone) when there is nothing to keep. Stops cleanly at cap
// rather than overrunning.
size_t  encode(uint8_t* out, size_t cap);

// The other direction: clears both user tables and re-adds what the blob
// holds. Returns how many channels came back. A blob in a format this build
// does not know is left alone and nothing is restored -- a channel list is
// not something to guess at.
uint8_t replay(const uint8_t* rec, size_t n);

// The store. save() after any change to either user table -- adding,
// dropping, muting, clearing; restore() once at boot, before the sniffer's
// task can read a frame.
// False when the list could not be written -- no scratch memory, or a store
// that refused it. The caller says so out loud; there is no point in a channel
// store that quietly is not one.
bool    save();
uint8_t restore();

// ---- named groups of open channels -----------------------------------------
// A hashtag channel holds no secret: the tag IS the key, so a list of tags is
// a list of channels anyone may join and there is nothing here that should not
// be in a source file. Which makes a named group worth having -- somebody
// reading eleven tags off a page should type one command, not eleven, and a
// console at 115200 baud with a panel in the other hand is where typos come
// from. The tags' hashes are pinned in test/lora_channels_test.cpp against the
// table in docs/LORA.md section 3.13, because a mistyped tag is not an error: it
// is a different channel that quietly hears nothing for ever.
//
// TEN GROUPS, 49 TAGS, AND ROOM FOR 24. NRW is the eleven one operator listens
// on; the other nine come from meshcore.df0x.de/api/init's `radarChannels`,
// forty names measured on 2026-09-26 -- the list the club's own visualiser
// watches, which is the list with traffic on it. They do not all fit and were
// never meant to: MeshCore::maxUserChannels() is 24, so a board carries a
// posture and not a catalogue. addGroup() adds what fits and leaves the rest,
// and the console names the free slots when it runs short. The split is by
// where and why -- BENCH, DE, AT-CH, EU, WORLD, NET, FIELD, SOCIAL, HAM -- so
// that two groups is a sensible thing to hold and three is a decision.
//
// The keys are NOT written down in the table. A tag derives its own, which is
// the whole reason a group can be a list of strings; the test does the
// deriving and pins the result.
uint8_t     groupCount();
const char* groupName(uint8_t g);
uint8_t     groupSize(uint8_t g);
const char* groupTag(uint8_t g, uint8_t i);
int         findGroup(const char* name);           // -1 when there is no such group
// Adds every channel of the group that is not already held; returns how many
// were new and leaves the rest of the list alone. Does NOT save -- like the
// decoders' own addChannel(), writing the store is the caller's call, so there
// is exactly one layer that decides when the list is written.
uint8_t     addGroup(uint8_t g);

}
}
