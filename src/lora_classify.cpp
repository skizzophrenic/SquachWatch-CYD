// SquachWatch-CYD — naming a LoRa frame. See include/lora_classify.h.
//
// The sync word already sorted most of the world: 0x34 is LoRaWAN, 0x2B is
// Meshtastic or MeshCom (the frequency tells them apart), 0xF1 is FANET, and
// 0x12 is everybody else -- MeshCore, LoRa APRS, RNode, the demos. Within a
// sync word the bytes decide. Each decoder gets asked "is this yours?" and
// the first to say yes names it; the order is most-specific first, so a
// frame that happens to start with an APRS prefix is APRS before it is a
// MeshCore packet whose header byte happens to be 0x3C.
#include "lora_classify.h"
#include "lora_profiles.h"
#include "lora_meshtastic.h"
#include "lora_meshcore.h"
#include "lora_lorawan.h"
#include "lora_aprs.h"
#include "lora_fanet.h"
#include <stdio.h>
#include <string.h>

namespace Lora {

static bool looksAprs(const Packet& pk) {
    return pk.len > 3 && pk.data[0] == 0x3C && pk.data[1] == 0xFF && pk.data[2] == 0x01;
}

static bool looksMeshCom(const Packet& pk) {
    // type byte, 4-byte id, flags, then ASCII "SRC>DEST" ... 0x7E at the end
    if (pk.len < 12) return false;
    const uint8_t t = pk.data[0];
    if (t == 'A') return pk.len == 12 && (pk.data[5] & 0x80);
    if (t != ':' && t != '!' && t != '@') return false;
    return pk.data[pk.len - 1] == 0x7E && memchr(pk.data + 6, '>', pk.len - 6) != nullptr;
}

static bool looksMeshtastic(const Packet& pk) {
    // 16-byte header, then at least an encrypted Data. Flags' reserved bits
    // are all used now, so the only cheap test is the size and a non-zero id.
    return pk.len >= 17 && (pk.data[8] | pk.data[9] | pk.data[10] | pk.data[11]) != 0;
}

static bool looksMeshCore(const Packet& pk) {
    if (pk.len < 2) return false;
    const uint8_t h = pk.data[0];
    if (h & 0xC0) return false;                 // payload version must be 0
    const uint8_t type = (h >> 2) & 0x0F;
    if (type > 0x0B && type != 0x0F) return false;
    const uint8_t route = h & 3;
    size_t off = 1 + ((route == 0 || route == 3) ? 4 : 0);
    if (off >= pk.len) return false;
    const uint8_t pl = pk.data[off];
    const uint8_t hashSize = (pl >> 6) + 1;
    const size_t pathBytes = (size_t)(pl & 0x3F) * hashSize;
    if (hashSize > 2 || pathBytes > 64) return false;
    return off + 1 + pathBytes < pk.len;
}

static bool looksFanet(const Packet& pk) {
    if (pk.len < 4) return false;
    const uint8_t type = pk.data[0] & 0x3F;
    return type <= 0x0A;
}

static bool looksLoRaWAN(const Packet& pk) {
    if (pk.len < 12 && !(pk.pflags & PF_IMPLICIT)) return false;
    const uint8_t mtype = pk.data[0] >> 5, major = pk.data[0] & 3;
    if (major != 0) return false;
    if (mtype == 6) return false;                     // RFU
    if (mtype == 0) return pk.len == 23;              // join-request is exactly 23 bytes
    if (mtype == 1) return pk.len == 17 || pk.len == 33;
    return pk.len >= 12;                              // MHDR + DevAddr + FCtrl + FCnt + MIC
}

static bool looksReticulum(const Packet& pk) {
    // RNode's 1-byte framing header: a sequence nibble above, bit 0 the
    // split flag, bits 1-3 clear. Then Reticulum's flags and a hops byte and
    // a 16-byte hash: at least 19 bytes.
    return pk.len >= 20 && (pk.data[0] & 0x0E) == 0;
}

void classify(Packet& pk) {
    pk.proto = Proto::UNKNOWN;
    pk.flags &= (uint8_t)~(PK_DECODED | PK_PLAINTEXT);
    if (pk.flags & PK_CRC_ERR) return;    // a broken frame names nobody

    switch (pk.sync) {
        case 0x34:
            if (looksLoRaWAN(pk)) pk.proto = Proto::LORAWAN;
            break;
        case 0x2B:
            if (looksMeshCom(pk)) pk.proto = Proto::MESHCOM;
            else if (looksMeshtastic(pk)) pk.proto = Proto::MESHTASTIC;
            break;
        case 0xF1:
            if (looksFanet(pk)) pk.proto = Proto::FANET;
            break;
        default:
            if (looksAprs(pk)) pk.proto = Proto::APRS;
            else if (looksMeshCore(pk)) pk.proto = Proto::MESHCORE;
            else if (looksReticulum(pk)) pk.proto = Proto::RETICULUM;
            break;
    }
    if (pk.proto != Proto::UNKNOWN) pk.flags |= PK_DECODED;
    if (pk.proto == Proto::APRS || pk.proto == Proto::MESHCOM || pk.proto == Proto::FANET)
        pk.flags |= PK_PLAINTEXT;
}

void summary(const Packet& pk, char* out, size_t cap) {
    if (!cap) return;
    if (!(pk.flags & PK_CRC_ERR)) {
        switch (pk.proto) {
            case Proto::MESHTASTIC: Meshtastic::summary(pk, out, cap); return;
            case Proto::MESHCORE:   MeshCore::summary(pk, out, cap); return;
            case Proto::LORAWAN:    LoRaWAN::summary(pk, out, cap); return;
            case Proto::APRS:       Aprs::summary(pk, out, cap); return;
            case Proto::MESHCOM:    MeshCom::summary(pk, out, cap); return;
            case Proto::FANET:      Fanet::summary(pk, out, cap); return;
            default: break;
        }
    }
    char hex[3 * 12 + 1];
    size_t o = 0;
    const uint8_t n = pk.len < 12 ? pk.len : 12;
    for (uint8_t i = 0; i < n; i++) o += (size_t)snprintf(hex + o, sizeof hex - o, "%02x ", pk.data[i]);
    if (o && hex[o - 1] == ' ') hex[o - 1] = '\0';
    snprintf(out, cap, "%-4s %3u B  %s%s", protoShort(pk.proto), (unsigned)pk.len, hex, pk.len > 12 ? " .." : "");
}

uint8_t hexDump(const Packet& pk, uint8_t perLine, char* out, size_t cap) {
    if (!cap) return 0;
    if (!perLine) perLine = 16;
    size_t o = 0;
    uint8_t rows = 0;
    for (uint8_t i = 0; i < pk.len; i += perLine) {
        rows++;
        if (o + 5 >= cap) break;
        o += (size_t)snprintf(out + o, cap - o, "%02x: ", (unsigned)i);
        for (uint8_t j = i; j < pk.len && j < (uint8_t)(i + perLine); j++) {
            if (o + 4 >= cap) break;
            o += (size_t)snprintf(out + o, cap - o, "%02x ", pk.data[j]);
        }
        if (o + 2 < cap) { out[o++] = '\n'; out[o] = '\0'; }
    }
    if (o && out[o - 1] == '\n') out[o - 1] = '\0';
    return rows;
}

}
