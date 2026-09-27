// SquachWatch-CYD — Meshtastic, off the air. See include/lora_meshtastic.h.
// Field numbers are mesh.proto's and telemetry.proto's as of firmware 2.8;
// the crypto is CryptoEngine.cpp's; the hash is Channels.cpp's.
#include "lora_meshtastic.h"
#include "lora_crypto.h"
#include "lora_pb.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>   // strcasecmp: a channel name typed by a person

namespace Meshtastic {

using namespace LoraCrypto;

// ---- header ----------------------------------------------------------------
bool parseHeader(const uint8_t* d, uint8_t len, Header& h) {
    if (len < 16) return false;
    h.to   = rd32le(d);
    h.from = rd32le(d + 4);
    h.id   = rd32le(d + 8);
    const uint8_t f = d[12];
    h.hopLimit = f & 0x07;
    h.wantAck  = (f & 0x08) != 0;
    h.viaMqtt  = (f & 0x10) != 0;
    h.hopStart = (f >> 5) & 0x07;
    h.channelHash = d[13];
    h.nextHop   = d[14];
    h.relayNode = d[15];
    return h.id != 0;
}

uint8_t hopsAway(const Header& h) {
    if (!h.hopStart) return 0xFF;
    return h.hopStart >= h.hopLimit ? (uint8_t)(h.hopStart - h.hopLimit) : 0;
}

void nodeId(uint32_t num, char out[12]) { snprintf(out, 12, "!%08lx", (unsigned long)num); }

// ---- channels -----------------------------------------------------------
static const uint8_t DEFAULT_KEY[16] = {
    0xd4, 0xf1, 0xbb, 0x3a, 0x20, 0x29, 0x07, 0x59, 0xf0, 0xbc, 0xff, 0xab, 0xcf, 0x4e, 0x69, 0x01 };

void expandPsk(uint8_t psk, uint8_t out[16]) {
    memcpy(out, DEFAULT_KEY, 16);
    if (psk == 0) { memset(out, 0, 16); return; }
    out[15] = (uint8_t)(out[15] + (psk - 1));
}

uint8_t channelHash(const char* name, const uint8_t* key, uint8_t keyLen) {
    uint8_t h = 0;
    for (; *name; name++) h ^= (uint8_t)*name;
    for (uint8_t i = 0; i < keyLen; i++) h ^= key[i];
    return h;
}

static const char* const PRESET_NAMES[] = {
    "LongFast", "MediumFast", "ShortSlow", "ShortFast", "MediumSlow", "LongSlow", "LongMod",
    "LongTurbo", "ShortTurbo", "MediumTurbo", "NarrowSlow", "NarrowFast", "LiteFast", "LiteSlow",
};
static const uint8_t N_PRESETS = sizeof PRESET_NAMES / sizeof PRESET_NAMES[0];
static const uint8_t MAX_USER = 6;
static const uint8_t N_BUILTIN = (uint8_t)(2 * N_PRESETS);

// The presets on the default key, the presets in ham mode (no key, so the
// hash is the name's alone), then the user's.
static Channel s_user[MAX_USER];
static uint8_t s_nUser = 0;
// Index-for-index with channel(). A parallel array rather than a field in
// Channel, because channel() builds each preset from the tables above on every
// call and a counter in that struct would be reset by its own getter.
static uint32_t s_frames[N_BUILTIN + MAX_USER];
static uint32_t s_lastMs[N_BUILTIN + MAX_USER];

uint8_t channelCount() { return (uint8_t)(N_BUILTIN + s_nUser); }
uint8_t userChannelCount() { return s_nUser; }
uint8_t maxUserChannels() { return MAX_USER; }

// By value: see the note on MeshCore::channel(). One file-static rebuilt on
// every call was a buffer shared between the decode on the radio task and the
// CHANS view's thirty reads a second on loop(). Returning a copy also gives the
// past-the-end case a defined answer -- a zeroed channel, which is disabled and
// opens nothing -- where the static used to hand back whatever a previous
// caller had left in it.
Channel channel(uint8_t i) {
    Channel c;
    memset(&c, 0, sizeof c);
    if (i < N_PRESETS) {
        strncpy(c.name, PRESET_NAMES[i], sizeof c.name - 1);
        memcpy(c.key, DEFAULT_KEY, 16); c.keyLen = 16;
        c.hash = channelHash(c.name, c.key, 16);
        c.enabled = true;      // built in, never switched off
        return c;
    }
    i -= N_PRESETS;
    if (i < N_PRESETS) {
        strncpy(c.name, PRESET_NAMES[i], sizeof c.name - 1);
        c.keyLen = 0;
        c.hash = channelHash(c.name, nullptr, 0);
        c.enabled = true;
        return c;
    }
    i -= N_PRESETS;
    if (i < s_nUser) return s_user[i];
    return c;
}

bool addChannel(const char* name, const char* keyText) {
    if (s_nUser >= MAX_USER || !name || !*name) return false;
    // A preset's name is already held, so "LongFast" with a key of one's own
    // has to be called something else -- the hash is the name's as well as
    // the key's, and two rows with one name would be indistinguishable on
    // screen.
    if (findChannel(name) >= 0) return false;
    Channel& c = s_user[s_nUser];
    memset(&c, 0, sizeof c);
    strncpy(c.name, name, sizeof c.name - 1);
    size_t n = 0;
    if (keyText && *keyText) {
        n = fromBase64(keyText, c.key, sizeof c.key);
        if (n != 16 && n != 32 && n != 1) n = fromHex(keyText, c.key, sizeof c.key);
        if (n == 1) { expandPsk(c.key[0], c.key); n = c.key[0] ? 16 : 0; }
        else if (n > 0 && n < 16) { memset(c.key + n, 0, 16 - n); n = 16; }   // zero-padded, as the firmware does
        else if (n > 16 && n < 32) { memset(c.key + n, 0, 32 - n); n = 32; }
        else if (n != 16 && n != 32 && n != 0) return false;
    }
    // A key that was given and did not parse is a typo, not "no key".
    if (keyText && *keyText && n == 0 && !(keyText[0] == '0' && keyText[1] == '\0')) return false;
    c.keyLen = (uint8_t)n;
    c.hash = channelHash(c.name, c.key, c.keyLen);
    c.enabled = true;
    s_frames[N_BUILTIN + s_nUser] = 0;
    s_lastMs[N_BUILTIN + s_nUser] = 0;
    s_nUser++;
    return true;
}

void clearUserChannels() {
    s_nUser = 0;
    memset(s_frames + N_BUILTIN, 0, sizeof s_frames - N_BUILTIN * sizeof s_frames[0]);
    memset(s_lastMs + N_BUILTIN, 0, sizeof s_lastMs - N_BUILTIN * sizeof s_lastMs[0]);
}

int findChannel(const char* name) {
    if (!name || !*name) return -1;
    for (uint8_t i = 0; i < channelCount(); i++)
        if (strcasecmp(channel(i).name, name) == 0) return (int)i;
    return -1;
}

bool removeChannel(uint8_t i) {
    if (i < N_BUILTIN || i >= channelCount()) return false;
    const uint8_t u = (uint8_t)(i - N_BUILTIN);
    for (uint8_t k = u; k + 1 < s_nUser; k++) s_user[k] = s_user[k + 1];
    for (uint8_t k = i; k + 1 < channelCount(); k++) { s_frames[k] = s_frames[k + 1]; s_lastMs[k] = s_lastMs[k + 1]; }
    s_nUser--;
    s_frames[N_BUILTIN + s_nUser] = 0;
    s_lastMs[N_BUILTIN + s_nUser] = 0;
    return true;
}

void setChannelEnabled(uint8_t i, bool on) {
    if (i < N_BUILTIN || i >= channelCount()) return;
    s_user[i - N_BUILTIN].enabled = on;
}

void noteChannelHeard(uint8_t i, uint32_t nowMs) {
    if (i >= channelCount()) return;
    s_frames[i]++;
    s_lastMs[i] = nowMs;
}
uint32_t channelFrames(uint8_t i) { return i < channelCount() ? s_frames[i] : 0; }
uint32_t channelLastMs(uint8_t i) { return i < channelCount() ? s_lastMs[i] : 0; }

// ---- the cipher ---------------------------------------------------------------
void decrypt(const Header& h, const Channel& c, uint8_t* payload, uint8_t len) {
    if (!c.keyLen) return;
    // AES-256 for 32-byte keys is the firmware's rule; this reads the
    // 16-byte ones, which is every preset. A 32-byte key falls through as
    // "cannot open", never as a wrong answer.
    if (c.keyLen != 16) return;
    uint8_t nonce[16] = {0};
    wr32le(nonce, h.id);            // packet id as a 64-bit little-endian number
    wr32le(nonce + 8, h.from);      // then the sender; the last four bytes stay zero
    Aes128 k;
    k.setKey(c.key);
    ctr(k, nonce, payload, len);
}

bool parseData(const uint8_t* p, uint8_t len, Data& out) {
    memset(&out, 0, sizeof out);
    Pb::Reader r(p, len);
    Pb::Field f;
    bool havePort = false;
    while (r.next(f)) {
        switch (f.num) {
            case 1: if (f.wire == Pb::VARINT) { out.portnum = (uint32_t)f.varint; havePort = true; } break;
            case 2: if (f.wire == Pb::BYTES) { out.payload = f.bytes; out.payloadLen = (uint8_t)(f.len > 255 ? 255 : f.len); } break;
            case 3: out.wantResponse = f.varint != 0; break;
            case 4: out.dest = f.fixed32; break;
            case 5: out.source = f.fixed32; break;
            case 6: out.requestId = f.fixed32; break;
            case 7: out.replyId = f.fixed32; break;
            case 8: out.emoji = f.fixed32; break;
            case 9: out.bitfield = (uint32_t)f.varint; break;
            case 10: out.signed_ = f.wire == Pb::BYTES && f.len == 64; break;
            default: break;
        }
    }
    // A wrong key gives noise, and noise almost never starts with the
    // portnum tag and a port under 512 that then walks cleanly to the end.
    return havePort && out.portnum > 0 && out.portnum < 512 && r.p == r.end;
}

const char* portName(uint32_t port) {
    switch (port) {
        case PORT_TEXT:          return "TEXT";
        case PORT_REMOTE_HW:     return "REMOTE HW";
        case PORT_POSITION:      return "POSITION";
        case PORT_NODEINFO:      return "NODEINFO";
        case PORT_ROUTING:       return "ROUTING";
        case PORT_ADMIN:         return "ADMIN";
        case PORT_TEXT_COMPRESSED: return "TEXT (compressed)";
        case PORT_WAYPOINT:      return "WAYPOINT";
        case PORT_AUDIO:         return "AUDIO";
        case PORT_DETECTION:     return "DETECTION";
        case PORT_ALERT:         return "ALERT";
        case PORT_KEY_VERIFY:    return "KEY VERIFY";
        case PORT_REPLY:         return "REPLY";
        case PORT_IP_TUNNEL:     return "IP TUNNEL";
        case PORT_PAXCOUNTER:    return "PAXCOUNTER";
        case PORT_SERIAL:        return "SERIAL";
        case PORT_STORE_FORWARD: return "STORE&FWD";
        case PORT_RANGE_TEST:    return "RANGE TEST";
        case PORT_TELEMETRY:     return "TELEMETRY";
        case PORT_ZPS:           return "ZPS";
        case PORT_SIMULATOR:     return "SIMULATOR";
        case PORT_TRACEROUTE:    return "TRACEROUTE";
        case PORT_NEIGHBORINFO:  return "NEIGHBORS";
        case PORT_ATAK_PLUGIN:   return "ATAK";
        case PORT_MAP_REPORT:    return "MAP REPORT";
        case PORT_POWERSTRESS:   return "POWER STRESS";
        case PORT_PRIVATE:       return "PRIVATE";
        case PORT_ATAK_FORWARDER: return "ATAK FWD";
        default:                 return "PORT ?";
    }
}

// ---- payloads ---------------------------------------------------------------
bool parseUser(const uint8_t* p, uint8_t len, User& u) {
    memset(&u, 0, sizeof u);
    Pb::Reader r(p, len);
    Pb::Field f;
    bool any = false;
    while (r.next(f)) {
        any = true;
        switch (f.num) {
            case 1: if (f.wire == Pb::BYTES) Pb::str(f, u.id, sizeof u.id); break;
            case 2: if (f.wire == Pb::BYTES) Pb::str(f, u.longName, sizeof u.longName); break;
            case 3: if (f.wire == Pb::BYTES) Pb::str(f, u.shortName, sizeof u.shortName); break;
            case 5: u.hwModel = (uint32_t)f.varint; break;
            case 6: u.licensed = f.varint != 0; break;
            case 7: u.role = (uint8_t)f.varint; break;
            case 8: if (f.wire == Pb::BYTES && f.len == 32) { memcpy(u.pubkey, f.bytes, 32); u.hasPubkey = true; } break;
            default: break;
        }
    }
    return any && r.p == r.end;
}

const char* roleName(uint8_t role) {
    switch (role) {
        case 0: return "CLIENT";       case 1: return "CLIENT_MUTE";  case 2: return "ROUTER";
        case 3: return "ROUTER_CLIENT"; case 4: return "REPEATER";    case 5: return "TRACKER";
        case 6: return "SENSOR";       case 7: return "TAK";          case 8: return "CLIENT_HIDDEN";
        case 9: return "LOST&FOUND";   case 10: return "TAK_TRACKER"; case 11: return "ROUTER_LATE";
        case 12: return "CLIENT_BASE"; default: return "ROLE ?";
    }
}

const char* hwModelName(uint32_t m, char* buf, size_t cap) {
    // The boards one meets; the rest print their number, which the apps
    // resolve. From mesh.proto's HardwareModel.
    switch (m) {
        case 4:  return "T-Beam";            case 7:  return "T-Echo";
        case 9:  return "RAK4631";           case 12: return "T-Beam S3";
        case 16: return "T3-S3";             case 43: return "Heltec V3";
        case 44: return "Heltec WSL V3";     case 48: return "Heltec Tracker";
        case 49: return "Heltec Paper";      case 50: return "T-Deck";
        case 51: return "T-Watch S3";        case 69: return "Heltec T114";
        case 70: return "SenseCAP Indicator"; case 71: return "T1000-E";
        case 84: return "WisMesh Tap";       case 97: return "CrowPanel";
        case 37: return "Portduino";
        default:
            snprintf(buf, cap, "HW %lu", (unsigned long)m);
            return buf;
    }
}

bool parsePosition(const uint8_t* p, uint8_t len, Position& out) {
    memset(&out, 0, sizeof out);
    out.precisionBits = 32;
    Pb::Reader r(p, len);
    Pb::Field f;
    bool any = false, lat = false, lon = false;
    while (r.next(f)) {
        any = true;
        switch (f.num) {
            case 1:  out.latI = (int32_t)f.fixed32; lat = true; break;
            case 2:  out.lonI = (int32_t)f.fixed32; lon = true; break;
            case 3:  out.altitude = (int32_t)f.varint; break;
            case 4:  out.time = f.fixed32; break;
            case 15: out.groundSpeed = (uint32_t)f.varint; break;
            case 19: out.satsInView = (uint32_t)f.varint; break;
            case 23: out.precisionBits = (uint32_t)f.varint; break;
            default: break;
        }
    }
    out.hasLatLon = lat && lon;
    return any && r.p == r.end;
}

static void parseDeviceMetrics(const Pb::Field& m, Telemetry& t) {
    Pb::Reader r(m.bytes, m.len);
    Pb::Field f;
    while (r.next(f)) {
        switch (f.num) {
            case 1: t.batteryLevel = (uint32_t)f.varint; break;
            case 2: t.voltage = Pb::f32(f); break;
            case 3: t.channelUtil = Pb::f32(f); break;
            case 4: t.airUtilTx = Pb::f32(f); break;
            case 5: t.uptime = (uint32_t)f.varint; break;
            default: break;
        }
    }
}
static void parseEnvMetrics(const Pb::Field& m, Telemetry& t) {
    Pb::Reader r(m.bytes, m.len);
    Pb::Field f;
    while (r.next(f)) {
        switch (f.num) {
            case 1: t.temperature = Pb::f32(f); t.hasTemp = true; break;
            case 2: t.humidity = Pb::f32(f); t.hasHumidity = true; break;
            case 3: t.pressure = Pb::f32(f); t.hasPressure = true; break;
            default: break;
        }
    }
}
static void parseLocalStats(const Pb::Field& m, Telemetry& t) {
    Pb::Reader r(m.bytes, m.len);
    Pb::Field f;
    while (r.next(f)) {
        switch (f.num) {
            case 1: t.uptime = (uint32_t)f.varint; break;
            case 2: t.channelUtil = Pb::f32(f); break;
            case 3: t.airUtilTx = Pb::f32(f); break;
            case 4: t.packetsTx = (uint32_t)f.varint; break;
            case 5: t.packetsRx = (uint32_t)f.varint; break;
            case 6: t.packetsRxBad = (uint32_t)f.varint; break;
            case 7: t.onlineNodes = (uint32_t)f.varint; break;
            case 8: t.totalNodes = (uint32_t)f.varint; break;
            case 9: t.rxDupe = (uint32_t)f.varint; break;
            case 10: t.txRelay = (uint32_t)f.varint; break;
            case 11: t.txRelayCanceled = (uint32_t)f.varint; break;
            default: break;
        }
    }
}

bool parseTelemetry(const uint8_t* p, uint8_t len, Telemetry& out) {
    memset(&out, 0, sizeof out);
    Pb::Reader r(p, len);
    Pb::Field f;
    bool any = false;
    while (r.next(f)) {
        any = true;
        if (f.num == 1) { out.time = f.fixed32; continue; }
        if (f.wire != Pb::BYTES) continue;
        switch (f.num) {
            case 2: out.kind = 2; parseDeviceMetrics(f, out); break;
            case 3: out.kind = 3; parseEnvMetrics(f, out); break;
            case 6: out.kind = 6; parseLocalStats(f, out); break;
            case 4: case 5: case 7: case 8: out.kind = (uint8_t)f.num; break;
            default: break;
        }
    }
    return any && r.p == r.end;
}

bool parseRouteDiscovery(const uint8_t* p, uint8_t len, RouteDiscovery& out) {
    memset(&out, 0, sizeof out);
    Pb::Reader r(p, len);
    Pb::Field f;
    int32_t tmp[9];
    while (r.next(f)) {
        switch (f.num) {
            case 1:
                if (f.wire == Pb::BYTES) out.n = Pb::packedFixed32(f, out.route, 8);
                else if (f.wire == Pb::FIXED32 && out.n < 8) out.route[out.n++] = f.fixed32;
                break;
            case 3:
                if (f.wire == Pb::BYTES) out.nBack = Pb::packedFixed32(f, out.routeBack, 8);
                else if (f.wire == Pb::FIXED32 && out.nBack < 8) out.routeBack[out.nBack++] = f.fixed32;
                break;
            case 2:
                if (f.wire == Pb::BYTES) { out.nSnr = Pb::packedVarint(f, tmp, 9); for (uint8_t i = 0; i < out.nSnr; i++) out.snrTowards[i] = (int8_t)tmp[i]; }
                else if (out.nSnr < 9) out.snrTowards[out.nSnr++] = (int8_t)(int32_t)f.varint;
                break;
            case 4:
                if (f.wire == Pb::BYTES) { out.nSnrBack = Pb::packedVarint(f, tmp, 9); for (uint8_t i = 0; i < out.nSnrBack; i++) out.snrBack[i] = (int8_t)tmp[i]; }
                else if (out.nSnrBack < 9) out.snrBack[out.nSnrBack++] = (int8_t)(int32_t)f.varint;
                break;
            default: break;
        }
    }
    return r.p == r.end;
}

bool parseRouting(const uint8_t* p, uint8_t len, Routing& out) {
    memset(&out, 0, sizeof out);
    Pb::Reader r(p, len);
    Pb::Field f;
    while (r.next(f)) {
        switch (f.num) {
            case 1: out.kind = 1; if (f.wire == Pb::BYTES) parseRouteDiscovery(f.bytes, (uint8_t)f.len, out.rd); break;
            case 2: out.kind = 2; if (f.wire == Pb::BYTES) parseRouteDiscovery(f.bytes, (uint8_t)f.len, out.rd); break;
            case 3: out.kind = 3; out.errorReason = (uint8_t)f.varint; break;
            default: break;
        }
    }
    return r.p == r.end;
}

const char* routingErrorName(uint8_t e) {
    switch (e) {
        case 0: return "ACK";            case 1: return "NO ROUTE";      case 2: return "GOT NAK";
        case 3: return "TIMEOUT";        case 4: return "NO INTERFACE";  case 5: return "MAX RETRANSMIT";
        case 6: return "NO CHANNEL";     case 7: return "TOO LARGE";     case 8: return "NO RESPONSE";
        case 9: return "DUTY CYCLE";     case 32: return "BAD REQUEST";  case 33: return "NOT AUTHORIZED";
        case 34: return "PKI FAILED";    case 35: return "PKI UNKNOWN PUBKEY"; case 36: return "ADMIN BAD SESSION";
        case 37: return "ADMIN PUBKEY UNAUTHORIZED"; case 38: return "RATE LIMIT";
        default: return "ERROR ?";
    }
}

bool parseNeighborInfo(const uint8_t* p, uint8_t len, NeighborInfo& out) {
    memset(&out, 0, sizeof out);
    Pb::Reader r(p, len);
    Pb::Field f;
    while (r.next(f)) {
        switch (f.num) {
            case 1: out.nodeId = (uint32_t)f.varint; break;
            case 2: out.lastSentBy = (uint32_t)f.varint; break;
            case 3: out.interval = (uint32_t)f.varint; break;
            case 4:
                if (f.wire == Pb::BYTES && out.n < 12) {
                    Pb::Reader nr(f.bytes, f.len);
                    Pb::Field nf;
                    uint32_t id = 0; float snr = 0;
                    while (nr.next(nf)) {
                        if (nf.num == 1) id = (uint32_t)nf.varint;
                        else if (nf.num == 2) snr = Pb::f32(nf);
                    }
                    out.nb[out.n].id = id;
                    out.nb[out.n].snr4 = (int8_t)(snr * 4);
                    out.n++;
                }
                break;
            default: break;
        }
    }
    return r.p == r.end;
}

// ---- all of it ---------------------------------------------------------------
bool decode(const Lora::Packet& pk, Decoded& out) {
    memset(&out, 0, sizeof out);
    if (!parseHeader(pk.data, pk.len, out.hdr)) return false;
    const uint8_t plen = (uint8_t)(pk.len - 16);
    if (!plen) return true;
    if (plen > sizeof out.plain) return true;
    if (out.hdr.channelHash == 0 && out.hdr.to != BROADCAST) return true;   // PKI: nobody's business

    // Every channel whose hash matches gets a try; the first whose Data
    // parses wins. Ham-mode channels are plaintext, so "decrypt" is a copy.
    for (uint8_t i = 0; i < channelCount(); i++) {
        const Channel c = channel(i);
        if (!c.enabled || c.hash != out.hdr.channelHash) continue;
        memcpy(out.plain, pk.data + 16, plen);
        decrypt(out.hdr, c, out.plain, plen);
        if (!parseData(out.plain, plen, out.data)) continue;
        out.haveData = true;
        out.channelIdx = i;
        break;
    }
    if (!out.haveData) return true;

    const uint8_t* p = out.data.payload;
    const uint8_t  n = out.data.payloadLen;
    switch (out.data.portnum) {
        case PORT_TEXT: case PORT_RANGE_TEST: case PORT_DETECTION: case PORT_ALERT: {
            const uint8_t m = n < sizeof out.text - 1 ? n : (uint8_t)(sizeof out.text - 1);
            memcpy(out.text, p, m); out.text[m] = '\0';
            break;
        }
        case PORT_NODEINFO:     parseUser(p, n, out.user); break;
        case PORT_POSITION:     parsePosition(p, n, out.pos); break;
        case PORT_TELEMETRY:    parseTelemetry(p, n, out.tel); break;
        case PORT_TRACEROUTE:   parseRouteDiscovery(p, n, out.route); break;
        case PORT_ROUTING:      parseRouting(p, n, out.routing); break;
        case PORT_NEIGHBORINFO: parseNeighborInfo(p, n, out.nbr); break;
        default: break;
    }
    return true;
}

void summary(const Lora::Packet& pk, char* out, size_t cap) {
    Decoded d;
    if (!decode(pk, d)) { snprintf(out, cap, "MT short frame"); return; }
    char from[12], to[12];
    nodeId(d.hdr.from, from);
    if (d.hdr.to == BROADCAST) snprintf(to, sizeof to, "all"); else nodeId(d.hdr.to, to);
    char hops[8];
    if (d.hdr.hopStart) snprintf(hops, sizeof hops, "%u/%u", (unsigned)hopsAway(d.hdr), (unsigned)d.hdr.hopStart);
    else snprintf(hops, sizeof hops, "?/%u", (unsigned)d.hdr.hopLimit);
    size_t o = (size_t)snprintf(out, cap, "%s>%s hop%s ch%02x", from, to, hops, (unsigned)d.hdr.channelHash);
    if (o >= cap) return;
    if (!d.haveData) {
        snprintf(out + o, cap - o, d.hdr.channelHash == 0 && d.hdr.to != BROADCAST ? " PKI DM" : " (private)");
        return;
    }
    switch (d.data.portnum) {
        case PORT_TEXT: case PORT_RANGE_TEST:
            snprintf(out + o, cap - o, " %s %s", portName(d.data.portnum), d.text); break;
        case PORT_NODEINFO: {
            char hw[12];
            snprintf(out + o, cap - o, " NODEINFO %s \"%s\" %s %s", d.user.shortName, d.user.longName,
                     hwModelName(d.user.hwModel, hw, sizeof hw), roleName(d.user.role));
            break;
        }
        case PORT_POSITION:
            if (d.pos.hasLatLon)
                snprintf(out + o, cap - o, " POS %.5f %.5f %ldm p%lu", d.pos.latI / 1e7, d.pos.lonI / 1e7,
                         (long)d.pos.altitude, (unsigned long)d.pos.precisionBits);
            else snprintf(out + o, cap - o, " POS (no fix)");
            break;
        case PORT_TELEMETRY:
            if (d.tel.kind == 2) snprintf(out + o, cap - o, " TELEM bat %lu%% %.2fV ch %.1f%% tx %.1f%%",
                                          (unsigned long)d.tel.batteryLevel, d.tel.voltage, d.tel.channelUtil, d.tel.airUtilTx);
            else if (d.tel.kind == 3) snprintf(out + o, cap - o, " ENV %.1fC %.0f%% %.0fhPa", d.tel.temperature, d.tel.humidity, d.tel.pressure);
            else if (d.tel.kind == 6) snprintf(out + o, cap - o, " STATS rx %lu tx %lu relay %lu dupe %lu nodes %lu/%lu",
                                               (unsigned long)d.tel.packetsRx, (unsigned long)d.tel.packetsTx, (unsigned long)d.tel.txRelay,
                                               (unsigned long)d.tel.rxDupe, (unsigned long)d.tel.onlineNodes, (unsigned long)d.tel.totalNodes);
            else snprintf(out + o, cap - o, " TELEM kind %u", (unsigned)d.tel.kind);
            break;
        case PORT_TRACEROUTE: {
            o += (size_t)snprintf(out + o, cap - o, " TRACE %u hops", (unsigned)d.route.n);
            for (uint8_t i = 0; i < d.route.n && o < cap; i++) {
                char h[12]; nodeId(d.route.route[i], h);
                o += (size_t)snprintf(out + o, cap - o, " %s", h);
            }
            break;
        }
        case PORT_ROUTING:
            snprintf(out + o, cap - o, " ROUTING %s", d.routing.kind == 3 ? routingErrorName(d.routing.errorReason) : d.routing.kind == 1 ? "route req" : "route reply"); break;
        case PORT_NEIGHBORINFO:
            snprintf(out + o, cap - o, " NEIGHBORS %u", (unsigned)d.nbr.n); break;
        default:
            snprintf(out + o, cap - o, " %s %u B", portName(d.data.portnum), (unsigned)d.data.payloadLen); break;
    }
}

}
