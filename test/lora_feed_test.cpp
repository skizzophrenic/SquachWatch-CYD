// The MeshCore adverts feed -- src/lora_feed.cpp -- with a canned transport in
// place of the socket.
//
// WHAT THIS TEST IS FOR. The parser is the easy half. The half that decides
// whether this feature is honest is the MATCH, because the failure it can
// produce is not a crash and not a blank: it is a plausible name on the wrong
// node, which nobody would ever notice. A MeshCore node row pins down either
// eight key bytes (an advert this board heard in full) or ONE (a repeater seen
// only as a path hash), the feed carries thirty-two, and a one-byte prefix
// collides constantly -- a measured poll of eighty rows already held four
// first-byte triples. So the assertions that matter here are:
//
//   * two feed rows sharing a node row's prefix produce MATCH_AMBIGUOUS and NO
//     row, and the count of candidates comes back so a screen can explain the
//     blank. Never a pick.
//   * the allow-list: the fixture body carries every field the live route
//     really carries -- lat, lon, avg_rssi, avg_snr, observers, hop_count,
//     iata, first_seen, recv_ts and the feed's own `hash` -- and the assertion
//     is that NONE of it appears anywhere in a parsed Row. Written as a search
//     of the whole struct, for the reason lora_enrich_test.cpp gives: a
//     deny-list passes the fields you thought of.
//   * a request carries no identifier. What leaves the board is "24".
//
// ABOUT THE FIXTURES. The envelope, the key names and the value shapes are the
// real ones, read from http://meshcore.df0x.de/api/adverts/recent on
// 2026-09-26. The public keys and names are invented: a real key names a real
// node and there is no reason to pin a stranger's node into a test file for
// ever. `hash` is included in the fixture precisely because it is NOT usable --
// measured over eighty rows it is sixteen hex characters, distinct per row, and
// never a prefix of advert_pubkey, so it is an id of the visualiser's own and
// this parser must not be tempted by it.
#include "lora_feed.h"
#include "lora_enrich.h"
#include "test_util.h"
#include <cstring>
#include <cstdio>

using namespace Lora;

