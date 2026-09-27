// SquachWatch-CYD — the node table. See include/lora_nodes.h.
#include "lora_nodes.h"
#include "lora_meshtastic.h"
#include "lora_meshcore.h"
#include "lora_lorawan.h"
#include "lora_aprs.h"
#include "lora_fanet.h"
#include "lora_profiles.h"
#include "lora_survey.h"
#include "lora_msgs.h"
#include <stdio.h>
#include <string.h>

namespace Lora {
namespace Nodes {

static Node    s_nodes[CAP];
static uint8_t s_n = 0;

uint8_t     count() { return s_n; }
const Node* at(uint8_t i) { return i < s_n ? &s_nodes[i] : nullptr; }
void        clear() { s_n = 0; }

static Node* find(Proto p, uint64_t id) {
    for (uint8_t i = 0; i < s_n; i++) if (s_nodes[i].proto == p && s_nodes[i].id == id) return &s_nodes[i];
    return nullptr;
}

// A row for this sender, new or found. When the table is full the one
// unheard for longest gives way.
static Node* get(Proto p, uint64_t id, uint32_t now) {
    Node* n = find(p, id);
    if (n) return n;
    if (s_n < CAP) n = &s_nodes[s_n++];
    else {
        n = &s_nodes[0];
        for (uint8_t i = 1; i < CAP; i++) if (s_nodes[i].lastMs < n->lastMs) n = &s_nodes[i];
    }
    memset(n, 0, sizeof *n);
    n->proto = p; n->id = id; n->firstMs = now; n->hourStartMs = now;
    n->rssi = -200; n->viaRssi = -200;
    return n;
}

// Five minutes of a bucket before the ratio in it is worth anything. The
// figure was already the guard on the flag; dutyKnown() is it with a name, so
// the screen and the console can ask the same question the flag asks.
static const uint32_t DUTY_MIN_WINDOW_MS = 300000u;

static void heard(Node& n, const Packet& pk, uint32_t now, Link link) {
    n.lastMs = now;
    n.packets++;
    if (now - n.hourStartMs >= 3600000u) { n.hourStartMs = now; n.hourAirMs = 0; }
    if (link == LINK_DIRECT) {
        n.rssi = pk.rssi; n.snr4 = pk.snr4;
        n.directPackets++;
        // Only what this node transmitted itself: see Node::airtimeMs.
        n.airtimeMs += pk.toaUs / 1000;
        n.hourAirMs += pk.toaUs / 1000;
        // The band it transmits in decides what it is allowed. Taken from the
        // last direct frame, which is the whole answer for a node that lives on
        // one profile and a coarse one for a node whose operator changed preset
        // mid-hour; a relayed copy says which band the RELAY used, so it must
        // not set this.
        n.dutyLimit = dutyLimitPermille(pk.freqHz);
    } else {
        n.viaRssi = pk.rssi; n.viaSnr4 = pk.snr4;
        n.viaPackets++;
    }
    if (link == LINK_DIRECT) {
        // The antenna survey is fed from HERE and from nowhere else, because
        // this is the one place the DIRECT/VIA rule is decided -- a second copy
        // of that rule, in a file whose whole premise is "direct only", would
        // be two rules drifting apart. Kept outside the branch above so it sits
        // next to nothing else and is obvious.
        //
        // n.tag is still empty on a node's FIRST frame: every decoder fills it
        // in after calling heard(). The survey keeps the newest non-empty tag it
        // is offered, so the second frame from a station names it.
        Survey::noteDirect(n.proto, n.id, n.tag, pk.rssi, pk.snr4, now);
    }
    if (n.dutyLimit && dutyKnown(n, now) && dutyPermille(n, now) > n.dutyLimit) n.flags |= NF_DUTY;
    else                                                                        n.flags &= (uint8_t)~NF_DUTY;
}

uint16_t dutyPermille(const Node& n, uint32_t now) {
    const uint32_t span = now - n.hourStartMs;
    if (span < 1000) return 0;
    return (uint16_t)(((uint64_t)n.hourAirMs * 1000ull) / span);
}

bool dutyKnown(const Node& n, uint32_t now) { return now - n.hourStartMs > DUTY_MIN_WINDOW_MS; }

static void noteMeshtastic(const Packet& pk, uint32_t now) {
    Meshtastic::Decoded d;
    if (!Meshtastic::decode(pk, d)) return;
    // Which key opened it, counted here and nowhere else: note() runs once per
    // frame off the air, while decode() runs again for every row the LIST and
    // CHANNELS views redraw. A counter inside the decoder would measure the
    // screen's refresh rate.
    if (d.haveData) Meshtastic::noteChannelHeard(d.channelIdx, now);
    Node& n = *get(Proto::MESHTASTIC, d.hdr.from, now);
    if (!n.tag[0]) Meshtastic::nodeId(d.hdr.from, n.tag);
    // hopsAway is hop_start - hop_limit, and 0xFF when hop_start is 0 -- a
    // firmware before 2.3, which gives no way to tell a relayed copy from a
    // first-hand one. Zero hops consumed is the only direct evidence there is;
    // relayNode would be a second test but it is one byte of a node number, so
    // one node in 256 aliases with the sender and it cannot stand alone.
    const uint8_t away = Meshtastic::hopsAway(d.hdr);
    heard(n, pk, now, away == 0 ? LINK_DIRECT : LINK_VIA);
    n.hopLimit = d.hdr.hopLimit;
    n.hops = away;
    if (d.hdr.hopLimit > 3 || d.hdr.hopStart > 3) n.flags |= NF_HOPS_HIGH;
    if (d.hdr.viaMqtt) n.flags |= NF_MQTT;
    if (!d.hdr.hopStart || !d.hdr.relayNode) n.flags |= NF_OLD_FW;
    if (!d.haveData) return;
    switch (d.data.portnum) {
        case Meshtastic::PORT_NODEINFO:
            if (d.user.longName[0]) strncpy(n.name, d.user.longName, sizeof n.name - 1);
            if (d.user.shortName[0]) { snprintf(n.tag, sizeof n.tag, "%s", d.user.shortName); }
            n.role = d.user.role;
            if (d.user.role == 3 || d.user.role == 4) n.flags |= NF_DEPRECATED;
            break;
        case Meshtastic::PORT_POSITION:
            if (d.pos.hasLatLon) { n.hasPos = true; n.latE7 = d.pos.latI; n.lonE7 = d.pos.lonI; }
            if (n.lastTelemMs && now - n.lastTelemMs < 5 * 60000u) n.flags |= NF_CHATTY;
            n.lastTelemMs = now;
            break;
        // Kept rather than printed and forgotten: see include/lora_msgs.h. A
        // Meshtastic text frame names its sender only by the header's `from`, so
        // the sender string is n.tag -- the short name once a NodeInfo has
        // arrived, and the "!3b1c9a2e" node number until then.
        //
        // PORT_ALERT rides along because it is text somebody wrote and meant to
        // be read, and the port travels with the row so a view can mark it as
        // the alert it is. PORT_RANGE_TEST and PORT_DETECTION also decode into
        // `text` and are deliberately NOT kept: a range test is a counter a node
        // emits on a timer and would fill the ring with its own noise, and a
        // detection is a sensor trigger rather than a message.
        case Meshtastic::PORT_TEXT:
        case Meshtastic::PORT_ALERT:
            Msgs::note(Proto::MESHTASTIC, d.channelIdx, (uint8_t)d.data.portnum, n.tag, d.hdr.from,
                       d.text, pk, now, away == 0, away);
            break;
        case Meshtastic::PORT_TELEMETRY:
            // The default is an hour; under thirty minutes is the floor the
            // firmware enforces on the default channel.
            if (n.lastTelemMs && now - n.lastTelemMs < 30 * 60000u) n.flags |= NF_CHATTY;
            n.lastTelemMs = now;
            break;
        default: break;
    }
}

static void noteMeshCore(const Packet& pk, uint32_t now, uint32_t epoch) {
    MeshCore::Decoded d;
    if (!MeshCore::decode(pk, d)) return;
    if (d.haveChannel) MeshCore::noteChannelHeard(d.channelIdx, now);   // opened, not necessarily text; see noteMeshtastic
    const MeshCore::Frame& f = d.f;
    // On a flood the path is the hops already TAKEN, so hops == 0 means we
    // received the sender's own transmission. On the direct routes the path is
    // the hops still to come and the count says nothing about who transmitted
    // the copy we heard. Hoisted here because the advert row and the group
    // message below both need the same test, and two copies of it is how the
    // two would come to disagree.
    const bool flood = f.route == MeshCore::ROUTE_FLOOD || f.route == MeshCore::ROUTE_TRANSPORT_FLOOD;
    // A group message that opened AND parsed into a sender and a line of text.
    // A GRP_DATA opened and did not: it is counted by noteChannelHeard above
    // and kept nowhere, which include/lora_msgs.h explains. The sender name is
    // a string the sender typed -- the frame carries no signature over it.
    if (d.haveGroup)
        Msgs::note(Proto::MESHCORE, d.channelIdx, MeshCore::TYPE_GRP_TXT, d.grp.sender, 0,
                   d.grp.text, pk, now, flood && f.hops == 0, f.hops);
    // Adverts name their sender in full. Everything else names only the
    // last relay (the path's newest hash) or a destination, which is not
    // the sender -- so those count against the relay's row, as relayed.
    if (d.haveAdvert) {
        uint64_t id = 0;
        for (int i = 0; i < 8; i++) id = (id << 8) | d.adv.pubkey[i];
        Node& n = *get(Proto::MESHCORE, id, now);
        // An advert that crossed repeaters was last transmitted by one of
        // them, so its signal belongs to the relay and not to the advertiser:
        // the bench saw eleven-hop paths routinely (docs/LORA.md section 1).
        // `flood` is the test hoisted above.
        heard(n, pk, now, (flood && f.hops == 0) ? LINK_DIRECT : LINK_VIA);
        snprintf(n.tag, sizeof n.tag, "%02x%02x%02x", d.adv.pubkey[0], d.adv.pubkey[1], d.adv.pubkey[2]);
        if (d.adv.hasName) strncpy(n.name, d.adv.name, sizeof n.name - 1);
        n.role = d.adv.nodeType;
        n.hops = f.hops;
        n.counter++;
        if (d.adv.hasLatLon) { n.hasPos = true; n.latE7 = d.adv.latE6 * 10; n.lonE7 = d.adv.lonE6 * 10; }
        if (epoch) {
            const uint32_t diff = d.adv.timestamp > epoch ? d.adv.timestamp - epoch : epoch - d.adv.timestamp;
            if (diff > 86400u) n.flags |= NF_BAD_CLOCK;
        }
        return;
    }
    if (f.hops && f.hashSize == 1 && flood) {
        // The newest path byte is the repeater that just sent this copy.
        const uint8_t h = f.path[f.hops - 1];
        // Match it to an advertised key when one is known; else its own row.
        Node* n = nullptr;
        for (uint8_t i = 0; i < s_n; i++)
            if (s_nodes[i].proto == Proto::MESHCORE && (uint8_t)(s_nodes[i].id >> 56) == h && s_nodes[i].name[0]) { n = &s_nodes[i]; break; }
        if (!n) {
            n = get(Proto::MESHCORE, 0x0100000000000000ull | h, now);
            if (!n->tag[0]) snprintf(n->tag, sizeof n->tag, "rpt %02x", h);
        }
        // This row IS the station whose transmission we just received, so the
        // signal is a measured link to it even though the frame is relayed.
        heard(*n, pk, now, LINK_DIRECT);
        n->hops = f.hops;
    }
}

static void noteLoRaWAN(const Packet& pk, uint32_t now) {
    LoRaWAN::Decoded d;
    if (!LoRaWAN::decode(pk, d) || d.isBeacon) return;
    const LoRaWAN::Frame& f = d.f;
    if (f.mtype == LoRaWAN::JOIN_REQUEST) {
        uint64_t id = 0;
        for (int i = 0; i < 8; i++) id = (id << 8) | f.devEui[i];
        Node& n = *get(Proto::LORAWAN, id, now);
        heard(n, pk, now, LINK_DIRECT);
        snprintf(n.tag, sizeof n.tag, "%02x%02x..%02x%02x", f.devEui[0], f.devEui[1], f.devEui[6], f.devEui[7]);
        strncpy(n.name, d.maker ? d.maker : "join request", sizeof n.name - 1);
        n.role = 1;
        n.counter++;
        if (n.counter > 3 && now - n.firstMs < 10 * 60000u) n.flags |= NF_CHATTY;   // joining over and over
        return;
    }
    if (f.mtype == LoRaWAN::JOIN_ACCEPT || f.mtype == LoRaWAN::PROPRIETARY) return;
    Node& n = *get(Proto::LORAWAN, f.devAddr, now);
    const bool fresh = n.packets == 0;
    // A class A uplink reaches its gateway in one hop and a downlink comes
    // from one, so what we heard is the device's own transmitter. TS011 relays
    // are the exception and the profile exists (src/lora_profiles.cpp), but
    // whether any are deployed within range is unverified.
    heard(n, pk, now, LINK_DIRECT);
    snprintf(n.tag, sizeof n.tag, "%08lx", (unsigned long)f.devAddr);
    if (d.net.op) strncpy(n.name, d.net.op, sizeof n.name - 1);
    n.role = f.uplink ? 0 : 2;
    if (f.uplink) {
        if (!fresh) {
            if (f.fCnt > n.counter + 1) n.lost = (uint16_t)(n.lost + (f.fCnt - n.counter - 1));
            else if (f.fCnt < n.counter) n.flags |= NF_RESETS;
        }
        n.counter = f.fCnt;
    }
}

static uint64_t textId(const char* s) {
    // FNV-1a over a callsign: stable, and a collision needs two hams to
    // share a hash, which the screen would show as one row.
    uint64_t h = 0xcbf29ce484222325ull;
    for (; *s; s++) { h ^= (uint8_t)*s; h *= 0x100000001b3ull; }
    return h;
}

static void noteAprs(const Packet& pk, uint32_t now) {
    Aprs::Frame f;
    if (!Aprs::parse(pk.data, pk.len, f)) return;
    Node& n = *get(Proto::APRS, textId(f.src), now);
    // A digipeater marks its entry in the path with '*', so an unmarked path
    // is the originator's own transmission. A digipeater that does not set the
    // flag is invisible to this test and its copy counts as direct.
    heard(n, pk, now, f.digipeated == 0 ? LINK_DIRECT : LINK_VIA);
    strncpy(n.tag, f.src, sizeof n.tag - 1);
    if (f.info.hasPos) { n.hasPos = true; n.latE7 = f.info.latE7; n.lonE7 = f.info.lonE7; }
    n.hops = f.digipeated;
    if (f.info.kind == Aprs::INFO_STATUS && f.info.text[0]) strncpy(n.name, f.info.text, sizeof n.name - 1);
}

static void noteMeshCom(const Packet& pk, uint32_t now) {
    MeshCom::Frame f;
    if (!MeshCom::parse(pk.data, pk.len, f) || f.type == 'A') return;
    Node& n = *get(Proto::MESHCOM, textId(f.src), now);
    // Relays append their call only with the track bit set (include/lora_aprs.h).
    // So an empty path proves first-hand reception when track is set, and
    // proves nothing at all when it is clear.
    heard(n, pk, now, (f.track && !f.path[0]) ? LINK_DIRECT : LINK_VIA);
    strncpy(n.tag, f.src, sizeof n.tag - 1);
    const char* hw = MeshCom::hwName(f.hwId);
    if (hw) snprintf(n.name, sizeof n.name, "%s fw%u", hw, (unsigned)f.fwVersion);
    else snprintf(n.name, sizeof n.name, "hw%u fw%u", (unsigned)f.hwId, (unsigned)f.fwVersion);
    if (f.hasInfo) { n.hasPos = true; n.latE7 = f.info.latE7; n.lonE7 = f.info.lonE7; }
    n.hops = f.hopsLeft;
    n.role = f.viaServer ? 1 : 0;
}

static void noteFanet(const Packet& pk, uint32_t now) {
    Fanet::Frame f;
    if (!Fanet::parse(pk.data, pk.len, f)) return;
    Node& n = *get(Proto::FANET, ((uint64_t)f.manufacturer << 16) | f.id, now);
    // FANET carries no path and no hop count: `forward` says the sender wants
    // a relay, not that this copy is one (include/lora_fanet.h). Counted as
    // direct because that is what the frame says and because a tracking frame
    // is normally heard straight off the aircraft; a forwarded copy would be
    // miscredited, and the fix for that is duplicate detection, not a guess.
    heard(n, pk, now, LINK_DIRECT);
    Fanet::addressText(f.manufacturer, f.id, n.tag);
    if (f.type == Fanet::T_NAME && f.text[0]) strncpy(n.name, f.text, sizeof n.name - 1);
    else if (!n.name[0]) { const char* m = Fanet::manufacturerName(f.manufacturer); if (m) strncpy(n.name, m, sizeof n.name - 1); }
    if (f.hasPos) { n.hasPos = true; n.latE7 = f.latE7; n.lonE7 = f.lonE7; }
    if (f.type == Fanet::T_TRACKING) n.role = (uint8_t)(0x10 | f.aircraft);
    else if (f.type == Fanet::T_GROUND) n.role = (uint8_t)(0x20 | f.groundType);
    else if (f.type == Fanet::T_SERVICE) n.role = 0x30;
}

void note(const Packet& pk, uint32_t now, uint32_t epoch) {
    if (pk.flags & PK_CRC_ERR) return;
    switch (pk.proto) {
        case Proto::MESHTASTIC: noteMeshtastic(pk, now); break;
        case Proto::MESHCORE:   noteMeshCore(pk, now, epoch); break;
        case Proto::LORAWAN:    noteLoRaWAN(pk, now); break;
        case Proto::APRS:       noteAprs(pk, now); break;
        case Proto::MESHCOM:    noteMeshCom(pk, now); break;
        case Proto::FANET:      noteFanet(pk, now); break;
        default: break;
    }
}

uint8_t order(uint8_t* idx, uint8_t cap) {
    uint8_t n = 0;
    for (uint8_t i = 0; i < s_n && n < cap; i++) idx[n++] = i;
    // Insertion sort by lastMs, newest first: the table is small.
    for (uint8_t i = 1; i < n; i++) {
        const uint8_t v = idx[i];
        int j = i - 1;
        while (j >= 0 && s_nodes[idx[j]].lastMs < s_nodes[v].lastMs) { idx[j + 1] = idx[j]; j--; }
        idx[j + 1] = v;
    }
    return n;
}

const char* roleText(const Node& n) {
    switch (n.proto) {
        case Proto::MESHTASTIC: return Meshtastic::roleName(n.role);
        case Proto::MESHCORE:   return n.role ? MeshCore::nodeTypeName(n.role) : "relay";
        case Proto::LORAWAN:    return n.role == 1 ? "joining" : n.role == 2 ? "downlink" : "device";
        case Proto::APRS:       return n.hops ? "digipeated" : "direct";
        case Proto::MESHCOM:    return n.role ? "via server" : "rf";
        case Proto::FANET:
            if ((n.role & 0xF0) == 0x10) return Fanet::aircraftName(n.role & 0x0F);
            if ((n.role & 0xF0) == 0x20) return "ground";
            if (n.role == 0x30) return "station";
            return "";
        default: return "";
    }
}

const char* flagsText(const Node& n, char* out, size_t cap) {
    size_t o = 0;
    if (cap) out[0] = '\0';
    struct { uint8_t f; const char* s; } names[] = {
        { NF_HOPS_HIGH, "hops" }, { NF_DEPRECATED, "oldrole" }, { NF_MQTT, "mqtt" }, { NF_CHATTY, "chatty" },
        { NF_DUTY, "duty" }, { NF_BAD_CLOCK, "clock" }, { NF_RESETS, "reset" }, { NF_OLD_FW, "oldfw" },
    };
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
        if ((n.flags & names[i].f) && o + strlen(names[i].s) + 2 < cap)
            o += (size_t)snprintf(out + o, cap - o, "%s%s", o ? " " : "", names[i].s);
    return out;
}

}
}
