// MeshCore direct messages -- src/lora_mcdm.cpp -- end to end against a
// stand-in for a stock companion built from lora_meshcore.cpp's own pieces
// (whose frames are pinned to Python-built ones in lora_meshcore_test.cpp):
// their advert makes them a contact, our DM goes out after our advert, their
// PATH return's ACK settles it; retries with a fresh ACK each attempt; a DM
// to us is handed on and answered the way BaseChatMesh answers.
#include "lora_mcdm.h"
#include "lora_meshcore.h"
#include "lora_crypto.h"
#include "test_util.h"
#include <cstring>
#include <cstdio>

using namespace MeshCore;
namespace Mc = Lora::McDm;

static uint8_t s_ctr = 0;
static void rnd(uint8_t* out, size_t n) { for (size_t i = 0; i < n; i++) out[i] = (uint8_t)(0x30 + s_ctr++); }

static Lora::Packet pk;
static bool load(const uint8_t* f, uint16_t n, Decoded& d) {
    memset(&pk, 0, sizeof pk); memcpy(pk.data, f, n); pk.len = (uint8_t)n;
    return decode(pk, d);
}

static char     s_gotText[200];
static uint32_t s_gotPeer = 0;
static int      s_texts = 0;
static void onText(uint32_t peer, const char*, const DirectText& t, uint32_t) {
    s_gotPeer = peer; snprintf(s_gotText, sizeof s_gotText, "%s", t.text); s_texts++;
}
static Mc::State s_lastState = Mc::ST_NONE;
static void onState(uint32_t, uint32_t, Mc::State s) { s_lastState = s; }

