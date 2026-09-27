// SquachWatch-CYD — asking a public service about a node we heard.
//
// This is a passive listener sending other people's identifiers to third
// parties, so the rules come before the features.
//
// 1. NOTHING LEAVES WITHOUT AN EXPLICIT SWITCH, PER SOURCE. Three settings,
//    all OFF by default (Settings::loraLookups and the two beside it). A
//    master alone could not express "FANET yes, callsigns no", which is the
//    setting a careful operator actually wants. Compare
//    Settings::updateCheck(), same shape and default ON: that one talks to
//    first-party infrastructure about THIS board, these talk to third parties
//    about OTHER PEOPLE, and that is the whole of the difference.
//
// 2. ONLY WHAT WAS HEARD ON THE AIR, enforced by the shape of the code rather
//    than by good intentions. The queue has exactly one entrance, scan(),
//    which takes a snapshot of the node table and derives every identifier
//    from a row itself. There is no function anywhere that takes a string to
//    look up, so there is no code path by which this board can ask about
//    something it did not hear, and no future edit can add one without
//    changing a signature.
//
// 3. AN EXTRACTED CALLSIGN IS NEVER SENT. For LoRa APRS and MeshCom the
//    callsign is a field -- station identification the licence requires
//    (docs/LORA.md section 8) -- and looking it up is looking up a published
//    station ID. For Meshtastic and MeshCore it is a substring of a name a
//    stranger typed, and the chain runs: somebody's free-text name, a guessed
//    callsign, a real person's home address. Measured, not imagined: one
//    advert name from this bench yielded a token that resolved to a named
//    private individual at a street address, in one keyless GET. So plan()
//    refuses those two networks, and the host test pins the refusal.
//
// 4. THE BODY IS READ THROUGH AN ALLOW-LIST, never a deny-list. Exactly four
//    keys per source are parsed; every other byte of the response is dropped
//    unread. A deny-list would not work: the same hamrig route returns street
//    data under `addr1`/`addr2` for one backend and under `address` for
//    another, so a client dropping the two documented names still ships the
//    third to the screen.
//
// 5. EVERY REQUEST IS LOGGED WHERE THE OWNER CAN READ IT -- what was sent, to
//    which host, the status, the byte count, the time -- and the log was built
//    before the first request was issued. A device built to detect
//    surveillance should be able to prove it is not performing any.
//
// WHAT IT CANNOT DO YET. There is no ENRICH action that joins WiFi on its own.
// Joining blinds this board: promiscuous capture and station mode are mutually
// exclusive (src/detection.cpp:1548-1568), so a background lookup would turn a
// surveillance detector off, silently, for minutes, to decorate a list. Until
// the restore path has been exercised on this board -- it never has, see
// docs/CROWPANEL7.md -- requests go out only while WiFi is already up for
// another reason and the user has already accepted going deaf, which today
// means UPDATE OVER WIFI. tick() does nothing at any other time.
//
// NO HTTPS, and that is now measured rather than assumed: see the TLS note in
// docs/LORA.md. So the two sources here are the two that answer over plain
// HTTP, and the MeshCore registry -- the best lookup of the six networks --
// waits for a proxy.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "lora_nodes.h"

