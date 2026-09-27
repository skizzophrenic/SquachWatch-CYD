// SquachWatch-CYD — naming a MeshCore node from a feed nobody told what we heard.
//
// THE SHAPE, AND WHY IT IS NOT THE QUEUE IN lora_enrich.h.
//
// That queue is a per-identifier lookup: one node row in, one identifier out,
// one request, one answer. Its whole discipline -- rule 2, the single entrance
// that takes a Nodes::Node and derives the identifier itself -- exists because
// the request BETRAYS what this board heard. Every line of lora_enrich.cpp is
// about keeping that betrayal deliberate, per-source and logged.
//
// This source is the other shape, and it is better. The request is
//
//     GET http://meshcore.df0x.de/api/adverts/recent?limit=24
//
// -- a row count and nothing else. It carries no key, no prefix, no hash, no
// name, no position, no protocol and no timing of anything this board heard. A
// log of every such request this device ever makes, read by the server
// operator, reveals exactly one fact: that a SquachWatch asked what the network
// has been saying. The match happens afterwards, here, in RAM, against a table
// the sniffer filled off the air.
//
// That is why it is its own module and not a third Source in that queue: an
// Entry whose `text` is "24" would make LogRow::what -- documented as "the
// identifier that was sent, verbatim" -- a lie, and that log is the one thing
// standing behind the claim that this board does not leak. So the queue keeps
// its invariant, and this file keeps a poll. What IS shared is the socket
// (Enrich::fetch), the host table and the request log, because one audit
// surface that shows everything beats two that each show half.
//
// THE AIR-ONLY RULE STILL HOLDS, and for the same structural reason: match()
// takes a `const Nodes::Node&` and derives the prefix from the row itself.
// There is no function here that takes a key, a hash or a name to look for, so
// there is no code path by which a caller can ask this table about something
// the sniffer did not hear -- and because the request is a bare count, there is
// not even a request to hide it in.
//
// WHAT IT DOES NOT TAKE. Three keys per row survive: advert_pubkey, name,
// role_name. The feed also carries lat, lon, observers, avg_rssi, avg_snr,
// hop_count, iata, first_seen and recv_ts, and none of those is read. Position
// is the one worth spelling out: this device shows positions it DECODED off the
// air, and a coordinate pair from a third party's aggregator is a claim about a
// node we cannot check, arriving on the same screen line as measurements we
// can. The feed is here to supply a NAME for a row that has none. It is not
// here to fill in the columns the radio could not.
//
// THE SOURCE. meshcore.df0x.de is the owner's club server (DF0X), a MeshCore
// visualiser ingesting from analyzer.meshcorenetz.de. Measured 2026-09-26, all
// of it with curl:
//   * /api/adverts/recent?limit=N answers 200 over PLAIN HTTP with no redirect.
//     That is the entire reason this source exists: TLS does not fit this chip
//     (mbedTLS wants 36,490 B from internal RAM only -- measured,
//     tools/tls_fit_probe.sh), so an HTTPS-only source is not a source.
//   * `limit` is honoured, linearly: 325 B a row across limit=3,5,25,40,60,80
//     (939, 1,658, 8,141, 13,050, 19,565, 26,033 bytes) over a 33 B envelope.
//   * `iata`, `region` and `hours` are IGNORED -- byte-identical bodies. There
//     is no way to ask this route for a region, so the feed is global: one poll
//     brought adverts from Zwickau, Helsinki and Sweden.
//   * /api/init is 606 B and carries radarChannels, forty channel names. Those
//     are open hashtags whose keys derive offline from the name, so they are a
//     table in src/lora_channels.cpp and not a runtime fetch.
//
// SOURCES THAT WERE REJECTED, so nobody re-treads them:
//   * map.meshcore.io, the official one, serves /api/v1/nodes as a 49,574,299
//     byte dump, or one node by FULL 64-hex key over HTTPS only. This firmware
//     keeps 8 of those 32 bytes for its best rows and 1 byte for the rest, so
//     the single-node route could not be called even if TLS fitted.
//   * analyzer.meshcorenetz.de/api/nodes ignores every query parameter tried
//     and returns 28.4 MB.
//   * letsmesh.net publishes no query API and every host under it returns 403
//     to anything that is not a browser -- /robots.txt included. Unusable from
//     a device and unverifiable from a laptop.
//
// AND NOTHING IS PUBLISHED. There is no MQTT client here, no observer
// registration, no report of what this board heard going anywhere. This is a
// read-only consumer of a public list. That was the instruction and it is the
// design: "nein, keine einspeisung, wir schauen nur".
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "lora_nodes.h"

