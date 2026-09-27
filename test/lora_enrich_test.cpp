// The online half of node enrichment -- src/lora_enrich.cpp -- with a canned
// transport in place of the socket and a fake clock in place of millis().
//
// This is the test that decides whether the feature ships, because what it
// checks is not "does a lookup work" but "can this device send something it
// should not". Three properties matter more than the rest:
//
//   * plan() refuses Meshtastic and MeshCore. A callsign extracted from a name
//     a stranger typed is never sent anywhere, and the refusal is pinned here
//     rather than left to a reviewer's memory.
//   * the parsers are an allow-list. The fixture bodies below carry the
//     personal fields the real responses carry -- a first name, a street
//     address, coordinates, a photo URL -- and the assertion is that NONE of
//     it appears anywhere in the projected record. A deny-list would pass the
//     first of those bodies and fail the second, which is exactly why this is
//     written as a search of the whole output struct.
//   * a rate limit stalls the source without consuming a try, a miss is
//     remembered so it is never asked again, and a give-up says "no answer"
//     rather than "not listed".
//
// ABOUT THE FIXTURES. The envelopes, key names and status codes are the real
// ones, taken from live requests on 2026-09-26: hamrig's
// {"success":..,"callsign":{..},"hamrig_user":{..},"source":..} wrapper, its
// two distinct miss shapes, and OGN's {"devices":[..]} wrapper. The VALUES in
// the personal fields are invented. The research that prompted this feature
// demonstrated the harm by resolving a real private individual's home address
// from one advert name, and committing that person's address into a test file
// for ever -- to prove that the code does not display addresses -- would be the
// same harm with better intentions. A fabricated address in the real field
// names tests the same property.
#include "lora_enrich.h"
#include "test_util.h"
#include <cstring>
#include <cstdio>

using namespace Lora;

// ---- the canned transport ----------------------------------------------------
namespace {

struct Canned {
    int         code;
    const char* body;
};
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

// A hit, in the envelope the live route really uses, with the personal fields
// the live route really carries and invented values in them.
const char* HAM_HIT =
    "{\"success\":true,\"callsign\":{"
    "\"callsign\":\"DL1TMA\",\"name\":\"Ficticious Operator\","
    "\"first_name\":\"Ficticious\",\"last_name\":\"Operator\","
    "\"address\":\"9 Invented Street\",\"addr1\":\"9 Invented Street\",\"addr2\":null,"
    "\"zip\":\"99999\",\"county\":null,"
    "\"city\":\"Arnsberg\",\"state\":\"NW\",\"country\":\"Germany\","
    "\"latitude\":\"51.42465500\",\"longitude\":\"8.01921800\",\"grid_square\":\"JO41ak\","
    "\"license_class\":\"Class A\",\"profile_image_url\":\"http://example.invalid/p.jpg\","
    "\"verification_status\":\"QRZ_VERIFIED\",\"source\":\"qrz\"},"
    "\"hamrig_user\":{\"is_hamrig_user\":true,\"city\":\"Somewhere Else\"},"
    "\"source\":\"qrz\",\"timestamp\":\"2026-09-26T13:29:48+00:00\"}";

// Miss shape 1: HTTP 404, 30 bytes. Verified live.
const char* HAM_404 = "{\"error\":\"Callsign not found\"}";

// Miss shape 2: HTTP 200, every field the literal string NOT_FOUND, source
// hamdb -- and a field list that includes `address`. Verified live, byte for
// byte apart from the timestamp.
const char* HAM_NOTFOUND =
    "{\"success\":true,\"callsign\":{\"callsign\":\"DL0ZZZQ\",\"first_name\":\"NOT_FOUND\","
    "\"last_name\":\"NOT_FOUND\",\"address\":\"NOT_FOUND\",\"city\":\"NOT_FOUND\","
    "\"state\":\"NOT_FOUND\",\"zip\":\"NOT_FOUND\",\"country\":\"NOT_FOUND\","
    "\"grid_square\":\"NOT_FOUND\",\"license_class\":\"NOT_FOUND\",\"source\":\"hamdb\"},"
    "\"hamrig_user\":{\"is_hamrig_user\":false},\"source\":\"hamdb\","
    "\"timestamp\":\"2026-09-26T13:29:48+00:00\"}";

// The OGN reply, 148 bytes, envelope included. Verified live.
const char* OGN_HIT =
    "{\"devices\":[{\"device_type\":\"F\",\"device_id\":\"11000C\",\"aircraft_model\":\"Paraglider\","
    "\"registration\":\"Peak4\",\"cn\":\"Nk\",\"tracked\":\"Y\",\"identified\":\"Y\"}]}";
// A pilot who opted out. The server blanks the identity fields itself -- all
// 482 such rows in the dump carry no registration, cn or model -- so this is
// what one really looks like.
const char* OGN_OPTOUT =
    "{\"devices\":[{\"device_type\":\"F\",\"device_id\":\"11000D\",\"aircraft_model\":\"\","
    "\"registration\":\"\",\"cn\":\"\",\"tracked\":\"N\",\"identified\":\"N\"}]}";
const char* OGN_EMPTY = "{\"devices\":[]}";

// Every byte of a record, as one string, so a test can assert that a substring
// is nowhere in it.
void flatten(const Enrich::Record& r, char* out, size_t cap) {
    snprintf(out, cap, "%s|%s|%s|%s", r.what, r.where, r.grid, r.extra);
}
bool absent(const Enrich::Record& r, const char* needle) {
    char flat[96];
    flatten(r, flat, sizeof flat);
    return strstr(flat, needle) == nullptr;
}

Nodes::Node row(Proto p, uint64_t id, const char* tag, const char* name,
                uint16_t packets = 1, uint16_t direct = 1, uint32_t lastMs = 1000) {
    Nodes::Node n;
    memset(&n, 0, sizeof n);
    n.proto = p; n.id = id;
    if (tag) snprintf(n.tag, sizeof n.tag, "%s", tag);
    if (name) snprintf(n.name, sizeof n.name, "%s", name);
    n.packets = packets; n.directPackets = direct; n.lastMs = lastMs;
    return n;
}

void reset() {
    Enrich::clearAll();
    g_calls = 0;
    g_lastText[0] = '\0';
    g_lastSource = 255;
    g_netUp = true;
}

}  // namespace