namespace Lora {
namespace Enrich {

// Every host this firmware may talk to about a LoRa node, in one table.
//
// SRC_HAM and SRC_OGN are QUEUE sources: one node row in, one identifier out,
// one request. SRC_MC_FEED is not -- it is a periodic fetch of a list nobody
// asked a question about, matched locally (include/lora_feed.h). It lives in
// this enum anyway, and only for one reason: LORA LOOKUPS is the audit
// surface, and a request the owner cannot see in that log might as well not
// have been logged. One table of hosts, one request log, one console command.
//
// What keeps it out of the queue is structural, not a convention: plan() has
// no case that produces it, so no Entry can ever carry it, and the host test
// pins that for every protocol. allowed() refuses it a second time, at the
// moment step() would pick one.
enum Source : uint8_t {
    SRC_HAM = 0,        // hamrig.com callsign lookup, for APRS and MeshCom only
    SRC_OGN = 1,        // the OGN device database, for FANET
    SRC_MC_FEED = 2,    // meshcore.df0x.de recent adverts -- a feed, not a lookup
    SRC_COUNT = 3,
};
const char* sourceName(uint8_t s);      // "callsign"
const char* sourceHost(uint8_t s);      // "hamrig.com"
// The path, with %s where the identifier goes. Named here so the one thing
// that reaches the network is readable in one place.
const char* sourcePath(uint8_t s);

// Minimum milliseconds between two requests to the same source.
//
// OGN: 12,000. Measured -- it serves one request, 429s the next two
// immediately, is STILL 429 four seconds later, serves again after 20 s idle
// and serves a further request 12 s after that. So 12 s is verified
// sufficient and 4 s verified insufficient; the threshold between them is
// unverified, and this must not be tuned down to find it.
// hamrig: 1,000 as a courtesy. No limit is published, which is not the same
// as none existing -- and it is the owner's own server.
// meshcore.df0x.de: 300,000. Not a rate limit -- a POLL PERIOD. Nothing on
// that server asked for one; it is the club's own box and the figure is chosen
// against what the request is worth, which is a list of the adverts the whole
// network saw in the last few minutes. See include/lora_feed.h.
uint32_t sourceSpacingMs(uint8_t s);

// ---- what would be sent for a row ------------------------------------------
// Pure, and the only place a row becomes an identifier on the wire.
struct Plan {
    uint8_t source;
    char    text[16];   // exactly what goes into the URL
};
// False when this row must not be looked up at all -- which is most rows:
// Meshtastic and MeshCore (rule 3 above), LoRaWAN (its tables are offline and
// no service maps a DevAddr to anything), a row with no identifier, and any
// row whose source is switched off.
bool plan(const Nodes::Node& n, bool allowHam, bool allowOgn, Plan& out);

// ---- what comes back --------------------------------------------------------
enum Answer : uint8_t {
    ANS_NONE = 0,       // not asked yet
    ANS_WAIT,           // in the queue
    ANS_HIT,            // answered, with fields
    ANS_MISS,           // answered: no such thing. Never asked again
    ANS_NOANSWER,       // we never managed to ask. Different from a miss, and said differently
};
const char* answerText(uint8_t a);

// The projected record: the ONLY fields that survive a response. Four per
// source, and the mapping IS the allow-list:
//
//   hamrig callsign:  country -> what, city -> where, grid_square -> grid,
//                     license_class -> extra
//   OGN device DB:    aircraft_model -> what, registration + cn -> extra,
//                     tracked -> where ("opted out" or blank)
//
// Everything else in either body -- a name, a photo URL, coordinates, a street
// address -- is never parsed, never stored, never displayed, never logged.
struct Record {
    uint64_t key;       // the row's Nodes::Node::id it belongs to
    Proto    proto;
    uint8_t  source;
    uint8_t  answer;
    uint32_t whenMs;
    char     what[24];  // "Fed. Rep. of Germany" | "Paraglider"
    char     where[20]; // "Paderborn"            | "opted out", or blank
    char     grid[8];   // "JO41ak"               | blank
    char     extra[16]; // "Class A"              | "Peak4 Nk"
};

// The parsers. Pure: bytes and an HTTP status in, a projected record out.
// `code` is the HTTP status, or negative when nothing came back.
Answer parseHamrig(const char* body, size_t len, int code, Record& out);
Answer parseOgn(const char* body, size_t len, int code, Record& out);

// ---- the queue --------------------------------------------------------------
static const uint8_t QUEUE_MAX = 16;
static const uint8_t CACHE_MAX = 96;    // one per node row
static const uint8_t LOG_MAX   = 16;

struct LogRow {
    uint32_t ms;
    uint8_t  source;
    int16_t  code;      // the HTTP status, or negative for no answer at all
    uint16_t bytes;
    char     what[16];  // the identifier that was sent, verbatim
};

// Allocates the cache. Call once, after PSRAM is up.
void begin();
// The switches, read from Settings by the caller so this module keeps no
// opinion about where they live.
void setSources(bool master, bool ham, bool ogn);
bool armed();                           // master and at least one source

// THE ONLY ENTRANCE TO THE QUEUE. Takes a copy of the node table -- see
// Lora::nodeSnapshot() -- and enqueues what is new, in priority order:
// directly-heard rows first, then by packet count, then by how recently heard.
// Deliberately not newest-first: spending a twelve-second rate-limit slot on a
// node heard once in the last minute is the worst available use of it.
// Returns how many entries the queue holds afterwards.
uint8_t scan(const Nodes::Node* rows, uint8_t n, uint32_t now);

// One request, if one is due and the transport says the network is up. Returns
// true when it issued one, which means it blocked for as long as that request
// took. This is the whole of the queue's behaviour and the host test drives it
// against a fake clock and a canned transport.
bool step(uint32_t now);

// From loop(), every pass. Cheap and non-blocking: it starts a short-lived
// worker task to call step() when there is something to ask and the network is
// up, and does nothing at all otherwise. A synchronous GET must not happen on
// loop()'s task -- that is a frame the screen loses -- and the precedent is
// ota_wifi.cpp's own "otawifi" task.
void tick(uint32_t now);
bool running();

struct Progress {
    uint8_t queued;     // still waiting for an answer
    uint8_t sent;       // requests issued this boot
    uint8_t hit, miss, noAnswer;
    uint8_t stalled;    // sources inside their spacing window right now
};
void progress(Progress& out, uint32_t now);

// What is known about a row. Copied out rather than pointed at: the records
// live in memory the worker task writes, and a pointer into them would be valid
// only until the next response landed.
bool cached(Proto p, uint64_t id, Record& out);

// One write per response, counted, because the discipline it guards is real:
// scattered PSRAM writes starve the panel's DMA and twitch the picture
// (docs/CROWPANEL7.md), so a cache record is built on the stack and copied in
// whole, once, and never touched from the frame path.
uint32_t cacheWrites();

uint8_t logCount();
bool    logAt(uint8_t i, LogRow& out);  // 0 = newest
void    clearAll();                     // the cache, the queue and the log

// ---- the transport ----------------------------------------------------------
// The one thing here that a desktop cannot test, so it is the one thing behind
// a pointer. Returns the HTTP status, or a negative number when nothing came
// back at all; writes at most `cap` bytes of body and sets `len`.
// `netUp` answers whether it is worth trying at all.
typedef int  (*Fetch)(uint8_t source, const char* text, char* body, size_t cap, size_t& len);
typedef bool (*NetUp)();
void setTransport(Fetch f, NetUp up);

// The same socket path, for the adverts feed, which is not a queue entry but
// is still a GET to a host in the table above. Shared rather than copied so
// that there is ONE place that turns a source and a string into a URL, one
// user agent, one timeout, and one answer to "what can this board connect
// to" -- and so that the canned transport a host test installs stands in for
// the feed as well. Installs the platform default on first use.
int  fetch(uint8_t source, const char* text, char* body, size_t cap, size_t& len);
bool netUp();

// One row in the request log, for a request this module did not issue itself.
// The feed calls it; nothing else does. It is a WRITE TO THE LOG and not a
// second entrance to the queue -- rule 2 is about what can be asked, and this
// cannot ask anything.
//
// It also counts toward Progress::sent, deliberately: that number answers "how
// many requests has this board made", and a request left out of it because it
// came from another module would make the answer wrong. Progress::hit, miss and
// noAnswer stay the queue's alone, so they no longer sum to sent -- they never
// did, since an entry can be in flight, and the feed's own counters are
// Feed::polls() and Feed::rowCount().
void logRequest(uint8_t source, const char* what, int code, size_t bytes, uint32_t now);

}
}
