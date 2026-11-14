// Meshtastic direct messages -- src/lora_dm.cpp -- walked end to end against
// a stand-in for a stock node built from lora_meshtastic.cpp's own pieces:
// the key swap, a DM out and its ACK, a DM in and the ACK owed, retries, a
// DM that never gets an answer, a sender we have no key for, and a second key
// for a node we already know (ignored, as NodeDB::updateUser does).
//
// The sealing itself is pinned to an independent library's bytes in
// test/lora_meshtastic_test.cpp; this is about the bookkeeping around it.
#include "lora_dm.h"
#include "lora_meshtastic.h"
#include "lora_crypto.h"
#include "test_util.h"
#include <cstring>
#include <cstdio>

using namespace Meshtastic;
namespace Dm = Lora::Dm;

static const uint32_t ME = 0x5A5A1234u, THEM = 0x27cc63ccu, STRANGER = 0x11112222u;
static uint8_t s_theirPriv[32], s_theirPub[32];

// Deterministic "randomness": a counter, so every run seals the same bytes.
static uint8_t s_rndCtr = 0;
static void rnd(uint8_t* out, size_t n) { for (size_t i = 0; i < n; i++) out[i] = (uint8_t)(0xA0 + s_rndCtr++); }

static Lora::Packet pk;
static void load(const uint8_t* f, uint16_t n) { memset(&pk, 0, sizeof pk); memcpy(pk.data, f, n); pk.len = (uint8_t)n; }

// Their NodeInfo: User{id, long name, short name, hw 255, public key}, to `to`.
static uint16_t theirNodeInfo(uint32_t from, const uint8_t* pub, uint32_t to, bool wantResponse, uint32_t id, uint8_t* f) {
    uint8_t user[96]; size_t u = 0;
    const char* idS = "!27cc63cc"; const char* ln = "Tester"; const char* sn = "TST";
    user[u++] = 0x0A; user[u++] = (uint8_t)strlen(idS); memcpy(user + u, idS, strlen(idS)); u += strlen(idS);
    user[u++] = 0x12; user[u++] = (uint8_t)strlen(ln);  memcpy(user + u, ln, strlen(ln));   u += strlen(ln);
    user[u++] = 0x1A; user[u++] = (uint8_t)strlen(sn);  memcpy(user + u, sn, strlen(sn));   u += strlen(sn);
    user[u++] = 0x28; user[u++] = 0xFF; user[u++] = 0x01;
    if (pub) { user[u++] = 0x42; user[u++] = 32; memcpy(user + u, pub, 32); u += 32; }
    uint8_t data[160];
    const size_t dn = encodeData(PORT_NODEINFO, user, u, wantResponse, 0, data);
    return buildChannelFrame(channel(0), from, to, id, 3, false, data, dn, f);
}

// Their ACK (or NAK) of packet `req`, on the primary channel.
static uint16_t theirRouting(uint32_t req, uint8_t err, uint8_t* f) {
    const uint8_t routing[2] = { 0x18, err };
    uint8_t data[32];
    const size_t dn = encodeData(PORT_ROUTING, routing, 2, false, req, data);
    return buildChannelFrame(channel(0), THEM, ME, req ^ 0x777, 3, false, data, dn, f);
}

static uint32_t s_lastId = 0; static Dm::State s_lastState = Dm::ST_NONE; static int s_calls = 0;
static void onState(uint32_t id, uint32_t, Dm::State s, uint8_t) { s_lastId = id; s_lastState = s; s_calls++; }