int main() {
    // "Them": a companion with its own identity.
    uint8_t bSeed[32], bPub[32], bPrv[64];
    LoraCrypto::fromHex("808182838485868788898a8b8c8d8e8f909192939495969798999a9b9c9d9e9f", bSeed, 32);
    LoraCrypto::ed25519KeyPair(bSeed, bPub, bPrv);
    const uint32_t B = Mc::peerId(bPub);

    suite("Our identity");
    uint8_t ourSeed[32];
    {
        Mc::reset();
        ck("a first boot makes one and says to keep it", Mc::begin(nullptr, rnd) && Mc::ready());
        memcpy(ourSeed, Mc::seed(), 32);
        uint8_t pub[32]; memcpy(pub, Mc::publicKey(), 32);
        Mc::reset();
        ck("the next boot loads the same one", !Mc::begin(ourSeed, rnd) && !memcmp(Mc::publicKey(), pub, 32));
        Mc::onText(onText);
        Mc::onState(onState);
    }
    const uint8_t* ourPub = Mc::publicKey();
    uint8_t bSecret[32];
    LoraCrypto::ed25519KeyExchange(bSecret, ourPub, bPrv);

    uint8_t f[256];
    Decoded d;
    Mc::Out o;

    suite("Their advert makes them a contact");
    {
        ck("not a contact yet: send() refuses", !Mc::send(B, "hi", 0x1001, 1000));
        ck("their advert decodes", load(f, buildAdvert(bPub, bPrv, 1792700000u, "SASQ PRO", f), d) && d.haveAdvert);
        Mc::onFrame(d, 1000);
        char nm[40];
        ck("now they are, by name", Mc::hasContact(B) && Mc::contactName(B, nm, sizeof nm) && !strcmp(nm, "SASQ PRO"));
        ck("and the list wants saving", Mc::contactsDirty() && Mc::contactCount() == 1);
        // Our own advert heard back through a repeater is not a contact.
        load(f, buildAdvert(ourPub, bPrv, 1, "ME", f), d);
        Mc::onFrame(d, 1000);
        ck("our own key is never a contact", Mc::contactCount() == 1);
    }

    suite("A DM out, and its ACK inside their PATH return");
    {
        const uint32_t now = 2000;
        ck("queued", Mc::send(B, "anyone at the fire tower?", 0x1001, now) == 0x1001 && s_lastState == Mc::ST_SENDING);
        ck("our advert goes first", Mc::next(now, 1792700100u, "SQUACHY", o) && o.job == Mc::J_ADVERT);
        load(o.frame, o.len, d);
        ck("signed, named, ours", d.haveAdvert && !memcmp(d.adv.pubkey, ourPub, 32) && !strcmp(d.adv.name, "SQUACHY"));
        ck("then the DM", Mc::next(now, 1792700100u, "SQUACHY", o) && o.job == Mc::J_DM && o.to == B && o.id == 0x1001);
        // They open it.
        ck("decodes as a TXT_MSG to them from us", load(o.frame, o.len, d) && d.f.type == TYPE_TXT_MSG &&
           d.destHash == bPub[0] && d.srcHash == ourPub[0] && d.f.route == ROUTE_FLOOD);
        uint8_t plain[192], pl = 0;
        DirectText t;
        ck("and opens with their secret", openSecret(bSecret, d.f.payload + 2, (uint8_t)(d.f.payloadLen - 2), plain, pl) &&
           parseDirectText(plain, pl, t) && !strcmp(t.text, "anyone at the fire tower?") && t.attempt == 0 &&
           t.timestamp > 1792700100u - 1);
        uint8_t ack[6] = {0};
        ackFor(plain, (size_t)(5 + t.textLen), ourPub, ack);
        ack[5] = 0x77;
        ck("still SENDING until they answer", Mc::state(0x1001) == Mc::ST_SENDING && !Mc::next(now + 100, 1792700101u, "SQUACHY", o));
        const uint16_t pn = buildPathReturn(bSecret, ourPub[0], bPub[0], 0x00, nullptr, ack, f);
        load(f, pn, d);
        Mc::onFrame(d, now + 3000);
        ck("their PATH return's ACK: DELIVERED", Mc::state(0x1001) == Mc::ST_DELIVERED && s_lastState == Mc::ST_DELIVERED);
        ck("and nothing is resent", !Mc::next(now + 30000, 1792700130u, "SQUACHY", o));
    }

    suite("A DM nobody answers, and one answered late");
    {
        uint32_t now = 100000;
        Mc::send(B, "hello?", 0x1002, now);
        int sends = 0; uint8_t acks[3][4]; uint8_t lastPlainAttempt = 9;
        for (uint32_t t = now; t < now + 60000; t += 1000)
            while (Mc::next(t, 1792800000u + t / 1000, "SQUACHY", o)) {
                if (o.job != Mc::J_DM || o.id != 0x1002) continue;
                load(o.frame, o.len, d);
                uint8_t plain[192], pl = 0; DirectText dt;
                openSecret(bSecret, d.f.payload + 2, (uint8_t)(d.f.payloadLen - 2), plain, pl);
                parseDirectText(plain, pl, dt);
                if (sends < 3) ackFor(plain, (size_t)(5 + dt.textLen), ourPub, acks[sends]);
                lastPlainAttempt = dt.attempt;
                sends++;
            }
        ck("three sends", sends == 3);
        ck("the attempt counted up in the flags", lastPlainAttempt == 2);
        ck("each attempt with its own ACK", memcmp(acks[0], acks[1], 4) && memcmp(acks[1], acks[2], 4));
        ck("then FAILED", Mc::state(0x1002) == Mc::ST_FAILED);

        now = 200000;
        Mc::send(B, "late one", 0x1003, now);
        uint8_t first[4] = {0};
        for (int k = 0; k < 2; k++) {
            while (Mc::next(now, 1792900000u + k, "SQUACHY", o)) {
                if (o.job != Mc::J_DM) continue;
                load(o.frame, o.len, d);
                uint8_t plain[192], pl = 0; DirectText dt;
                openSecret(bSecret, d.f.payload + 2, (uint8_t)(d.f.payloadLen - 2), plain, pl);
                parseDirectText(plain, pl, dt);
                if (k == 0) ackFor(plain, (size_t)(5 + dt.textLen), ourPub, first);
            }
            now += Mc::RETRY_MS;
        }
        // The first attempt's ACK arrives after the second went: still ours.
        uint8_t ack6[6] = { first[0], first[1], first[2], first[3], 0, 1 };
        load(f, buildAck(ack6, f), d);
        Mc::onFrame(d, now);
        ck("an earlier attempt's bare ACK still counts", Mc::state(0x1003) == Mc::ST_DELIVERED);
    }

    suite("A DM to us");
    {
        const uint32_t now = 300000;
        uint8_t ack[4];
        // By flood, through two repeaters (path ab, cd).
        uint16_t n = buildDirectText(bSecret, ourPub, bPub, 1792710000u, 0, "meet at the big rock", f, ack);
        uint8_t g[256];
        g[0] = f[0]; g[1] = 0x02; g[2] = 0xab; g[3] = 0xcd; memcpy(g + 4, f + 2, n - 2);
        load(g, (uint16_t)(n + 2), d);
        ck("decodes, two hops", d.f.type == TYPE_TXT_MSG && d.f.hops == 2);
        Mc::onFrame(d, now);
        ck("handed on: from them, the text", s_texts == 1 && s_gotPeer == B && !strcmp(s_gotText, "meet at the big rock"));
        ck("a PATH return is owed", Mc::next(now, 1792710001u, "SQUACHY", o) && o.job == Mc::J_PATH && o.to == B);
        load(o.frame, o.len, d);
        uint8_t plain[192], pl = 0; PathReturn pr;
        ck("they open it: the path it came by, and our ACK",
           d.f.type == TYPE_PATH && d.f.payload[0] == bPub[0] && d.f.payload[1] == ourPub[0] &&
           openSecret(bSecret, d.f.payload + 2, (uint8_t)(d.f.payloadLen - 2), plain, pl) && parsePathReturn(plain, pl, pr) &&
           pr.hops == 2 && pr.path[0] == 0xab && pr.path[1] == 0xcd && pr.extraType == TYPE_ACK && pr.extraLen >= 6 &&
           !memcmp(pr.extra, ack, 4));
        load(g, (uint16_t)(n + 2), d);
        Mc::onFrame(d, now + 1000);
        ck("a relay's second copy is not answered twice", s_texts == 1 && !Mc::next(now + 1000, 1792710002u, "SQUACHY", o));

        // Sent direct (they learned a path to us): a bare ACK.
        n = buildDirectText(bSecret, ourPub, bPub, 1792710100u, 0, "direct now", f, ack);
        f[0] = (uint8_t)((TYPE_TXT_MSG << 2) | ROUTE_DIRECT);
        load(f, n, d);
        Mc::onFrame(d, now + 10000);
        ck("a direct one gets a bare ACK with the same four bytes", s_texts == 2 &&
           Mc::next(now + 10000, 1792710101u, "SQUACHY", o) && o.job == Mc::J_ACK && load(o.frame, o.len, d) &&
           d.f.type == TYPE_ACK && !memcmp(d.ackCrc, ack, 4));
    }

    suite("What is not ours");
    {
        uint8_t xSeed[32], xPub[32], xPrv[64], xSecret[32], ack[4];
        memset(xSeed, 0x55, 32);
        LoraCrypto::ed25519KeyPair(xSeed, xPub, xPrv);
        LoraCrypto::ed25519KeyExchange(xSecret, ourPub, xPrv);
        load(f, buildDirectText(xSecret, ourPub, xPub, 1, 0, "who dis", f, ack), d);
        const int before = s_texts;
        Mc::onFrame(d, 400000);
        ck("a DM from somebody who is not a contact is not opened", s_texts == before && !Mc::next(400000, 2, "SQUACHY", o));
        load(f, buildDirectText(bSecret, xPub, bPub, 1, 0, "not for you", f, ack), d);
        Mc::onFrame(d, 400000);
        ck("nor one to somebody else", s_texts == before);
    }

    suite("Contacts across a restart");
    {
        uint8_t buf[4000];
        const size_t n = Mc::saveContacts(buf, sizeof buf);
        ck("one contact, 65 bytes", n == 65);
        Mc::clearContacts();
        ck("gone", !Mc::hasContact(B));
        char nm[40];
        ck("back, with the name", Mc::loadContacts(buf, n) && Mc::hasContact(B) && Mc::contactName(B, nm, sizeof nm) &&
           !strcmp(nm, "SASQ PRO") && !Mc::contactsDirty());
    }

    return report();
}