namespace Lora {
namespace Feed {

// How many leading bytes of a public key this module keeps and compares.
//
// EIGHT, because eight is every byte a node row can ever supply: an advert row
// keeps the first 8 of the 32 (src/lora_nodes.cpp's noteMeshCore packs them
// into Node::id). Keeping the other 24 would be keeping bytes that nothing can
// ever be matched against -- and it would not resolve one ambiguous case
// either: when two feed rows agree on their first eight bytes, the doubt is in
// OUR row, which has no ninth byte to offer, not in the table.
static const uint8_t KEY_BYTES = 8;

// One row, after the allow-list. Three fields, and this struct IS the
// allow-list: a field that is not here is a field that never reaches RAM this
// side of the socket buffer.
struct Row {
    uint8_t key[KEY_BYTES];
    char    name[24];    // as Nodes::Node::name -- measured longest was 31 bytes, so it truncates
    char    role[12];    // "repeater", "companion", "room" -- the three measured
};

// What the table can say about a node row. Three answers and not two: a wrong
// name is worse than no name, so "more than one row could be this node" is an
// answer of its own and never a coin toss.
enum Match : uint8_t {
    MATCH_NONE = 0,      // no row in the table starts with this node's prefix
    MATCH_ONE,           // exactly one did, on all EIGHT bytes: a name worth printing
    MATCH_WEAK,          // exactly one did, on ONE byte: a candidate, NOT a name
    MATCH_AMBIGUOUS,     // two or more did, and nothing in the node row can separate them
};
const char* matchText(uint8_t m);

// ---- the prefix a node row pins down ----------------------------------------
// src/lora_nodes.cpp's noteMeshCore makes exactly two kinds of MESHCORE row:
//
//   an ADVERT row -- Node::id is the first 8 bytes of the Ed25519 public key,
//   big-endian. Eight bytes, and a match on eight bytes is as certain as
//   anything here gets.
//
//   a RELAY-ONLY row -- a repeater seen only as a path byte, id
//   0x01 << 56 | hash, where the hash IS pubkey[0] (docs/LORA.md section 3.2:
//   "its first byte is the node's hash"). ONE byte, and one byte is not a name.
//
//   Measured on 200 live rows fetched from the feed: 135 distinct first bytes,
//   multiplicities {1:90, 2:27, 3:16, 4:2}. Given a match exists at all it is
//   unique for 90.7 % of one-byte rows against a 24-row table and 66.4 %
//   against a full 96-row one. Worse, and this is the case that decides the
//   design: when the heard node is NOT in the table, the chance that exactly
//   one table row happens to share its byte -- a confident wrong name -- is
//   8.5 % at 24 rows and 25.0 % at 96. Combining the two, the probability that
//   a displayed name is WRONG reaches 27 % if half the heard relays are in the
//   feed, 53 % at a quarter and 77 % at a tenth.
//
//   So a one-byte hit returns MATCH_WEAK and callers must not print its name.
//   An earlier version of this header quoted 95.6 % and 82.6 % and authorised
//   the display on that basis; those figures were wrong by an order of
//   magnitude once the absent-node case is counted, and the paragraph that
//   carried them cited, two lines above, the very triples that refute them.
//
// The relay-only encoding is 0x01 in the top byte with bytes 1..6 zero, a shape
// an advert row can only take with probability 2^-56. If one ever did, this
// would call it one byte and the answer would be MATCH_WEAK -- never a name.
//
// Returns how many bytes of `out` were filled: 8, 1, or 0 for a row this feed
// can say nothing about (every protocol that is not MeshCore). `cap` must be at
// least KEY_BYTES even for the one-byte answer, because which answer it is is
// not knowable before the row is read.
uint8_t prefix(const Nodes::Node& n, uint8_t* out, size_t cap);

// THE ONLY ENTRANCE. Takes a node row, derives the prefix itself, and reads the
// table. `candidates` is how many rows matched -- worth printing, because "9 of
// 96 rows start with this byte" is the honest explanation of a blank name.
// Pure read: never writes the table, so it is safe on the frame path.
Match match(const Nodes::Node& n, Row& out, uint8_t& candidates);

// ---- the request -------------------------------------------------------------
// How many rows to ask for.
//
// TWENTY-FOUR, and the cost is 7.8 kB of body (325 B a row measured, over a
// 33 B envelope; limit=25 measured 8,141 B) read into a 12 kB PSRAM buffer.
// The straight line means this is a free choice, so here is what each direction
// buys and costs:
//
//   HIGHER is better for advert rows: the feed is global and unfiltered, so
//   more rows is a better chance that the node we heard is in it at all. At 80
//   it is 26 kB a poll, at 200 it is 64,660 B measured -- and the request is
//   still one GET, so the server cost is not the limit either.
//
//   HIGHER IS WORSE for relay-only rows, and that is the part a heap figure
//   does not show. Those match on one byte, so every extra row in the TABLE is
//   another chance of a collision that turns a name into "ambiguous". The table
//   accumulates across polls, which is where the real number comes from.
//
//   So: 24 a poll, accumulating into 96 rows. The poll is small enough to read
//   into a buffer whose worst case is comfortable, and the TABLE is what does
//   the covering -- eight polls at five minutes fill it, and a board that has
//   had WiFi up for forty minutes has as many names as a single limit=96 fetch
//   would have given it, for a fifth of the peak RAM.
static const uint16_t LIMIT = 24;

// Rows held. 96: the same number as Nodes::CAP and Enrich::CACHE_MAX, and the
// same reason -- a feed row is worth keeping only if some node row could claim
// it, and there are at most 96 node rows. 96 x 48 bytes of PSRAM.
static const uint8_t ROW_MAX = 96;

// Milliseconds between polls. 300,000: NOT a rate limit -- nothing on that
// server publishes one, and it is the owner's own club box. It is what the
// request is worth. The answer is "the adverts the network saw recently", and
// on a network whose nodes re-advertise on the order of minutes, asking again
// after five buys new rows; asking again after five seconds buys the same rows
// and a server bill.
static const uint32_t PERIOD_MS = 300000;

// ---- the table ---------------------------------------------------------------
// Allocates the table and the body buffer. Call once, after PSRAM is up.
void begin();
// The switches, read from Settings by the caller so this module keeps no
// opinion about where they live. The master is the same one the queue obeys:
// ONLINE LOOKUPS off means nothing leaves, whatever this one says.
void setArmed(bool master, bool feed);
bool armed();

// Pure: a body in, rows out. The parser and the allow-list, with no table and
// no socket behind it, which is how a desktop checks both.
// `code` is the HTTP status, or negative when nothing came back. Returns how
// many rows were written, at most cap. A truncated body -- the feed is read
// into a fixed buffer and a long one WILL be cut mid-row -- yields the rows
// before the cut and drops the partial one.
uint8_t parse(const char* body, size_t len, int code, Row* out, uint8_t cap);

// Merge parsed rows into the table. A row whose prefix is already held is
// updated in place and moved to the front of the eviction order (a node may
// rename itself, and one still appearing in the feed is one still on the air);
// the rest append, evicting the least recently seen. Returns how many were new.
//
// No clock argument, deliberately: eviction is by ARRIVAL ORDER, a counter this
// module keeps, and not by a timestamp. millis() wraps at 49 days and every row
// of one poll shares a value, so a timestamp would order a poll's own rows
// arbitrarily and reverse the whole table twice a year.
uint8_t ingest(const Row* rows, uint8_t n);

uint8_t  rowCount();
bool     rowAt(uint8_t i, Row& out);       // 0 = most recently seen in a poll
uint32_t polls();                          // requests issued this boot
uint32_t rowsSeen();                       // rows parsed this boot, duplicates included
// One write per row merged, counted for the same reason Enrich::cacheWrites()
// is: scattered PSRAM writes starve the panel's DMA and twitch the picture
// (docs/CROWPANEL7.md). A row is built on the stack and copied in whole.
uint32_t tableWrites();
void     clearAll();

// One poll, if one is due and the network is up. Returns true when it issued a
// request, which means it blocked for as long as that request took -- so this
// belongs on the worker task and never on loop()'s.
bool poll(uint32_t now);

// From loop(), every pass. Cheap and non-blocking: starts a short-lived worker
// to call poll() when one is due and the network is already up for another
// reason, and does nothing at all otherwise. Same shape, and the same reason,
// as Enrich::tick() -- a synchronous GET on loop()'s task is a frame the screen
// loses.
void tick(uint32_t now);
bool running();
// Milliseconds until the next poll may go out, or 0 when one is due now.
uint32_t dueInMs(uint32_t now);

}
}