int main() {
    LoraCrypto::fromHex("404142434445464748494a4b4c4d4e4f505152535455565758595a5b5c5d5e5f", s_theirPriv, 32);
    LoraCrypto::x25519Base(s_theirPub, s_theirPriv);
    Meshtastic::setPkiOpener(Dm::open);

    suite("Our key pair");
    {
        Dm::reset();
        ck("a first boot makes one and says to keep it", Dm::begin(ME, nullptr, rnd) && Dm::ready());
        uint8_t saved[32], pub[32];
        memcpy(saved, Dm::privateKey(), 32); memcpy(pub, Dm::publicKey(), 32);
        uint8_t check[32]; LoraCrypto::x25519Base(check, saved);
        ck("the public half belongs to the private half", !memcmp(check, pub, 32));
        Dm::reset();
        ck("the next boot loads it and says nothing to save", !Dm::begin(ME, saved, rnd));
        ck("and it is the same key", !memcmp(Dm::publicKey(), pub, 32));
        Dm::onState(onState);
    }

    uint8_t f[256];
    Dm::Out o;
    uint32_t now = 100000;

    suite("A DM to a node whose key we do not have yet");
    {
        const uint32_t id = Dm::send(THEM, "anyone home?", 0x1001, now);
        ck("queued, waiting for their key", id == 0x1001 && Dm::state(id) == Dm::ST_WAIT_KEY && s_lastState == Dm::ST_WAIT_KEY);
        ck("first job: our NodeInfo to them, asking for theirs",
           Dm::next(now, 3, o) && o.job == Dm::J_NODEINFO && o.to == THEM && o.wantResponse);
        ck("and nothing else is due yet", !Dm::next(now + 1000, 3, o));
        ck("not asked again within the gap", !Dm::next(now + 10000, 3, o));

        // Their NodeInfo comes back with their key.
        load(f, theirNodeInfo(THEM, s_theirPub, ME, false, 0x2001, f));
        Decoded d;
        ck("their NodeInfo decodes with a key", decode(pk, d) && d.haveData && d.user.hasPubkey);
        Dm::onFrame(d, now + 12000, 3);
        ck("the key is learned", Dm::hasKey(THEM) && Dm::keyCount() == 1 && Dm::keysDirty());

        ck("now the DM goes out", Dm::next(now + 12000, 3, o) && o.job == Dm::J_DM && o.to == THEM && o.id == 0x1001 && o.len > 16);
        ck("as SENDING", Dm::state(0x1001) == Dm::ST_SENDING);
        // They open it with their own key and our public one.
        Header h; parseHeader(o.frame, (uint8_t)o.len, h);
        ck("to them, from us, channel 0, want_ack", h.to == THEM && h.from == ME && h.channelHash == 0 && h.wantAck && h.id == 0x1001);
        uint8_t k[32], plain[240];
        ck("their key agrees", pkiKey(s_theirPriv, Dm::publicKey(), k));
        const int pn = pkiOpen(k, ME, 0x1001, o.frame + 16, (size_t)o.len - 16, plain);
        Data dd;
        ck("and the text inside is ours", pn > 0 && parseData(plain, (uint8_t)pn, dd) && dd.portnum == PORT_TEXT &&
           dd.payloadLen == 12 && !memcmp(dd.payload, "anyone home?", 12));

        // Their ACK.
        load(f, theirRouting(0x1001, 0, f));
        ck("their ACK decodes", decode(pk, d) && d.haveData && d.data.portnum == PORT_ROUTING);
        const int before = s_calls;
        Dm::onFrame(d, now + 14000, 3);
        ck("DELIVERED", Dm::state(0x1001) == Dm::ST_DELIVERED && s_lastId == 0x1001 && s_lastState == Dm::ST_DELIVERED && s_calls == before + 1);
        ck("and no resend is due", !Dm::next(now + 40000, 3, o));
    }

    suite("A DM nobody answers");
    {
        now = 200000;
        Dm::send(THEM, "hello?", 0x1002, now);
        int sends = 0;
        for (uint32_t t = now; t < now + 70000; t += 1000)
            while (Dm::next(t, 3, o)) if (o.job == Dm::J_DM && o.id == 0x1002) { sends++; Dm::sent(o, true, t); }
        uint8_t err = 0;
        ck("sent three times in all", sends == 3);
        ck("then FAILED, out of retries", Dm::state(0x1002, &err) == Dm::ST_FAILED && err == Dm::ERR_MAX_RETRANSMIT);

        Dm::send(THEM, "radio busy", 0x1003, now);
        ck("a send the radio dropped", Dm::next(now, 3, o) && o.job == Dm::J_DM);
        Dm::sent(o, false, now);
        ck("is tried again soon", !Dm::next(now + 1000, 3, o) && Dm::next(now + 3000, 3, o) && o.job == Dm::J_DM);
        Dm::sent(o, true, now + 3000);
        int more = 0;
        for (uint32_t t = now + 3000; t < now + 80000; t += 1000)
            while (Dm::next(t, 3, o)) if (o.job == Dm::J_DM && o.id == 0x1003) more++;
        ck("without losing one of its three tries to the radio", more == 2);
    }

    suite("A DM to us");
    {
        now = 300000;
        uint8_t k[32]; pkiKey(s_theirPriv, Dm::publicKey(), k);
        uint8_t data[220];
        const size_t dn = encodeData(PORT_TEXT, (const uint8_t*)"meet at the big rock", 20, false, 0, data);
        const uint16_t n = buildPkiFrame(k, THEM, ME, 0x3001, 3, true, 0xCAFE, data, dn, f);
        load(f, n);
        Decoded d;
        ck("opens, as a DM", decode(pk, d) && d.haveData && d.pki && !strcmp(d.text, "meet at the big rock"));
        Dm::onFrame(d, now, 3);
        ck("an ACK is owed to them for that packet", Dm::next(now, 3, o) && o.job == Dm::J_ACK && o.to == THEM && o.id == 0x3001);
        Dm::onFrame(d, now + 5000, 3);
        ck("their retry is ACKed again (the first ACK was lost)", Dm::next(now + 5000, 3, o) && o.job == Dm::J_ACK && o.id == 0x3001);

        // The same frame to somebody else: not ours.
        const uint16_t n2 = buildPkiFrame(k, THEM, 0x99998888u, 0x3002, 3, true, 0xCAFE, data, dn, f);
        load(f, n2);
        ck("a DM between two other nodes stays shut", decode(pk, d) && !d.haveData);
    }

    suite("A DM to us from a node we have no key for");
    {
        now = 400000;
        uint8_t strangerPriv[32], strangerPub[32], k[32], data[64];
        memset(strangerPriv, 0x33, 32);
        LoraCrypto::x25519Base(strangerPub, strangerPriv);
        pkiKey(strangerPriv, Dm::publicKey(), k);
        const size_t dn = encodeData(PORT_TEXT, (const uint8_t*)"who dis", 7, false, 0, data);
        load(f, buildPkiFrame(k, STRANGER, ME, 0x4001, 3, true, 1, data, dn, f));
        Decoded d;
        ck("cannot be opened", decode(pk, d) && !d.haveData);
        Dm::onFrame(d, now, 3);
        bool nak = false, ask = false;
        while (Dm::next(now, 3, o)) {
            if (o.job == Dm::J_NAK && o.to == STRANGER && o.id == 0x4001 && o.err == Dm::ERR_PKI_UNKNOWN_PUBKEY) nak = true;
            if (o.job == Dm::J_NODEINFO && o.to == STRANGER && o.wantResponse) ask = true;
        }
        ck("they are told we lack their key (PKI_UNKNOWN_PUBKEY)", nak);
        ck("and get our NodeInfo, asking for theirs", ask);
        Dm::onFrame(d, now + 2000, 3);
        bool askAgain = false;
        while (Dm::next(now + 2000, 3, o)) if (o.job == Dm::J_NODEINFO) askAgain = true;
        ck("their retry does not send our NodeInfo again so soon", !askAgain);
    }

    suite("Their NAK: they never heard our key");
    {
        now = 500000;
        Dm::send(THEM, "try again", 0x5001, now);
        ck("out it goes", Dm::next(now, 3, o) && o.job == Dm::J_DM && o.id == 0x5001);
        load(f, theirRouting(0x5001, Dm::ERR_PKI_UNKNOWN_PUBKEY, f));
        Decoded d; decode(pk, d);
        Dm::onFrame(d, now + 2000, 3);
        ck("our NodeInfo goes to them first", Dm::next(now + 2000, 3, o) && o.job == Dm::J_NODEINFO && o.to == THEM);
        ck("still SENDING", Dm::state(0x5001) == Dm::ST_SENDING);
        ck("then the DM again", Dm::next(now + 6000, 3, o) && o.job == Dm::J_DM && o.id == 0x5001);
        load(f, theirRouting(0x5001, Dm::ERR_PKI_UNKNOWN_PUBKEY, f));
        decode(pk, d);
        Dm::onFrame(d, now + 8000, 3);
        uint8_t err = 0;
        ck("a second time is a failure, with their reason", Dm::state(0x5001, &err) == Dm::ST_FAILED && err == Dm::ERR_PKI_UNKNOWN_PUBKEY);
    }

    suite("Their NodeInfo asking for ours");
    {
        now = 600000;
        load(f, theirNodeInfo(THEM, s_theirPub, ME, true, 0x6001, f));
        Decoded d; decode(pk, d);
        Dm::onFrame(d, now, 3);
        ck("ours goes back, not asking again", Dm::next(now, 3, o) && o.job == Dm::J_NODEINFO && o.to == THEM && !o.wantResponse);
        Dm::onFrame(d, now + 5000, 3);
        ck("a second request inside 30 s gets nothing", !Dm::next(now + 5000, 3, o));
        load(f, theirNodeInfo(THEM, s_theirPub, BROADCAST, true, 0x6002, f));
        decode(pk, d);
        Dm::onFrame(d, now + 60000, 3);
        ck("a broadcast NodeInfo is not answered", !Dm::next(now + 60000, 3, o));
    }

    suite("A second key for a node we know");
    {
        uint8_t other[32]; memset(other, 0x44, 32);
        uint8_t otherPub[32]; LoraCrypto::x25519Base(otherPub, other);
        ck("is refused: the first key heard wins", Dm::learnKey(THEM, otherPub) == Dm::L_MISMATCH);
        ck("the same key again is fine", Dm::learnKey(THEM, s_theirPub) == Dm::L_SAME);
        ck("our own key from someone else is refused", Dm::learnKey(STRANGER, Dm::publicKey()) == Dm::L_BAD);
    }

    suite("Keys across a restart");
    {
        uint8_t buf[2000];
        const size_t n = Dm::saveKeys(buf, sizeof buf);
        ck("saved: one key, 37 bytes", n == 37);
        Dm::clearKeys();
        ck("cleared", !Dm::hasKey(THEM));
        ck("loaded back", Dm::loadKeys(buf, n) && Dm::hasKey(THEM) && !Dm::keysDirty());
        ck("a damaged blob is refused", !Dm::loadKeys(buf, n - 1));
    }

    suite("What send() refuses");
    {
        ck("empty text", Dm::send(THEM, "", 0x7001, 1) == 0);
        ck("everyone", Dm::send(BROADCAST, "x", 0x7002, 1) == 0);
        ck("ourselves", Dm::send(ME, "x", 0x7003, 1) == 0);
        int ok = 0;
        for (uint32_t i = 0; i < 10; i++) if (Dm::send(THEM, "x", 0x7100 + i, 1)) ok++;
        ck("more than the outbox holds", ok < 10 && ok >= 1);
    }

    suite("Hops for an answer (RoutingModule::getHopLimitForResponse)");
    {
        Header h; memset(&h, 0, sizeof h);
        h.hopStart = 3; h.hopLimit = 3;
        ck("heard directly: 2", Dm::responseHops(h, 3) == 3 || Dm::responseHops(h, 7) == 2);
        h.hopStart = 7; h.hopLimit = 1;
        ck("came 6 hops with a limit of 3: answer with 6", Dm::responseHops(h, 3) == 6);
        h.hopStart = 0; h.hopLimit = 0;
        ck("an old firmware's frame: our own setting", Dm::responseHops(h, 3) == 3);
    }

    Meshtastic::setPkiOpener(nullptr);
    return report();
}