namespace {

// ---- the canned transport, the same shape lora_enrich_test.cpp installs -----
struct Canned { int code; const char* body; };
Canned g_reply = { 200, "" };
int    g_calls = 0;
char   g_lastText[32];
uint8_t g_lastSource = 255;
bool   g_netUp = true;

int fakeFetch(uint8_t source, const char* text, char* body, size_t cap, size_t& len) {
    g_calls++;
    g_lastSource = source;
    snprintf(g_lastText, sizeof g_lastText, "%s", text);
    len = 0;
    if (g_reply.body) {
        len = strlen(g_reply.body);
        if (len > cap - 1) len = cap - 1;
        memcpy(body, g_reply.body, len);
        body[len] = '\0';
    }
    return g_reply.code;
}
bool fakeUp() { return g_netUp; }

// One row, in the real envelope with every real field name, values invented.
// 65 is the first key byte of the first row and 65 of the second: the pair that
// makes the one-byte case ambiguous.
const char* THREE_ROWS =
    "{\"adverts\":["
    "{\"hash\":\"cf73aa59fe4bc03c\","
    "\"advert_pubkey\":\"65aa11223344556677889900aabbccddeeff00112233445566778899aabbccdd\","
    "\"recv_ts\":1790437083353,\"observers\":2,\"avg_snr\":7.65,\"avg_rssi\":-77.5,"
    "\"iata\":\"ZGA\",\"hop_count\":2,\"first_seen\":1790437083199,"
    "\"name\":\"Invented Hill Relay\",\"role_name\":\"repeater\","
    "\"lat\":50.720381,\"lon\":12.431848},"
    "{\"hash\":\"97f5d445270cfb25\","
    "\"advert_pubkey\":\"65bb99887766554433221100ffeeddccbbaa00112233445566778899aabbccdd\","
    "\"recv_ts\":1790437083210,\"observers\":1,\"avg_snr\":6,\"avg_rssi\":-57.25,"
    "\"iata\":null,\"hop_count\":7,\"first_seen\":1790437078040,"
    "\"name\":\"Invented Companion\",\"role_name\":\"companion\","
    "\"lat\":null,\"lon\":null},"
    "{\"hash\":\"017bffcdbd7e285b\","
    "\"advert_pubkey\":\"3c69a5ec3475c4ac0f3d7a83108eab8dd789a3e6cdb17280657aa1991e11f8c4\","
    "\"recv_ts\":1790437083199,\"observers\":1,\"avg_snr\":-10.2,\"avg_rssi\":-125,"
    "\"iata\":null,\"hop_count\":0,\"first_seen\":1790437083199,"
    "\"name\":\"Invented Room\",\"role_name\":\"room\","
    "\"lat\":60.226996,\"lon\":24.762846}"
    "]}";

// A node row, the way src/lora_nodes.cpp's noteMeshCore builds one.
Nodes::Node advertRow(uint64_t id, const char* name = nullptr) {
    Nodes::Node n;
    memset(&n, 0, sizeof n);
    n.proto = Proto::MESHCORE;
    n.id = id;
    if (name) snprintf(n.name, sizeof n.name, "%s", name);
    return n;
}
// The synthetic id noteMeshCore gives a repeater it has only ever seen as a
// path byte: 0x01 in the top byte, the hash in the low one.
Nodes::Node relayRow(uint8_t hash) {
    Nodes::Node n;
    memset(&n, 0, sizeof n);
    n.proto = Proto::MESHCORE;
    n.id = 0x0100000000000000ull | (uint64_t)hash;
    snprintf(n.tag, sizeof n.tag, "rpt %02x", hash);
    return n;
}

// Is `needle` anywhere in the bytes of a Row? The allow-list assertion.
bool inRow(const Feed::Row& r, const char* needle) {
    const size_t nl = strlen(needle);
    const char* p = (const char*)&r;
    for (size_t i = 0; i + nl <= sizeof r; i++) if (memcmp(p + i, needle, nl) == 0) return true;
    return false;
}

void reset() {
    Feed::clearAll();
    g_calls = 0;
    g_lastText[0] = '\0';
    g_lastSource = 255;
    g_netUp = true;
    g_reply = { 200, "" };
}

}  // namespace