int main() {
    Enrich::begin();
    Enrich::setTransport(fakeFetch, fakeUp);

    suite("plan(): what may be asked about, and what may never be");
    {
        Enrich::Plan p;
        // THE REFUSAL THAT MATTERS. A MeshCore advert name with a perfectly
        // good callsign in it, and the answer is still no.
        Nodes::Node mc = row(Proto::MESHCORE, 0x61A1B7A7437459FBull, "61a1b7", "DE-NW-HSK-AR-DL1TMA");
        ck("a MeshCore advert name is never looked up", !Enrich::plan(mc, true, true, p));
        Nodes::Node mt = row(Proto::MESHTASTIC, 0x3B1CD9ECull, "!3b1cd9ec", "DL1TMA Bergstation");
        ck("a Meshtastic long name is never looked up", !Enrich::plan(mt, true, true, p));

        // APRS and MeshCom: the callsign is a field, transmitted as station
        // identification, and it goes without the SSID.
        Nodes::Node ap = row(Proto::APRS, 1, "DH5DAX-7", nullptr);
        ck("an APRS source callsign is asked about", Enrich::plan(ap, true, true, p) &&
                                                     p.source == Enrich::SRC_HAM);
        ck("...without the SSID", strcmp(p.text, "DH5DAX") == 0);
        Nodes::Node mcm = row(Proto::MESHCOM, 2, "OE1KBC", nullptr);
        ck("a MeshCom source callsign is asked about", Enrich::plan(mcm, true, true, p) &&
                                                       strcmp(p.text, "OE1KBC") == 0);

        // FANET: the row id IS the OGN address, manufacturer in the high byte.
        Nodes::Node fa = row(Proto::FANET, (0x11ull << 16) | 0x000C, "11:000C", nullptr);
        ck("a FANET address becomes an OGN device id", Enrich::plan(fa, true, true, p) &&
                                                       p.source == Enrich::SRC_OGN &&
                                                       strcmp(p.text, "11000C") == 0);

        // LoRaWAN is answered from flash, never from a server.
        Nodes::Node lw = row(Proto::LORAWAN, 0x26011B2Cull, "26011b2c", "The Things Network");
        ck("a LoRaWAN DevAddr is never sent anywhere", !Enrich::plan(lw, true, true, p));

        // The switches, at the level of a single source.
        ck("callsigns off means no callsign request", !Enrich::plan(ap, false, true, p));
        ck("OGN off means no OGN request", !Enrich::plan(fa, true, false, p));
        ck("both off means nothing at all", !Enrich::plan(ap, false, false, p) &&
                                            !Enrich::plan(fa, false, false, p));
        Nodes::Node stub = row(Proto::APRS, 3, "X1", nullptr);
        ck("a tag too short to be a callsign is not sent", !Enrich::plan(stub, true, true, p));
    }

    suite("parseHamrig(): four keys out, everything else dropped");
    {
        Enrich::Record r;
        memset(&r, 0, sizeof r);
        ck("a hit is a hit", Enrich::parseHamrig(HAM_HIT, strlen(HAM_HIT), 200, r) == Enrich::ANS_HIT);
        ck("country", strcmp(r.what, "Germany") == 0);
        ck("city", strcmp(r.where, "Arnsberg") == 0);
        ck("grid square", strcmp(r.grid, "JO41ak") == 0);
        ck("licence class", strcmp(r.extra, "Class A") == 0);
        // THE ALLOW-LIST, PROVEN RATHER THAN INSPECTED. A deny-list of the two
        // documented address field names would let the third one through.
        ck("no street address (`address`)", absent(r, "Invented"));
        ck("no street address (`addr1`)", absent(r, "9 Invented Street"));
        ck("no postcode", absent(r, "99999"));
        ck("no first name", absent(r, "Ficticious"));
        ck("no surname", absent(r, "Operator"));
        ck("no coordinates", absent(r, "51.42") && absent(r, "8.019"));
        ck("no photo URL", absent(r, "example.invalid") && absent(r, "http"));
        // The envelope matters: hamrig_user is a sibling object with a city of
        // its own, and a whole-body scan would have taken the wrong one.
        ck("the city came from the callsign object, not its sibling", absent(r, "Somewhere Else"));

        memset(&r, 0, sizeof r);
        ck("404 is a miss", Enrich::parseHamrig(HAM_404, strlen(HAM_404), 404, r) == Enrich::ANS_MISS);
        ck("...and carries nothing", r.what[0] == '\0' && r.where[0] == '\0');
        memset(&r, 0, sizeof r);
        ck("200 with NOT_FOUND everywhere is also a miss",
           Enrich::parseHamrig(HAM_NOTFOUND, strlen(HAM_NOTFOUND), 200, r) == Enrich::ANS_MISS);
        ck("...and does not put NOT_FOUND on the screen",
           absent(r, "NOT_FOUND") && r.what[0] == '\0');
        memset(&r, 0, sizeof r);
        ck("429 is a failure to ask, not a miss",
           Enrich::parseHamrig("", 0, 429, r) == Enrich::ANS_NOANSWER);
        memset(&r, 0, sizeof r);
        ck("no answer at all is a failure to ask",
           Enrich::parseHamrig(nullptr, 0, -1, r) == Enrich::ANS_NOANSWER);
    }

    suite("parseOgn(): an aircraft, and a pilot who said no");
    {
        Enrich::Record r;
        memset(&r, 0, sizeof r);
        ck("a hit is a hit", Enrich::parseOgn(OGN_HIT, strlen(OGN_HIT), 200, r) == Enrich::ANS_HIT);
        ck("aircraft model", strcmp(r.what, "Paraglider") == 0);
        ck("registration and competition number", strcmp(r.extra, "Peak4 Nk") == 0);
        memset(&r, 0, sizeof r);
        ck("tracked=N reads as opted out",
           Enrich::parseOgn(OGN_OPTOUT, strlen(OGN_OPTOUT), 200, r) == Enrich::ANS_HIT &&
           strcmp(r.where, "opted out") == 0);
        ck("...and carries no identity and no position",
           r.what[0] == '\0' && r.extra[0] == '\0' && r.grid[0] == '\0');
        memset(&r, 0, sizeof r);
        ck("an unregistered address is a miss",
           Enrich::parseOgn(OGN_EMPTY, strlen(OGN_EMPTY), 200, r) == Enrich::ANS_MISS);
        memset(&r, 0, sizeof r);
        ck("429 is a failure to ask", Enrich::parseOgn("", 0, 429, r) == Enrich::ANS_NOANSWER);
    }

    suite("the queue: order, and one entrance");
    {
        reset();
        Enrich::setSources(true, true, true);
        // Four rows worth asking about, deliberately in the wrong order, plus
        // two that must never be enqueued at all.
        Nodes::Node rows[6] = {
            row(Proto::APRS,  10, "DL0AAA", nullptr, /*pkts*/ 1, /*direct*/ 0, /*last*/ 9000),
            row(Proto::APRS,  11, "DL0BBB", nullptr, 200, 0, 5000),
            row(Proto::APRS,  12, "DL0CCC", nullptr, 3, 3, 1000),
            row(Proto::APRS,  13, "DL0DDD", nullptr, 99, 99, 2000),
            row(Proto::MESHCORE, 14, "61a1b7", "DE-NW-HSK-AR-DL1TMA"),
            row(Proto::LORAWAN, 15, "26011b2c", "The Things Network"),
        };
        ck("only the four askable rows are queued", Enrich::scan(rows, 6, 1000) == 4);
        // Directly heard beats relayed, whatever the packet count; then the
        // busiest; then the most recent.
        g_reply = { 404, HAM_404 };
        Enrich::step(2000);
        ck("the directly-heard 99-packet row goes first", strcmp(g_lastText, "DL0DDD") == 0);
        Enrich::step(2000 + 1000);
        ck("then the other directly-heard row", strcmp(g_lastText, "DL0CCC") == 0);
        Enrich::step(2000 + 2000);
        ck("then the relayed row with 200 packets", strcmp(g_lastText, "DL0BBB") == 0);
        Enrich::step(2000 + 3000);
        ck("then the relayed row heard once", strcmp(g_lastText, "DL0AAA") == 0);
        Enrich::Progress pr;
        Enrich::progress(pr, 2000 + 3000);
        ck("the queue is empty and four misses are remembered",
           pr.queued == 0 && pr.miss == 4 && pr.sent == 4);
        // A miss is permanent: a second scan of the same table asks nothing.
        ck("a miss is never re-enqueued", Enrich::scan(rows, 6, 60000) == 0);
        const int before = g_calls;
        ck("...and nothing goes out for it", !Enrich::step(120000) && g_calls == before);
    }

    suite("rate limits: the spacing, and what a 429 costs");
    {
        reset();
        Enrich::setSources(true, false, true);
        Nodes::Node rows[2] = {
            row(Proto::FANET, (0x11ull << 16) | 0x000C, "11:000C", nullptr, 5, 5, 1000),
            row(Proto::FANET, (0x11ull << 16) | 0x000D, "11:000D", nullptr, 4, 4, 1000),
        };
        Enrich::scan(rows, 2, 1000);
        g_reply = { 200, OGN_HIT };
        ck("the first request goes out", Enrich::step(1000) && g_calls == 1);
        // 12 seconds, measured: 4 s was verified insufficient against this
        // server and 12 s verified sufficient.
        ck("a second is refused one second later", !Enrich::step(2000) && g_calls == 1);
        ck("still refused four seconds later", !Enrich::step(5000) && g_calls == 1);
        ck("allowed after twelve", Enrich::step(13001) && g_calls == 2);

        // A 429 stalls the SOURCE and does not consume a try, so the entry is
        // still asked four more times if it has to be.
        reset();
        Enrich::setSources(true, false, true);
        Enrich::scan(rows, 1, 1000);
        g_reply = { 429, "" };
        ck("a 429 is issued and counted", Enrich::step(1000) && g_calls == 1);
        ck("the source is stalled for its spacing", !Enrich::step(6000));
        g_reply = { 200, OGN_HIT };
        ck("and the entry is still there to ask again", Enrich::step(14000) && g_calls == 2);
        Enrich::Progress pr;
        Enrich::progress(pr, 14000);
        ck("the answer landed after the stall", pr.hit == 1 && pr.queued == 0);
    }

    suite("failures: four goes, then no answer -- which is not a miss");
    {
        reset();
        Enrich::setSources(true, true, false);
        Nodes::Node r1 = row(Proto::APRS, 20, "DL0EEE", nullptr);
        Enrich::scan(&r1, 1, 1000);
        g_reply = { 500, "" };
        uint32_t t = 1000;
        ck("try 1", Enrich::step(t) && g_calls == 1);
        ck("not retried a second later", !Enrich::step(t + 1000));
        t += 60001;
        ck("try 2 after a minute", Enrich::step(t) && g_calls == 2);
        ck("not retried a minute later", !Enrich::step(t + 60000));
        t += 120001;
        ck("try 3 after two minutes", Enrich::step(t) && g_calls == 3);
        t += 240001;
        ck("try 4 after four minutes", Enrich::step(t) && g_calls == 4);
        ck("and then it stops", !Enrich::step(t + 3600000) && g_calls == 4);
        Enrich::Progress pr;
        Enrich::progress(pr, t);
        ck("the row is remembered as no answer, not as a miss",
           pr.noAnswer == 1 && pr.miss == 0 && pr.queued == 0);
        Enrich::Record rec;
        ck("...and the word on the screen says so",
           Enrich::cached(Proto::APRS, 20, rec) && rec.answer == Enrich::ANS_NOANSWER &&
           strcmp(Enrich::answerText(rec.answer), "no answer") == 0);
        ck("a miss and a no-answer do not read alike",
           strcmp(Enrich::answerText(Enrich::ANS_MISS), Enrich::answerText(Enrich::ANS_NOANSWER)) != 0);
    }

    suite("the switches stop what is already queued");
    {
        reset();
        Enrich::setSources(true, true, true);
        Nodes::Node rows[2] = {
            row(Proto::APRS, 30, "DL0FFF", nullptr, 9, 9, 1000),
            row(Proto::FANET, (0x11ull << 16) | 0x000C, "11:000C", nullptr, 8, 8, 1000),
        };
        ck("two queued", Enrich::scan(rows, 2, 1000) == 2);
        Enrich::setSources(true, false, true);          // callsigns off, mid-queue
        g_reply = { 200, OGN_HIT };
        ck("the OGN entry still goes", Enrich::step(1000) && g_lastSource == Enrich::SRC_OGN);
        ck("the callsign entry does not", !Enrich::step(30000) && g_calls == 1);
        Enrich::setSources(false, true, true);          // the master, off
        ck("the master switch stops everything", !Enrich::step(60000) && g_calls == 1);
        ck("and armed() says so", !Enrich::armed());
        // Nothing is asked while the network is down, and nothing is lost.
        reset();
        Enrich::setSources(true, true, false);
        Enrich::scan(rows, 2, 1000);
        g_netUp = false;
        ck("no network, no request", !Enrich::step(1000) && g_calls == 0);
        g_netUp = true;
        g_reply = { 200, HAM_HIT };
        ck("the network comes back and the entry is still there", Enrich::step(2000) && g_calls == 1);
    }

    suite("the log: one row per request, before anything is read");
    {
        reset();
        ck("it starts empty", Enrich::logCount() == 0);
        Enrich::setSources(true, true, false);
        Nodes::Node r1 = row(Proto::APRS, 40, "DL0GGG", nullptr);
        Enrich::scan(&r1, 1, 1000);
        g_reply = { 200, HAM_HIT };
        Enrich::step(1000);
        Enrich::LogRow l;
        ck("one row", Enrich::logCount() == 1 && Enrich::logAt(0, l));
        ck("the identifier that was sent, verbatim", strcmp(l.what, "DL0GGG") == 0);
        ck("the host it went to", strcmp(Enrich::sourceHost(l.source), "hamrig.com") == 0);
        ck("the status", l.code == 200);
        ck("the byte count", l.bytes == strlen(HAM_HIT));
        ck("the time", l.ms == 1000);
        // Nothing enters the log that did not leave the device: a row that was
        // never sent leaves no trace, and an empty log is a provable claim.
        reset();
        Enrich::setSources(true, true, true);
        Nodes::Node mc = row(Proto::MESHCORE, 41, "61a1b7", "DE-NW-HSK-AR-DL1TMA");
        Enrich::scan(&mc, 1, 1000);
        Enrich::step(1000);
        ck("a MeshCore row leaves the log empty", Enrich::logCount() == 0 && g_calls == 0);
    }

    suite("the cache: one write per response");
    {
        reset();
        Enrich::setSources(true, true, false);
        const uint32_t before = Enrich::cacheWrites();
        Nodes::Node r1 = row(Proto::APRS, 50, "DL0HHH", nullptr);
        Enrich::scan(&r1, 1, 1000);
        g_reply = { 200, HAM_HIT };
        Enrich::step(1000);
        // The panel twitches on scattered PSRAM writes, which is why the
        // record is built on the stack and copied in whole, once.
        ck("exactly one write", Enrich::cacheWrites() == before + 1);
        Enrich::Record rec;
        ck("and it is readable", Enrich::cached(Proto::APRS, 50, rec) &&
                                 rec.answer == Enrich::ANS_HIT &&
                                 strcmp(rec.what, "Germany") == 0);
        ck("a row nobody asked about has no record", !Enrich::cached(Proto::APRS, 51, rec));
        ck("and neither does the same id on another network",
           !Enrich::cached(Proto::FANET, 50, rec));
    }

    suite("the queue is bounded, and keeps the best");
    {
        reset();
        Enrich::setSources(true, true, false);
        Nodes::Node many[40];
        for (int i = 0; i < 40; i++) {
            char tag[12];
            snprintf(tag, sizeof tag, "DL%d%s", i, i % 10 == 0 ? "AA" : "BB");
            // The last few are the busiest and the only ones heard directly.
            many[i] = row(Proto::APRS, (uint64_t)(100 + i), tag, nullptr,
                          (uint16_t)i, (uint16_t)(i > 35 ? 1 : 0), 1000);
        }
        const uint8_t n = Enrich::scan(many, 40, 1000);
        ck("no more than the queue holds", n == Enrich::QUEUE_MAX);
        g_reply = { 404, HAM_404 };
        Enrich::step(1000);
        ck("and the busiest directly-heard row is first out", strcmp(g_lastText, "DL39BB") == 0);
    }

    return report();
}
