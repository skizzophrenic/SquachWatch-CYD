// SquachWatch-CYD — MeshCore, off the air. See include/lora_meshcore.h.
// Layouts from docs/packet_format.md and docs/payloads.md in the MeshCore
// repository; the sealing from src/Utils.cpp; the advert from
// src/helpers/AdvertDataHelpers.h.
#include "lora_meshcore.h"
#include "lora_crypto.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>   // strcasecmp: a tag typed by a person

namespace MeshCore {

using namespace LoraCrypto;

const char* typeName(uint8_t t) {
    switch (t) {
        case TYPE_REQ: return "REQ";           case TYPE_RESPONSE: return "RESPONSE";
        case TYPE_TXT_MSG: return "TXT";       case TYPE_ACK: return "ACK";
        case TYPE_ADVERT: return "ADVERT";     case TYPE_GRP_TXT: return "GRP_TXT";
        case TYPE_GRP_DATA: return "GRP_DATA"; case TYPE_ANON_REQ: return "ANON_REQ";
        case TYPE_PATH: return "PATH";         case TYPE_TRACE: return "TRACE";
        case TYPE_MULTIPART: return "MULTIPART"; case TYPE_CONTROL: return "CONTROL";
        case TYPE_RAW_CUSTOM: return "RAW";    default: return "TYPE ?";
    }
}

const char* routeName(uint8_t r) {
    switch (r) {
        case ROUTE_TRANSPORT_FLOOD: return "tflood"; case ROUTE_FLOOD: return "flood";
        case ROUTE_DIRECT: return "direct";           case ROUTE_TRANSPORT_DIRECT: return "tdirect";
        default: return "?";
    }
}

const char* nodeTypeName(uint8_t t) {
    switch (t) {
        case NODE_CHAT: return "CHAT";  case NODE_REPEATER: return "REPEATER";
        case NODE_ROOM: return "ROOM";  case NODE_SENSOR: return "SENSOR";
        default: return "NODE ?";
    }
}

bool parse(const uint8_t* d, uint8_t len, Frame& f) {
    memset(&f, 0, sizeof f);
    if (len < 2) return false;
    const uint8_t h = d[0];
    f.route   = h & 0x03;
    f.type    = (h >> 2) & 0x0F;
    f.version = h >> 6;
    if (f.version != 0) return false;
    size_t off = 1;
    if (f.route == ROUTE_TRANSPORT_FLOOD || f.route == ROUTE_TRANSPORT_DIRECT) {
        if (len < 6) return false;
        f.hasTransport = true;
        f.transport1 = rd16le(d + 1);
        f.transport2 = rd16le(d + 3);
        off = 5;
    }
    if (off >= len) return false;
    const uint8_t pl = d[off++];
    f.hashSize = (uint8_t)((pl >> 6) + 1);
    f.hops     = pl & 0x3F;
    if (f.hashSize > 2) return false;                 // three is reserved
    const size_t pathBytes = (size_t)f.hops * f.hashSize;
    if (pathBytes > 64 || off + pathBytes > len) return false;
    f.path = d + off;
    off += pathBytes;
    f.payload = d + off;
    f.payloadLen = (uint8_t)(len - off);
    return true;
}

// ---- adverts -------------------------------------------------------------
bool parseAdvert(const Frame& f, Advert& a) {
    memset(&a, 0, sizeof a);
    if (f.type != TYPE_ADVERT || f.payloadLen < 32 + 4 + 64 + 1) return false;
    const uint8_t* p = f.payload;
    memcpy(a.pubkey, p, 32);
    a.timestamp = rd32le(p + 32);
    memcpy(a.signature, p + 36, 64);
    const uint8_t flags = p[100];
    a.nodeType = flags & 0x0F;
    size_t off = 101;
    const size_t end = f.payloadLen;
    if (flags & 0x10) {
        if (off + 8 > end) return false;
        a.hasLatLon = true;
        a.latE6 = (int32_t)rd32le(p + off); a.lonE6 = (int32_t)rd32le(p + off + 4);
        off += 8;
    }
    if (flags & 0x20) { if (off + 2 > end) return false; off += 2; }   // feat1, reserved
    if (flags & 0x40) { if (off + 2 > end) return false; off += 2; }   // feat2, reserved
    if (flags & 0x80) {
        a.hasName = true;
        size_t n = end - off;
        if (n > sizeof a.name - 1) n = sizeof a.name - 1;
        memcpy(a.name, p + off, n);
        a.name[n] = '\0';
    }
    return true;
}

// ---- channels --------------------------------------------------------------
void hashtagKey(const char* name, uint8_t key[16]) {
    uint8_t h[32];
    sha256((const uint8_t*)name, strlen(name), h);
    memcpy(key, h, 16);
}

uint8_t channelHash(const uint8_t key[16]) {
    uint8_t h[32];
    sha256(key, 16, h);
    return h[0];
}

static const uint8_t PUBLIC_KEY[16] = {
    0x8b, 0x33, 0x87, 0xe9, 0xc5, 0xcd, 0xea, 0x6a, 0xc9, 0xe5, 0xed, 0xba, 0xa1, 0x15, 0xcd, 0x72 };
// Room for a region's worth of open hashtag channels. A MeshCore Channel is
// 24 bytes of name, 16 of key and one of hash, so 24 of them cost about a
// kilobyte of the 7.6 MB free - the old 8 was not a memory budget, it was a
// guess, and one Ruhr-area operator's own list of open channels already needed
// eleven.
static const uint8_t MAX_USER = 24;
static Channel s_user[MAX_USER];
static uint8_t s_nUser = 0;
// Index-for-index with channel(): slot 0 is Public, the rest the user's. A
// parallel array rather than two fields in Channel, because channel(0) builds
// Public from the constant above on every call and a counter living in that
// struct would be reset by its own getter.
static uint32_t s_frames[1 + MAX_USER];
static uint32_t s_lastMs[1 + MAX_USER];

uint8_t channelCount() { return (uint8_t)(1 + s_nUser); }
uint8_t userChannelCount() { return s_nUser; }
uint8_t maxUserChannels() { return MAX_USER; }

// By value, and it matters. Public used to be built into one file-static and
// handed back by reference, so every caller shared one struct: the CHANS view
// asks for a row about thirty times a second from loop() while the radio task
// may be inside decode() holding what it thinks is its own copy. Nothing there
// could run off the end -- both decode sites copy immediately, and the cost of
// losing the race was a torn Channel and one frame that failed to decrypt --
// but a shared mutable buffer across two cores is not something to leave
// standing on the strength of its worst case. A Channel is 44 bytes; a copy is
// cheaper than the SHA-256 the caller is about to do with it.
Channel channel(uint8_t i) {
    Channel c;
    memset(&c, 0, sizeof c);
    if (i == 0) {
        strncpy(c.name, "Public", sizeof c.name - 1);
        memcpy(c.key, PUBLIC_KEY, 16);
        c.hash = 0x11;
        c.derived = false;      // a published constant, not a derivation
        c.enabled = true;       // built in, and never switched off
        return c;
    }
    i--;
    // Past the end -- a list that shrank under a caller holding an old index --
    // gives a zeroed channel, which is disabled and can open nothing.
    if (i < s_nUser) return s_user[i];
    return c;
}

bool addChannel(const char* name, const char* keyText) {
    if (s_nUser >= MAX_USER || !name || !*name) return false;
    if (findChannel(name) >= 0) return false;     // already held; a second slot would be waste
    Channel& c = s_user[s_nUser];
    memset(&c, 0, sizeof c);
    strncpy(c.name, name, sizeof c.name - 1);
    if (keyText && *keyText) {
        uint8_t k[32];
        size_t n = fromBase64(keyText, k, sizeof k);
        if (n != 16) n = fromHex(keyText, k, sizeof k);
        if (n != 16) return false;
        memcpy(c.key, k, 16);
        c.derived = false;
    } else {
        // A hashtag channel: "#name" is the key. Add the hash if it was left off.
        char tag[26];
        if (name[0] == '#') strncpy(tag, name, sizeof tag - 1);
        else snprintf(tag, sizeof tag, "#%s", name);
        tag[sizeof tag - 1] = '\0';
        hashtagKey(tag, c.key);
        c.derived = true;
    }
    c.hash = channelHash(c.key);
    c.enabled = true;
    s_frames[1 + s_nUser] = 0;
    s_lastMs[1 + s_nUser] = 0;
    s_nUser++;
    return true;
}

void clearUserChannels() {
    s_nUser = 0;
    memset(s_frames + 1, 0, sizeof s_frames - sizeof s_frames[0]);
    memset(s_lastMs + 1, 0, sizeof s_lastMs - sizeof s_lastMs[0]);
}

int findChannel(const char* name) {
    if (!name || !*name) return -1;
    for (uint8_t i = 0; i < channelCount(); i++)
        if (strcasecmp(channel(i).name, name) == 0) return (int)i;
    return -1;
}

bool removeChannel(uint8_t i) {
    if (i == 0 || i >= channelCount()) return false;
    const uint8_t u = (uint8_t)(i - 1);
    // Order is what the screen and the console print, so the tail shifts down
    // rather than the last entry backfilling the hole.
    for (uint8_t k = u; k + 1 < s_nUser; k++) s_user[k] = s_user[k + 1];
    for (uint8_t k = i; k + 1 < channelCount(); k++) { s_frames[k] = s_frames[k + 1]; s_lastMs[k] = s_lastMs[k + 1]; }
    s_nUser--;
    s_frames[1 + s_nUser] = 0;
    s_lastMs[1 + s_nUser] = 0;
    return true;
}

void setChannelEnabled(uint8_t i, bool on) {
    if (i == 0 || i >= channelCount()) return;
    s_user[i - 1].enabled = on;
}

void noteChannelHeard(uint8_t i, uint32_t nowMs) {
    if (i >= channelCount()) return;
    s_frames[i]++;
    s_lastMs[i] = nowMs;
}
uint32_t channelFrames(uint8_t i) { return i < channelCount() ? s_frames[i] : 0; }
uint32_t channelLastMs(uint8_t i) { return i < channelCount() ? s_lastMs[i] : 0; }

bool openSealed(const uint8_t key[16], const uint8_t* mac2, const uint8_t* cipher, uint8_t cipherLen,
                uint8_t* plain, uint8_t& plainLen) {
    plainLen = 0;
    if (!cipherLen || (cipherLen & 15)) return false;
    // The HMAC's key is the whole 32-byte secret slot, which for a 128-bit
    // channel key is the key and sixteen zero bytes after it.
    uint8_t secret[32] = {0};
    memcpy(secret, key, 16);
    uint8_t mac[32];
    hmacSha256(secret, 32, cipher, cipherLen, mac);
    if (mac[0] != mac2[0] || mac[1] != mac2[1]) return false;
    Aes128 a;
    a.setKey(key);
    for (uint8_t off = 0; off < cipherLen; off = (uint8_t)(off + 16)) a.decryptBlock(cipher + off, plain + off);
    plainLen = cipherLen;
    // The last block was zero-padded; trailing zeros are not content.
    while (plainLen && plain[plainLen - 1] == 0) plainLen--;
    return true;
}

bool parseGroupText(const uint8_t* plain, uint8_t len, GroupText& g) {
    memset(&g, 0, sizeof g);
    if (len < 5) return false;
    g.timestamp = rd32le(plain);
    g.flags = plain[4];
    const char* s = (const char*)plain + 5;
    const size_t n = len - 5;
    // "Sender: text"; the name is what the sender claims.
    const char* colon = (const char*)memchr(s, ':', n);
    if (colon && colon + 1 < s + n && colon[1] == ' ' && (size_t)(colon - s) < sizeof g.sender - 1) {
        memcpy(g.sender, s, (size_t)(colon - s));
        g.sender[colon - s] = '\0';
        const size_t tl = (size_t)(s + n - (colon + 2));
        const size_t m = tl < sizeof g.text - 1 ? tl : sizeof g.text - 1;
        memcpy(g.text, colon + 2, m); g.text[m] = '\0';
    } else {
        const size_t m = n < sizeof g.text - 1 ? n : sizeof g.text - 1;
        memcpy(g.text, s, m); g.text[m] = '\0';
    }
    return true;
}

bool parseTrace(const Frame& f, Trace& t) {
    memset(&t, 0, sizeof t);
    if (f.type != TYPE_TRACE || f.payloadLen < 9) return false;
    t.tag  = rd32le(f.payload);
    t.auth = rd32le(f.payload + 4);
    t.flags = f.payload[8];
    const uint8_t hs = (uint8_t)(1 << (t.flags & 0x03));
    const uint8_t n = (uint8_t)((f.payloadLen - 9) / (hs ? hs : 1));
    t.nHashes = n < 32 ? n : 32;
    memcpy(t.hashes, f.payload + 9, (size_t)t.nHashes * hs > 32 ? 32 : (size_t)t.nHashes * hs);
    // On a TRACE the path carries one signed SNR x 4 per hop instead of hashes.
    t.nSnr = f.hops < 32 ? f.hops : 32;
    for (uint8_t i = 0; i < t.nSnr; i++) t.snr4[i] = (int8_t)f.path[i * f.hashSize];
    return true;
}

// ---- all of it --------------------------------------------------------------
bool decode(const Lora::Packet& pk, Decoded& out) {
    memset(&out, 0, sizeof out);
    if (!parse(pk.data, pk.len, out.f)) return false;
    const Frame& f = out.f;
    switch (f.type) {
        case TYPE_ADVERT:
            out.haveAdvert = parseAdvert(f, out.adv);
            break;
        case TYPE_GRP_TXT: case TYPE_GRP_DATA: {
            if (f.payloadLen < 1 + 2 + 16) break;
            const uint8_t hash = f.payload[0];
            for (uint8_t i = 0; i < channelCount(); i++) {
                const Channel c = channel(i);
                if (!c.enabled || c.hash != hash) continue;
                uint8_t plen = 0;
                if (!openSealed(c.key, f.payload + 1, f.payload + 3, (uint8_t)(f.payloadLen - 3), out.plain, plen)) continue;
                // The seal opened, so this key is the right one - and that is
                // the fact the CHANS view reports, whatever the payload turned
                // out to be. Only GRP_TXT parses into a sender and a line of
                // text; a GRP_DATA carries something binary and there is
                // nothing to show. Setting channelIdx only in the text branch
                // made a channel that carries data read "never heard" for ever
                // while opening every one of its frames, which is precisely
                // the false signal that view exists to prevent.
                out.channelIdx = i;
                out.haveChannel = true;
                if (f.type == TYPE_GRP_TXT && parseGroupText(out.plain, plen, out.grp))
                    out.haveGroup = true;
                break;
            }
            break;
        }
        case TYPE_TRACE:
            out.haveTrace = parseTrace(f, out.trace);
            break;
        case TYPE_REQ: case TYPE_RESPONSE: case TYPE_TXT_MSG: case TYPE_PATH:
            if (f.payloadLen >= 2) { out.destHash = f.payload[0]; out.srcHash = f.payload[1]; }
            break;
        case TYPE_ANON_REQ:
            if (f.payloadLen >= 1) out.destHash = f.payload[0];
            break;
        case TYPE_ACK:
            if (f.payloadLen >= 4) memcpy(out.ackCrc, f.payload, 4);
            break;
        default: break;
    }
    return true;
}

static size_t pathText(const Frame& f, char* out, size_t cap) {
    size_t o = 0;
    for (uint8_t i = 0; i < f.hops && o + 6 < cap; i++) {
        if (f.hashSize == 2) o += (size_t)snprintf(out + o, cap - o, "%s%02x%02x", i ? "," : "", f.path[2*i], f.path[2*i+1]);
        else                 o += (size_t)snprintf(out + o, cap - o, "%s%02x", i ? "," : "", f.path[i]);
    }
    if (!o && cap) out[0] = '\0';
    return o;
}

void summary(const Lora::Packet& pk, char* out, size_t cap) {
    Decoded d;
    if (!decode(pk, d)) { snprintf(out, cap, "MC bad frame"); return; }
    const Frame& f = d.f;
    char path[48];
    pathText(f, path, sizeof path);
    size_t o = (size_t)snprintf(out, cap, "%s %s", typeName(f.type), routeName(f.route));
    if (o >= cap) return;
    if (f.hops) o += (size_t)snprintf(out + o, cap - o, " via %s", path);
    if (o >= cap) return;
    switch (f.type) {
        case TYPE_ADVERT:
            if (d.haveAdvert) {
                o += (size_t)snprintf(out + o, cap - o, " %02x %s \"%s\"", d.adv.pubkey[0], nodeTypeName(d.adv.nodeType), d.adv.name);
                if (o < cap && d.adv.hasLatLon) snprintf(out + o, cap - o, " %.4f %.4f", d.adv.latE6 / 1e6, d.adv.lonE6 / 1e6);
            }
            break;
        case TYPE_GRP_TXT:
            if (d.haveGroup) snprintf(out + o, cap - o, " [%s] %s: %s", channel(d.channelIdx).name, d.grp.sender, d.grp.text);
            else snprintf(out + o, cap - o, " ch %02x (no key)", f.payloadLen ? f.payload[0] : 0);
            break;
        case TYPE_GRP_DATA:
            snprintf(out + o, cap - o, " ch %02x %u B", f.payloadLen ? f.payload[0] : 0, (unsigned)f.payloadLen); break;
        case TYPE_TRACE: {
            o += (size_t)snprintf(out + o, cap - o, " tag %08lx", (unsigned long)d.trace.tag);
            for (uint8_t i = 0; i < d.trace.nSnr && o < cap; i++)
                o += (size_t)snprintf(out + o, cap - o, " %d.%02d", d.trace.snr4[i] / 4, (d.trace.snr4[i] % 4) * 25);
            break;
        }
        case TYPE_REQ: case TYPE_RESPONSE: case TYPE_TXT_MSG: case TYPE_PATH:
            snprintf(out + o, cap - o, " %02x<-%02x %u B", d.destHash, d.srcHash, (unsigned)f.payloadLen); break;
        case TYPE_ANON_REQ:
            snprintf(out + o, cap - o, " to %02x %u B", d.destHash, (unsigned)f.payloadLen); break;
        case TYPE_ACK:
            snprintf(out + o, cap - o, " %02x%02x%02x%02x", d.ackCrc[0], d.ackCrc[1], d.ackCrc[2], d.ackCrc[3]); break;
        default:
            snprintf(out + o, cap - o, " %u B", (unsigned)f.payloadLen); break;
    }
}

}
