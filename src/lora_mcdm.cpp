// SquachWatch-CYD — MeshCore direct messages. See include/lora_mcdm.h.
#include "lora_mcdm.h"
#include "lora_crypto.h"
#include <string.h>
#include <stdio.h>

namespace Lora {
namespace McDm {

using namespace MeshCore;

namespace {

struct Contact {
    uint8_t pub[32];      // all zero = free
    char    name[32];
    uint8_t secret[32];
    bool    secretReady, bad;
};

struct Pending {
    uint32_t id;          // 0 = free
    uint32_t to;
    State    st;
    uint8_t  tries;
    uint32_t nextMs;
    uint32_t stamp;       // the DM's own timestamp, the same every attempt
    uint8_t  ack[TRIES][4];
    char     text[161];
};

struct Owed { Job job; uint8_t contact; uint8_t ack[6]; uint8_t pathLenByte; uint8_t path[64]; };

bool     s_ready = false;
uint8_t  s_seed[32], s_pub[32], s_prv[64];
RandomFn s_rnd = nullptr;
Contact  s_c[CONTACTS];
bool     s_dirty = false;
Pending  s_out[OUTBOX];
static const uint8_t OWED = 6;
Owed     s_owed[OWED];
bool     s_advertDue = false;
bool     s_advertSent = false;
uint32_t s_advertMs = 0;
uint32_t s_lastStamp = 0;
TextFn   s_onText = nullptr;
StateFn  s_onState = nullptr;
// The last few DMs to us that we answered, so a flood's second copy (a relay
// repeating it) is not answered twice in a row.
struct Seen { uint32_t peer, stamp, ms; };
Seen     s_seen[8];
uint8_t  s_seenNext = 0;

bool isZero(const uint8_t* k, size_t n) { uint8_t a = 0; for (size_t i = 0; i < n; i++) a |= k[i]; return a == 0; }

int findContact(uint32_t peer) {
    for (int i = 0; i < CONTACTS; i++) if (!isZero(s_c[i].pub, 32) && peerId(s_c[i].pub) == peer) return i;
    return -1;
}
int findContactByKey(const uint8_t pub[32]) {
    for (int i = 0; i < CONTACTS; i++) if (!memcmp(s_c[i].pub, pub, 32)) return i;
    return -1;
}
const uint8_t* secretFor(int i) {
    if (i < 0 || !s_ready) return nullptr;
    Contact& c = s_c[i];
    if (c.bad) return nullptr;
    if (!c.secretReady) {
        if (!LoraCrypto::ed25519KeyExchange(c.secret, c.pub, s_prv)) { c.bad = true; return nullptr; }
        c.secretReady = true;
    }
    return c.secret;
}
Pending* findOut(uint32_t id) {
    if (!id) return nullptr;
    for (auto& d : s_out) if (d.id == id) return &d;
    return nullptr;
}
void setState(Pending& d, State s) {
    if (d.st == s) return;
    d.st = s;
    if (s_onState) s_onState(d.id, d.to, s);
}
void owe(const Owed& w) {
    for (auto& o : s_owed) if (o.job == J_NONE) { o = w; return; }
}
// A DM of ours, settled by an ACK: any attempt's ACK will do.
void settle(const uint8_t* ack4) {
    for (auto& d : s_out) {
        if (!d.id || d.st != ST_SENDING) continue;
        for (uint8_t a = 0; a < d.tries && a < TRIES; a++)
            if (!memcmp(d.ack[a], ack4, 4)) { setState(d, ST_DELIVERED); return; }
    }
}

}  // namespace

uint32_t peerId(const uint8_t pub[32]) {
    return ((uint32_t)pub[0] << 24) | ((uint32_t)pub[1] << 16) | ((uint32_t)pub[2] << 8) | pub[3];
}

void reset() {
    s_ready = false; s_rnd = nullptr; s_onText = nullptr; s_onState = nullptr;
    memset(s_seed, 0, sizeof s_seed); memset(s_pub, 0, sizeof s_pub); memset(s_prv, 0, sizeof s_prv);
    memset(s_c, 0, sizeof s_c); memset(s_out, 0, sizeof s_out); memset(s_owed, 0, sizeof s_owed);
    memset(s_seen, 0, sizeof s_seen);
    s_dirty = false; s_advertDue = false; s_advertSent = false; s_advertMs = 0; s_lastStamp = 0;
}

bool begin(const uint8_t* savedSeed, RandomFn rnd) {
    s_rnd = rnd;
    bool fresh = false;
    if (savedSeed && !isZero(savedSeed, 32)) memcpy(s_seed, savedSeed, 32);
    else if (rnd) { rnd(s_seed, 32); fresh = true; }
    else return false;
    LoraCrypto::ed25519KeyPair(s_seed, s_pub, s_prv);
    s_ready = true;
    return fresh;
}

bool           ready()     { return s_ready; }
const uint8_t* publicKey() { return s_pub; }
const uint8_t* seed()      { return s_seed; }

bool learn(const uint8_t pub[32], const char* name) {
    if (!pub || isZero(pub, 32) || (s_ready && !memcmp(pub, s_pub, 32))) return false;
    int i = findContactByKey(pub);
    if (i < 0) {
        for (int k = 0; k < CONTACTS; k++) if (isZero(s_c[k].pub, 32)) { i = k; break; }
        if (i < 0) {
            // Full: a contact we never worked a secret out for goes first.
            for (int k = 0; k < CONTACTS; k++) if (!s_c[k].secretReady) { i = k; break; }
            if (i < 0) return false;
        }
        memset(&s_c[i], 0, sizeof s_c[i]);
        memcpy(s_c[i].pub, pub, 32);
        s_dirty = true;
    }
    if (name && name[0] && strncmp(s_c[i].name, name, sizeof s_c[i].name)) {
        snprintf(s_c[i].name, sizeof s_c[i].name, "%s", name);
        s_dirty = true;
    }
    return true;
}

bool hasContact(uint32_t peer) { return findContact(peer) >= 0; }
bool contactName(uint32_t peer, char* out, size_t cap) {
    const int i = findContact(peer);
    if (i < 0 || !out || !cap) return false;
    if (s_c[i].name[0]) snprintf(out, cap, "%s", s_c[i].name);
    else snprintf(out, cap, "%02x%02x%02x", s_c[i].pub[0], s_c[i].pub[1], s_c[i].pub[2]);
    return true;
}
uint8_t contactCount() { uint8_t n = 0; for (auto& c : s_c) if (!isZero(c.pub, 32)) n++; return n; }
bool    contactsDirty() { return s_dirty; }
void    contactsSaved() { s_dirty = false; }
void    clearContacts() { memset(s_c, 0, sizeof s_c); s_dirty = true; }

// Version byte, then 64 bytes a contact: the key and a 32-byte name.
size_t contactsBytesNeeded() { return 1 + (size_t)CONTACTS * 64; }
size_t saveContacts(uint8_t* out, size_t cap) {
    if (!out || cap < contactsBytesNeeded()) return 0;
    size_t o = 0;
    out[o++] = 1;
    for (auto& c : s_c) {
        if (isZero(c.pub, 32)) continue;
        memcpy(out + o, c.pub, 32); memcpy(out + o + 32, c.name, 32); o += 64;
    }
    return o;
}
bool loadContacts(const uint8_t* in, size_t len) {
    if (!in || len < 1 || in[0] != 1 || (len - 1) % 64) return false;
    for (size_t o = 1; o + 64 <= len; o += 64) {
        char name[33]; memcpy(name, in + o + 32, 32); name[32] = '\0';
        learn(in + o, name);
    }
    s_dirty = false;
    return true;
}

void advertNow() { s_advertDue = true; }
void onText(TextFn fn)   { s_onText = fn; }
void onState(StateFn fn) { s_onState = fn; }
uint8_t pending() { uint8_t n = 0; for (auto& d : s_out) if (d.id && d.st == ST_SENDING) n++; return n; }

uint32_t send(uint32_t to, const char* text, uint32_t id, uint32_t now) {
    if (!s_ready || !text || !text[0] || !id || findContact(to) < 0 || findOut(id)) return 0;
    Pending* d = nullptr;
    for (auto& q : s_out) if (!q.id || q.st == ST_DELIVERED || q.st == ST_FAILED) { d = &q; break; }
    if (!d) return 0;
    memset(d, 0, sizeof *d);
    d->id = id; d->to = to; d->nextMs = now;
    snprintf(d->text, sizeof d->text, "%s", text);
    setState(*d, ST_SENDING);
    // Our advert first, unless it went out lately: without it they cannot open this.
    if (!s_advertSent || now - s_advertMs >= ADVERT_EVERY_MS) s_advertDue = true;
    return id;
}

State state(uint32_t id) { Pending* d = findOut(id); return d ? d->st : ST_NONE; }

bool next(uint32_t now, uint32_t epoch, const char* name, Out& o) {
    memset(&o, 0, sizeof o);
    if (!s_ready) return false;
    // A strictly rising stamp: MeshCore ignores an advert no newer than the
    // last it kept from us, and a DM's stamp is half of its ACK.
    const uint32_t stamp = epoch > s_lastStamp ? epoch : s_lastStamp + 1;

    for (auto& w : s_owed) {
        if (w.job == J_NONE) continue;
        const uint8_t* sec = secretFor(w.contact);
        const Contact& c = s_c[w.contact];
        if (w.job == J_PATH && sec) o.len = buildPathReturn(sec, c.pub[0], s_pub[0], w.pathLenByte, w.path, w.ack, o.frame);
        else if (w.job == J_ACK) o.len = buildAck(w.ack, o.frame);
        o.job = o.len ? w.job : J_NONE;
        o.to = peerId(c.pub);
        w.job = J_NONE;
        if (o.len) return true;
    }
    if (s_advertDue) {
        s_advertDue = false;
        o.len = buildAdvert(s_pub, s_prv, stamp, name ? name : "SQUACHY", o.frame);
        if (o.len) {
            s_lastStamp = stamp;
            s_advertSent = true; s_advertMs = now;
            o.job = J_ADVERT;
            return true;
        }
    }
    for (auto& d : s_out) {
        if (!d.id || d.st != ST_SENDING || (int32_t)(now - d.nextMs) < 0) continue;
        if (d.tries >= TRIES) { setState(d, ST_FAILED); continue; }
        const int ci = findContact(d.to);
        const uint8_t* sec = secretFor(ci);
        if (!sec) { setState(d, ST_FAILED); continue; }
        if (!d.stamp) { d.stamp = stamp; s_lastStamp = stamp; }
        o.len = buildDirectText(sec, s_c[ci].pub, s_pub, d.stamp, d.tries, d.text, o.frame, d.ack[d.tries]);
        if (!o.len) { setState(d, ST_FAILED); continue; }
        d.tries++;
        d.nextMs = now + RETRY_MS;
        o.job = J_DM; o.to = d.to; o.id = d.id;
        return true;
    }
    return false;
}

void sent(const Out& o, bool ok, uint32_t now) {
    if (ok) return;
    if (o.job == J_ADVERT) { s_advertDue = true; s_advertSent = false; return; }
    if (o.job != J_DM) return;
    Pending* d = findOut(o.id);
    if (!d || d->st != ST_SENDING) return;
    if (d->tries) d->tries--;
    d->nextMs = now + 3000;
}

void onFrame(const Decoded& d, uint32_t now) {
    if (!s_ready) return;
    const Frame& f = d.f;
    if (d.haveAdvert) { learn(d.adv.pubkey, d.adv.hasName ? d.adv.name : nullptr); return; }
    if (f.type == TYPE_ACK && f.payloadLen >= 4) { settle(f.payload); return; }
    if ((f.type != TYPE_TXT_MSG && f.type != TYPE_PATH) || f.payloadLen < 2 + 2 + 16) return;
    if (f.payload[0] != s_pub[0]) return;                  // not to us
    const uint8_t src = f.payload[1];
    // Every contact whose key starts with that byte gets a try, as
    // Mesh::searchPeersByHash does; the one whose secret opens it is the sender.
    for (int i = 0; i < CONTACTS; i++) {
        if (isZero(s_c[i].pub, 32) || s_c[i].pub[0] != src) continue;
        const uint8_t* sec = secretFor(i);
        if (!sec) continue;
        uint8_t plain[192], pl = 0;
        if (!openSecret(sec, f.payload + 2, (uint8_t)(f.payloadLen - 2), plain, pl)) continue;
        if (f.type == TYPE_PATH) {
            PathReturn pr;
            if (parsePathReturn(plain, pl, pr) && pr.extraType == TYPE_ACK && pr.extraLen >= 4) settle(pr.extra);
            return;
        }
        DirectText t;
        if (!parseDirectText(plain, pl, t) || t.txtType != 0) return;   // plain text only; CLI and signed kinds are not ours
        const uint32_t peer = peerId(s_c[i].pub);
        bool again = false;
        for (auto& s : s_seen) if (s.peer == peer && s.stamp == t.timestamp && now - s.ms < 5000) again = true;
        if (again) return;
        s_seen[s_seenNext] = Seen{ peer, t.timestamp, now };
        s_seenNext = (uint8_t)((s_seenNext + 1) % 8);
        if (s_onText) s_onText(peer, s_c[i].name, t, now);
        // The receipt: sha256 of what they sent and THEIR key, an attempt
        // byte (0 unless the attempt rode past the text) and a random one.
        Owed w; memset(&w, 0, sizeof w);
        w.contact = (uint8_t)i;
        ackFor(plain, (size_t)(5 + t.textLen), s_c[i].pub, w.ack);
        w.ack[4] = (5 + t.textLen + 1 < pl) ? plain[5 + t.textLen + 1] : 0;
        if (s_rnd) s_rnd(&w.ack[5], 1);
        const bool flood = f.route == ROUTE_FLOOD || f.route == ROUTE_TRANSPORT_FLOOD;
        if (flood) {
            w.job = J_PATH;
            const uint16_t pb = (uint16_t)(f.hashSize * f.hops);
            if (pb <= sizeof w.path) {
                w.pathLenByte = (uint8_t)(((f.hashSize - 1) << 6) | (f.hops & 63));
                if (pb) memcpy(w.path, f.path, pb);
            }
        } else {
            w.job = J_ACK;
        }
        owe(w);
        return;
    }
}

}
}
