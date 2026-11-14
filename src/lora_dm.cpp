// SquachWatch-CYD — Meshtastic direct messages. See include/lora_dm.h.
#include "lora_dm.h"
#include "lora_crypto.h"
#include <string.h>

namespace Lora {
namespace Dm {

using namespace Meshtastic;

static bool isZero(const uint8_t* k) { uint8_t a = 0; for (int i = 0; i < 32; i++) a |= k[i]; return a == 0; }

namespace {

struct Peer {
    uint32_t num;          // 0 = free
    uint8_t  pub[32];
    uint8_t  aes[32];      // SHA-256 of the shared secret, once worked out
    bool     aesReady;
    bool     bad;          // their key is a point no secret comes out of
    uint32_t askedMs;      // our NodeInfo last went to them (asking or answering)
    bool     asked;
};

struct Pending {
    uint32_t id;           // 0 = free
    uint32_t to;
    State    st;
    uint8_t  err;
    uint8_t  tries;        // sends so far
    uint8_t  pkiRetry;     // times their PKI_UNKNOWN_PUBKEY sent our key and started over
    uint32_t firstMs, nextMs;
    uint16_t len;
    char     text[MAX_TEXT + 1];
};

struct Owed {
    Job      job;          // J_NONE = free
    uint32_t to, id;
    uint8_t  hops, err;
    bool     wantResponse;
};

bool     s_ready = false;
uint32_t s_me = 0;
uint8_t  s_priv[32], s_pub[32];
RandomFn s_rnd = nullptr;
Peer     s_peer[PEERS];
bool     s_keysDirty = false;
Pending  s_out[OUTBOX];
static const uint8_t OWED = 8;
Owed     s_owed[OWED];
StateFn  s_onState = nullptr;

// Answers to one node are spaced at least this far apart, so a flood of
// requests (or a node stuck retrying) costs us one reply, not dozens.
static const uint32_t NODEINFO_GAP_MS = 30000;

Peer* findPeer(uint32_t num) {
    if (!num) return nullptr;
    for (auto& p : s_peer) if (p.num == num) return &p;
    return nullptr;
}

// The key, worked out the first time it is wanted -- tens of milliseconds
// of X25519 on the S3, once per contact.
const uint8_t* aesFor(uint32_t num) {
    Peer* p = findPeer(num);
    if (!p || p->bad || !s_ready) return nullptr;
    if (!p->aesReady) {
        if (!pkiKey(s_priv, p->pub, p->aes)) { p->bad = true; return nullptr; }
        p->aesReady = true;
    }
    return p->aes;
}

void setState(Pending& d, State s, uint8_t err) {
    if (d.st == s && d.err == err) return;
    d.st = s; d.err = err;
    if (s_onState) s_onState(d.id, d.to, s, err);
}

Pending* findOut(uint32_t id) {
    if (!id) return nullptr;
    for (auto& d : s_out) if (d.id == id) return &d;
    return nullptr;
}

// One owed answer, unless the same one is already waiting.
void owe(Job job, uint32_t to, uint32_t id, uint8_t hops, uint8_t err, bool wantResponse) {
    for (auto& o : s_owed)
        if (o.job == job && o.to == to && o.id == id) { o.wantResponse = o.wantResponse || wantResponse; return; }
    for (auto& o : s_owed) {
        if (o.job != J_NONE) continue;
        o.job = job; o.to = to; o.id = id; o.hops = hops; o.err = err; o.wantResponse = wantResponse;
        return;
    }
    // Full: an answer dropped here is a retry the other side makes anyway.
}

// Our NodeInfo to `num`, at most once per NODEINFO_GAP_MS.
void oweNodeInfo(uint32_t num, uint8_t hops, bool wantResponse, uint32_t now) {
    Peer* p = findPeer(num);
    if (p && p->asked && now - p->askedMs < NODEINFO_GAP_MS) return;
    if (p) { p->asked = true; p->askedMs = now; }
    else {
        // No key yet means no row yet: a placeholder keeps the spacing.
        for (auto& q : s_peer) if (!q.num) { memset(&q, 0, sizeof q); q.num = num; q.bad = true;
                                             q.asked = true; q.askedMs = now; break; }
    }
    owe(J_NODEINFO, num, 0, hops, 0, wantResponse);
}

}  // namespace

void reset() {
    s_ready = false; s_me = 0; s_rnd = nullptr; s_onState = nullptr;
    memset(s_priv, 0, sizeof s_priv); memset(s_pub, 0, sizeof s_pub);
    memset(s_peer, 0, sizeof s_peer); memset(s_out, 0, sizeof s_out); memset(s_owed, 0, sizeof s_owed);
    s_keysDirty = false;
}

bool begin(uint32_t myNum, const uint8_t* savedPriv, RandomFn rnd) {
    s_me = myNum;
    s_rnd = rnd;
    bool fresh = false;
    if (savedPriv && !isZero(savedPriv)) memcpy(s_priv, savedPriv, 32);
    else if (rnd) { rnd(s_priv, 32); fresh = true; }
    else return false;
    LoraCrypto::x25519Base(s_pub, s_priv);
    s_ready = true;
    return fresh;
}

bool           ready()      { return s_ready; }
uint32_t       myNum()      { return s_me; }
const uint8_t* publicKey()  { return s_pub; }
const uint8_t* privateKey() { return s_priv; }

Learn learnKey(uint32_t num, const uint8_t pub[32]) {
    if (!num || !pub || num == s_me) return L_BAD;
    if (isZero(pub)) return L_BAD;
    // Somebody else showing our own key: theirs is a copy, or ours leaked.
    if (s_ready && !memcmp(pub, s_pub, 32)) return L_BAD;
    Peer* p = findPeer(num);
    if (p && !isZero(p->pub)) {
        if (!memcmp(p->pub, pub, 32)) return L_SAME;
        return L_MISMATCH;                   // first key heard wins
    }
    if (!p) {
        for (auto& q : s_peer) if (!q.num) { p = &q; break; }
        if (!p) {
            // Full: the oldest-asked row that has never been used to seal goes.
            for (auto& q : s_peer) if (!q.aesReady) { p = &q; break; }
            if (!p) return L_FULL;
        }
        memset(p, 0, sizeof *p);
    }
    const uint32_t askedMs = p->askedMs; const bool asked = p->asked;
    memset(p, 0, sizeof *p);
    p->num = num;
    memcpy(p->pub, pub, 32);
    p->asked = asked; p->askedMs = askedMs;
    s_keysDirty = true;
    return L_NEW;
}

bool hasKey(uint32_t num) { Peer* p = findPeer(num); return p && !isZero(p->pub) && !p->bad; }

bool forgetKey(uint32_t num) {
    Peer* p = findPeer(num);
    if (!p) return false;
    memset(p, 0, sizeof *p);
    s_keysDirty = true;
    return true;
}

uint8_t keyCount() { uint8_t n = 0; for (auto& p : s_peer) if (p.num && !isZero(p.pub)) n++; return n; }
bool    keysDirty() { return s_keysDirty; }
void    keysSaved() { s_keysDirty = false; }
void    clearKeys() { memset(s_peer, 0, sizeof s_peer); s_keysDirty = true; }

// Saved as a version byte, then 36 bytes a key: node number LE, public key.
size_t keysBytesNeeded() { return 1 + (size_t)PEERS * 36; }
size_t saveKeys(uint8_t* out, size_t cap) {
    if (!out || cap < keysBytesNeeded()) return 0;
    size_t o = 0;
    out[o++] = 1;
    for (auto& p : s_peer) {
        if (!p.num || isZero(p.pub)) continue;
        LoraCrypto::wr32le(out + o, p.num); o += 4;
        memcpy(out + o, p.pub, 32); o += 32;
    }
    return o;
}
bool loadKeys(const uint8_t* in, size_t len) {
    if (!in || len < 1 || in[0] != 1 || (len - 1) % 36) return false;
    for (size_t o = 1; o + 36 <= len; o += 36) learnKey(LoraCrypto::rd32le(in + o), in + o + 4);
    s_keysDirty = false;
    return true;
}

bool open(const Header& h, const uint8_t* in, uint8_t len, uint8_t* out, uint8_t& outLen) {
    if (!s_ready || h.to != s_me || h.from == s_me) return false;
    const uint8_t* k = aesFor(h.from);
    if (!k) return false;
    const int n = pkiOpen(k, h.from, h.id, in, len, out);
    if (n < 0) return false;
    outLen = (uint8_t)n;
    return true;
}

uint8_t responseHops(const Header& h, uint8_t hops) {
    if (hops < 1 || hops > 7) hops = 3;
    const uint8_t used = hopsAway(h);
    if (used == 0xFF) return hops;           // a firmware before 2.3: no way to tell
    if (used > hops) return used;
    if (h.hopStart == 0) return 1;           // they asked for no relays; 1 is the least we send
    if ((uint8_t)(used + 2) < hops) return (uint8_t)(used + 2);
    return hops;
}

uint32_t send(uint32_t to, const char* text, uint32_t id, uint32_t now) {
    if (!s_ready || !to || to == BROADCAST || to == s_me || !text || !text[0] || !id) return 0;
    if (findOut(id)) return 0;
    Pending* d = nullptr;
    for (auto& q : s_out) if (!q.id || q.st == ST_DELIVERED || q.st == ST_FAILED) { d = &q; break; }
    if (!d) return 0;
    memset(d, 0, sizeof *d);
    d->id = id; d->to = to;
    size_t n = strlen(text); if (n > MAX_TEXT) n = MAX_TEXT;
    memcpy(d->text, text, n); d->text[n] = '\0'; d->len = (uint16_t)n;
    d->firstMs = now; d->nextMs = now;
    setState(*d, hasKey(to) ? ST_SENDING : ST_WAIT_KEY, ERR_NONE);
    return id;
}

State state(uint32_t id, uint8_t* err) {
    Pending* d = findOut(id);
    if (err) *err = d ? d->err : 0;
    return d ? d->st : ST_NONE;
}

void    onState(StateFn fn) { s_onState = fn; }
uint8_t pending() { uint8_t n = 0; for (auto& d : s_out) if (d.id && (d.st == ST_SENDING || d.st == ST_WAIT_KEY)) n++; return n; }

static bool takeOwed(uint8_t hops, Out& o) {
    for (auto& w : s_owed) {
        if (w.job == J_NONE) continue;
        o.job = w.job; o.to = w.to; o.id = w.id; o.hops = w.hops ? w.hops : hops;
        o.err = w.err; o.wantResponse = w.wantResponse;
        w.job = J_NONE;
        return true;
    }
    return false;
}

bool next(uint32_t now, uint8_t hops, Out& o) {
    memset(&o, 0, sizeof o);
    if (!s_ready) return false;
    // Owed answers first: somebody is waiting on each.
    if (takeOwed(hops, o)) return true;
    for (auto& d : s_out) {
        if (!d.id) continue;
        if (d.st == ST_WAIT_KEY) {
            if (hasKey(d.to)) { setState(d, ST_SENDING, ERR_NONE); d.nextMs = now; }
            else if (now - d.firstMs >= KEY_WAIT_MS) { setState(d, ST_FAILED, ERR_NO_KEY); continue; }
            else {
                // Our NodeInfo to them, asking for theirs: their reply carries
                // the key. Spaced by oweNodeInfo; taken on the next call.
                oweNodeInfo(d.to, hops, true, now);
                continue;
            }
        }
        if (d.st != ST_SENDING || (int32_t)(now - d.nextMs) < 0) continue;
        if (d.tries >= TRIES) { setState(d, ST_FAILED, ERR_MAX_RETRANSMIT); continue; }
        const uint8_t* k = aesFor(d.to);
        if (!k) { setState(d, ST_FAILED, ERR_PKI_FAILED); continue; }
        uint8_t data[220];
        const size_t dn = encodeData(PORT_TEXT, (const uint8_t*)d.text, d.len, false, 0, data);
        uint32_t extra = 0;
        if (s_rnd) s_rnd((uint8_t*)&extra, 4);
        if (!extra) extra = d.id ^ 0x5A5A5A5Au;
        const uint16_t n = buildPkiFrame(k, s_me, d.to, d.id, hops, true, extra, data, dn, o.frame);
        if (!n) { setState(d, ST_FAILED, ERR_PKI_FAILED); continue; }
        d.tries++;
        d.nextMs = now + RETRY_MS;
        o.job = J_DM; o.to = d.to; o.id = d.id; o.hops = hops; o.len = n;
        return true;
    }
    // A DM still waiting for a key may just have asked for one.
    return takeOwed(hops, o);
}

void sent(const Out& o, bool ok, uint32_t now) {
    if (o.job != J_DM || ok) return;
    Pending* d = findOut(o.id);
    if (!d || d->st != ST_SENDING) return;
    if (d->tries) d->tries--;                // the radio never sent it: not a try
    d->nextMs = now + 3000;
}

void onFrame(const Decoded& d, uint32_t now, uint8_t hops) {
    if (!s_ready) return;
    const Header& h = d.hdr;
    if (h.from == s_me) return;              // our own, heard back through a relay
    const bool toMe = h.to == s_me;

    if (d.haveData && d.data.portnum == PORT_NODEINFO) {
        if (d.user.hasPubkey) learnKey(h.from, d.user.pubkey);
        // They asked for ours: the reply carries our key.
        if (toMe && d.data.wantResponse) oweNodeInfo(h.from, responseHops(h, hops), false, now);
        return;
    }

    if (d.haveData && d.data.portnum == PORT_ROUTING && toMe && d.data.requestId) {
        Pending* p = findOut(d.data.requestId);
        if (!p || p->to != h.from || p->st != ST_SENDING) return;
        const uint8_t e = d.routing.errorReason;
        if (d.routing.kind == 3 && e == ERR_NONE) { setState(*p, ST_DELIVERED, ERR_NONE); return; }
        if (e == ERR_PKI_UNKNOWN_PUBKEY && p->pkiRetry < 1) {
            // They have not heard our key: send it, then the DM again.
            p->pkiRetry++;
            p->tries = 0;
            p->nextMs = now + 4000;
            Peer* q = findPeer(h.from);
            if (q) q->asked = false;
            oweNodeInfo(h.from, responseHops(h, hops), true, now);
            return;
        }
        if (d.routing.kind == 3) setState(*p, ST_FAILED, e);
        return;
    }

    if (!toMe || h.channelHash != 0 || h.to == BROADCAST) return;

    if (d.pki) {
        // A DM to us that opened. ACK it the way ReliableRouter does: a
        // plain message (not itself a reply) that asked for one. A retry of
        // one already answered is answered again -- the first ACK was lost.
        if (h.wantAck && !d.data.requestId && !d.data.replyId)
            owe(J_ACK, h.from, h.id, responseHops(h, hops), ERR_NONE, false);
        return;
    }

    // Sealed to us and we could not open it: we have no key for them (or a
    // wrong one). Tell them, as the firmware does, and send ours asking for
    // theirs, so their retry opens.
    if (!hasKey(h.from)) {
        if (h.wantAck) owe(J_NAK, h.from, h.id, responseHops(h, hops), ERR_PKI_UNKNOWN_PUBKEY, false);
        oweNodeInfo(h.from, responseHops(h, hops), true, now);
    }
}

}
}