int main() {
    Feed::begin();
    Enrich::begin();
    Enrich::setTransport(fakeFetch, fakeUp);

    suite("the prefix a node row pins down");
    {
        uint8_t pre[Feed::KEY_BYTES];
        // An advert row: all eight bytes of Node::id, big-endian, which is how
        // noteMeshCore packed pubkey[0..7] into it.
        Nodes::Node a = advertRow(0x65AA112233445566ull);
        ck("an advert row gives eight bytes", Feed::prefix(a, pre, sizeof pre) == Feed::KEY_BYTES);
        ck("and they are the key, big-endian",
           pre[0] == 0x65 && pre[1] == 0xAA && pre[6] == 0x55 && pre[7] == 0x66);
        // A relay-only row: one byte, and that byte is the MeshCore node hash,
        // which docs/LORA.md section 3.2 gives as pubkey[0].
        Nodes::Node r = relayRow(0x65);
        ck("a relay-only row gives one byte", Feed::prefix(r, pre, sizeof pre) == 1);
        ck("and it is the path hash", pre[0] == 0x65);
        // Nothing else has an Ed25519 key to take a prefix of.
        Nodes::Node other = advertRow(0x65AA112233445566ull);
        other.proto = Proto::MESHTASTIC;
        ck("a Meshtastic row gives nothing", Feed::prefix(other, pre, sizeof pre) == 0);
        other.proto = Proto::APRS;
        ck("an APRS row gives nothing", Feed::prefix(other, pre, sizeof pre) == 0);
        other.proto = Proto::LORAWAN;
        ck("a LoRaWAN row gives nothing", Feed::prefix(other, pre, sizeof pre) == 0);
        other.proto = Proto::FANET;
        ck("a FANET row gives nothing", Feed::prefix(other, pre, sizeof pre) == 0);
    }

    suite("the parser, and what it refuses to keep");
    {
        Feed::Row rows[8];
        const uint8_t n = Feed::parse(THREE_ROWS, strlen(THREE_ROWS), 200, rows, 8);
        ck("three rows out of the envelope", n == 3);
        // 65aa11223344556677... -- the first eight bytes are 65 aa 11 22 33 44 55
        // 66, and the 77 that follows is the first byte this deliberately drops.
        ck("the key is the first eight bytes of advert_pubkey",
           rows[0].key[0] == 0x65 && rows[0].key[1] == 0xAA && rows[0].key[2] == 0x11 &&
           rows[0].key[6] == 0x55 && rows[0].key[7] == 0x66);
        ck("the name comes through", strcmp(rows[0].name, "Invented Hill Relay") == 0);
        ck("and the role", strcmp(rows[0].role, "repeater") == 0);
        ck("a null lat/lon row parses like any other", strcmp(rows[1].role, "companion") == 0);
        ck("and a third role the feed really uses", strcmp(rows[2].role, "room") == 0);

        // THE ALLOW-LIST. Every one of these is in the fixture body.
        bool leaked = false;
        const char* never[] = {
            "50.72", "12.43", "60.22", "24.76",       // lat and lon
            "-77.5", "-57.25", "-125", "7.65",         // avg_rssi, avg_snr
            "1790437083353", "1790437083199",          // recv_ts, first_seen
            "ZGA",                                     // iata
            "cf73aa59fe4bc03c", "017bffcdbd7e285b",    // the feed's own hash field
        };
        for (uint8_t i = 0; i < 3; i++)
            for (size_t k = 0; k < sizeof never / sizeof never[0]; k++)
                if (inRow(rows[i], never[k])) { leaked = true; printf("      leaked: %s\n", never[k]); }
        ck("no position, signal, timestamp, iata or feed hash survives", !leaked);
        // And the positive half: eight key bytes, not thirty-two. The tail of
        // the key is in the fixture and must not be in the row.
        ck("only eight key bytes are kept", !inRow(rows[0], "\xee\xff\x00\x11"));

        // The shapes that are not an answer.
        ck("a non-200 parses to nothing", Feed::parse(THREE_ROWS, strlen(THREE_ROWS), 500, rows, 8) == 0);
        ck("so does an empty body", Feed::parse("", 0, 200, rows, 8) == 0);
        const char* notFeed = "{\"devices\":[{\"registration\":\"D-1234\"}]}";
        ck("and a body that is not this feed at all", Feed::parse(notFeed, strlen(notFeed), 200, rows, 8) == 0);
        const char* empty = "{\"adverts\":[]}";
        ck("an empty feed is zero rows, not a failure", Feed::parse(empty, strlen(empty), 200, rows, 8) == 0);
        // A key that is not 64 hex characters would match a short prefix and
        // name the wrong node, so it is dropped rather than padded.
        const char* shortKey = "{\"adverts\":[{\"advert_pubkey\":\"65aa11\",\"name\":\"Too Short\"}]}";
        ck("a short key is dropped", Feed::parse(shortKey, strlen(shortKey), 200, rows, 8) == 0);
        const char* noName = "{\"adverts\":[{\"advert_pubkey\":"
            "\"65aa11223344556677889900aabbccddeeff00112233445566778899aabbccdd\",\"name\":null}]}";
        ck("a row with no name has nothing to give", Feed::parse(noName, strlen(noName), 200, rows, 8) == 0);
        // Truncation is the expected failure of a fixed buffer against a body
        // whose length the server chooses: the rows before the cut are good.
        const size_t cut = strlen(THREE_ROWS) - 120;
        const uint8_t t = Feed::parse(THREE_ROWS, cut, 200, rows, 8);
        ck("a truncated body keeps the rows before the cut", t == 2);
        ck("and the partial row is not among them", strcmp(rows[1].name, "Invented Companion") == 0);
        // cap is honoured: the staging array is LIMIT long and no longer.
        ck("cap is honoured", Feed::parse(THREE_ROWS, strlen(THREE_ROWS), 200, rows, 1) == 1);
    }

    suite("matching a node row against the table");
    {
        reset();
        Feed::Row rows[8];
        const uint8_t n = Feed::parse(THREE_ROWS, strlen(THREE_ROWS), 200, rows, 8);
        ck("three ingested", Feed::ingest(rows, n) == 3);
        ck("and held", Feed::rowCount() == 3);

        Feed::Row got;
        uint8_t cand = 0;
        // Eight bytes: as certain as this gets, even though a SECOND row in the
        // table shares the first byte.
        Nodes::Node a = advertRow(0x65AA112233445566ull);
        ck("an eight-byte prefix names exactly one", Feed::match(a, got, cand) == Feed::MATCH_ONE);
        ck("it is the right one", strcmp(got.name, "Invented Hill Relay") == 0 && cand == 1);

        Nodes::Node b = advertRow(0x65BB998877665544ull);
        ck("the other eight-byte row too", Feed::match(b, got, cand) == Feed::MATCH_ONE &&
                                          strcmp(got.name, "Invented Companion") == 0);

        // THE CASE THIS WHOLE MODULE IS SHAPED AROUND. A relay-only row knows
        // one byte. Two feed rows start with 0x65. There is nothing in the node
        // row that can separate them, so the answer is that there is nothing.
        Nodes::Node r = relayRow(0x65);
        memset(&got, 0, sizeof got);
        ck("a one-byte prefix over two rows is ambiguous",
           Feed::match(r, got, cand) == Feed::MATCH_AMBIGUOUS);
        ck("two candidates are reported", cand == 2);
        ck("and NO row is handed back", got.name[0] == '\0' && got.key[0] == 0);
        ck("the word for it is a word", strcmp(Feed::matchText(Feed::MATCH_AMBIGUOUS), "ambiguous") == 0);

        // One byte that only one row carries is NOT a name, and this assertion
        // used to say it was. Measured on 200 live feed rows: when the heard
        // node is absent from the table, the chance that exactly one row
        // happens to share its single byte is 8.5 % at 24 rows and 25.0 % at
        // 96 - so a name printed here is wrong between 27 and 77 % of the time
        // depending on how much of the local mesh the feed carries. The answer
        // is a candidate, and no row comes back with it.
        Nodes::Node r2 = relayRow(0x3c);
        ck("a one-byte prefix over one row is a candidate, not a name",
           Feed::match(r2, got, cand) == Feed::MATCH_WEAK);
        ck("and hands back no row, so nothing can print it by accident",
           got.name[0] == '\0' && got.key[0] == 0);
        ck("it still says a candidate exists", cand == 1);
        ck("and the word for it is not 'named'",
           strcmp(Feed::matchText(Feed::MATCH_WEAK), "one-byte candidate") == 0);

        // Nothing in the table, and a protocol the feed cannot speak about.
        Nodes::Node miss = advertRow(0x1122334455667788ull);
        ck("an unknown prefix is not in the feed", Feed::match(miss, got, cand) == Feed::MATCH_NONE);
        ck("and reports no candidates", cand == 0);
        Nodes::Node mt = advertRow(0x65AA112233445566ull);
        mt.proto = Proto::MESHTASTIC;
        ck("a Meshtastic row is never matched, whatever its id",
           Feed::match(mt, got, cand) == Feed::MATCH_NONE);
    }

    suite("the table: dedup, rename, and a bound");
    {
        reset();
        Feed::Row rows[8];
        const uint8_t n = Feed::parse(THREE_ROWS, strlen(THREE_ROWS), 200, rows, 8);
        Feed::ingest(rows, n);
        ck("the same poll again adds nothing", Feed::ingest(rows, n) == 0);
        ck("and the table has not grown", Feed::rowCount() == 3);

        // A node renames itself; the row follows it rather than doubling.
        Feed::Row renamed = rows[0];
        snprintf(renamed.name, sizeof renamed.name, "%s", "Renamed Hill Relay");
        ck("a rename is not a new row", Feed::ingest(&renamed, 1) == 0);
        Feed::Row got;
        uint8_t cand = 0;
        Nodes::Node a = advertRow(0x65AA112233445566ull);
        ck("and the new name is what matches",
           Feed::match(a, got, cand) == Feed::MATCH_ONE && strcmp(got.name, "Renamed Hill Relay") == 0);

        // The bound, and the eviction order. Fill past ROW_MAX with distinct
        // keys and check the first arrivals are the ones that went.
        reset();
        Feed::Row one;
        for (uint16_t i = 0; i < Feed::ROW_MAX + 10; i++) {
            memset(&one, 0, sizeof one);
            one.key[0] = (uint8_t)(i >> 8);
            one.key[1] = (uint8_t)(i & 0xFF);
            one.key[2] = 0x5A;
            snprintf(one.name, sizeof one.name, "node %u", (unsigned)i);
            snprintf(one.role, sizeof one.role, "repeater");
            Feed::ingest(&one, 1);
        }
        ck("the table stops at its bound", Feed::rowCount() == Feed::ROW_MAX);
        ck("one write per row merged, no more",
           Feed::tableWrites() == (uint32_t)(Feed::ROW_MAX + 10));
        // Row 0 was evicted, row ROW_MAX+9 is still there.
        Nodes::Node first = advertRow(0x00005A0000000000ull);
        ck("the least recently seen went", Feed::match(first, got, cand) == Feed::MATCH_NONE);
        const uint16_t last = Feed::ROW_MAX + 9;
        Nodes::Node newest = advertRow(((uint64_t)(last >> 8) << 56) |
                                       ((uint64_t)(last & 0xFF) << 48) |
                                       ((uint64_t)0x5A << 40));
        ck("the newest is held", Feed::match(newest, got, cand) == Feed::MATCH_ONE);
        ck("newest first out of rowAt", Feed::rowAt(0, got) && strcmp(got.name, "node 105") == 0);

        // Seeing a row again saves it from eviction: that is what makes the
        // table accumulate instead of rolling with the feed's own window.
        Feed::Row again;
        memset(&again, 0, sizeof again);
        again.key[0] = 0x00; again.key[1] = 0x0B; again.key[2] = 0x5A;   // "node 11", oldest survivor
        snprintf(again.name, sizeof again.name, "node 11");
        snprintf(again.role, sizeof again.role, "repeater");
        ck("re-seeing an old row is not a new row", Feed::ingest(&again, 1) == 0);
        ck("and it is now the most recently seen",
           Feed::rowAt(0, got) && strcmp(got.name, "node 11") == 0);
        // Ten more distinct rows would have evicted it under first-arrival
        // order. Under least-recently-seen it stays and its neighbours go.
        for (uint16_t i = 500; i < 510; i++) {
            memset(&one, 0, sizeof one);
            one.key[0] = (uint8_t)(i >> 8); one.key[1] = (uint8_t)(i & 0xFF); one.key[2] = 0x5A;
            snprintf(one.name, sizeof one.name, "node %u", (unsigned)i);
            Feed::ingest(&one, 1);
        }
        Nodes::Node kept = advertRow(((uint64_t)0x00 << 56) | ((uint64_t)0x0B << 48) |
                                     ((uint64_t)0x5A << 40));
        ck("the refreshed row survived ten newcomers",
           Feed::match(kept, got, cand) == Feed::MATCH_ONE && strcmp(got.name, "node 11") == 0);
    }

    suite("what leaves the board");
    {
        reset();
        // Off by default: the switches are the caller's, and armed() is an AND.
        Feed::setArmed(false, false);
        ck("nothing polls with both off", !Feed::poll(1000) && g_calls == 0);
        Feed::setArmed(false, true);
        ck("nor with the master off", !Feed::poll(1000) && g_calls == 0);
        Feed::setArmed(true, false);
        ck("nor with the source off", !Feed::poll(1000) && g_calls == 0);
        Feed::setArmed(true, true);
        ck("armed is both", Feed::armed());

        g_netUp = false;
        ck("nor with no network", !Feed::poll(1000) && g_calls == 0);
        g_netUp = true;

        g_reply = { 200, THREE_ROWS };
        ck("armed and up, it polls", Feed::poll(1000));
        // THE PROPERTY. The request is a row count. Not a key, not a prefix,
        // not a hash, not a name -- there is no identifier in it to leak.
        ck("what was sent is the limit and nothing else", strcmp(g_lastText, "24") == 0);
        ck("to the feed's own source slot", g_lastSource == Enrich::SRC_MC_FEED);
        ck("and the rows landed", Feed::rowCount() == 3);

        // The period: a second poll inside it does not go out.
        const int before = g_calls;
        ck("a poll inside the period does not", !Feed::poll(1000 + Feed::PERIOD_MS - 1));
        ck("and no request was made", g_calls == before);
        ck("at the period it does", Feed::poll(1000 + Feed::PERIOD_MS));
        ck("two polls counted", Feed::polls() == 2);
        ck("due-in falls to zero", Feed::dueInMs(1000 + 2 * Feed::PERIOD_MS) == 0);

        // A dead server is not a reason to forget the rows already held.
        reset();
        Feed::setArmed(true, true);
        g_reply = { 200, THREE_ROWS };
        Feed::poll(1000);
        g_reply = { 503, "" };
        Feed::poll(1000 + Feed::PERIOD_MS);
        ck("a 503 keeps the table", Feed::rowCount() == 3);
        ck("and is still counted as a poll", Feed::polls() == 2);
    }

    suite("the feed is not a queue source");
    {
        // The structural half of the air-only rule. plan() is the only thing
        // that puts a source into the queue, and there is no protocol for which
        // it produces the feed -- so no Entry can ever carry it and no
        // per-identifier request can ever be sent to that host.
        bool everPlanned = false;
        for (int p = 0; p < (int)Proto::COUNT; p++) {
            Nodes::Node n;
            memset(&n, 0, sizeof n);
            n.proto = (Proto)p;
            n.id = 0x65AA112233445566ull;
            snprintf(n.tag, sizeof n.tag, "DL1TMA");
            snprintf(n.name, sizeof n.name, "DL1TMA Bergstation");
            Enrich::Plan pl;
            if (Enrich::plan(n, true, true, pl) && pl.source == Enrich::SRC_MC_FEED) everPlanned = true;
        }
        ck("plan() never routes a node row to the feed", !everPlanned);
        ck("but the host is named where the log can print it",
           strcmp(Enrich::sourceHost(Enrich::SRC_MC_FEED), "meshcore.df0x.de") == 0);
        ck("and the path interpolates a count, not an identifier",
           strstr(Enrich::sourcePath(Enrich::SRC_MC_FEED), "limit=%s") != nullptr);
    }

    suite("the request log is the audit surface, and covers the feed");
    {
        reset();
        Enrich::clearAll();
        Feed::setArmed(true, true);
        g_reply = { 200, THREE_ROWS };
        Feed::poll(5000);
        ck("the poll is in the log", Enrich::logCount() == 1);
        Enrich::LogRow l;
        ck("with the feed as its source", Enrich::logAt(0, l) && l.source == Enrich::SRC_MC_FEED);
        ck("and \"24\" as what was sent", strcmp(l.what, "24") == 0);
        ck("the byte count is the body's", l.bytes == (uint16_t)strlen(THREE_ROWS));
        ck("and the status", l.code == 200);
    }

    return report();
}
