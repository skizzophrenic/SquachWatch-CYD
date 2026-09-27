// SquachWatch-CYD — LoRaWAN, off the air. See include/lora_lorawan.h.
// Layouts from TS001-1.0.4 §4-6, the DevAddr rule from TS002 §13, the
// registry from the LoRa Alliance NetID allocation R111 (2025-09), the OUIs
// from the IEEE registry by way of Wireshark's manuf file.
#include "lora_lorawan.h"
#include "lora_crypto.h"
// The NetID registry, generated from the published allocation rather than
// typed here: 143 rows against the 39 this file used to carry, at about 6 kB
// of flash, and no network call ever -- a DevAddr names its operator offline
// or not at all (docs/LORA.md section 8). tools/gen_netid.py says where each
// row came from, what it refused to emit, and how it cross-checks the
// NWKID_BITS table in lookupDevAddr() against the published prefix widths.
#include "lora_netid_table.h"
#include <stdio.h>
#include <string.h>

namespace LoRaWAN {

using namespace LoraCrypto;

const char* mtypeName(uint8_t m) {
    switch (m) {
        case JOIN_REQUEST: return "join-req";  case JOIN_ACCEPT: return "join-acc";
        case UNCONF_UP: return "up";           case UNCONF_DOWN: return "down";
        case CONF_UP: return "up conf";        case CONF_DOWN: return "down conf";
        case PROPRIETARY: return "proprietary"; default: return "rfu";
    }
}

bool parse(const uint8_t* d, uint8_t len, Frame& f) {
    memset(&f, 0, sizeof f);
    if (len < 12) return false;
    f.mtype = d[0] >> 5;
    f.major = d[0] & 3;
    if (f.major != 0 || f.mtype == RFU) return false;
    f.uplink = (f.mtype == JOIN_REQUEST || f.mtype == UNCONF_UP || f.mtype == CONF_UP);
    f.msg = d; f.msgLen = (uint8_t)(len - 4);
    f.mic = rd32be(d + len - 4);
    if (f.mtype == JOIN_REQUEST) {
        if (len != 23) return false;
        for (int i = 0; i < 8; i++) { f.joinEui[i] = d[1 + 7 - i]; f.devEui[i] = d[9 + 7 - i]; }
        f.devNonce = rd16le(d + 17);
        return true;
    }
    if (f.mtype == JOIN_ACCEPT) return len == 17 || len == 33;
    if (f.mtype == PROPRIETARY) { f.frm = d + 1; f.frmLen = (uint8_t)(len - 5); return true; }
    f.devAddr = rd32le(d + 1);
    const uint8_t fctrl = d[5];
    f.adr = (fctrl & 0x80) != 0;
    f.ack = (fctrl & 0x20) != 0;
    f.fOptsLen = fctrl & 0x0F;
    if (f.uplink) { f.adrAckReq = (fctrl & 0x40) != 0; f.classB = (fctrl & 0x10) != 0; }
    else          { f.fPending = (fctrl & 0x10) != 0; }
    f.fCnt = rd16le(d + 6);
    if (8 + f.fOptsLen > len - 4) return false;
    f.fOpts = d + 8;
    size_t off = 8 + f.fOptsLen;
    const size_t end = len - 4;
    if (off < end) {
        f.hasFPort = true;
        f.fPort = d[off++];
        f.frm = d + off;
        f.frmLen = (uint8_t)(end - off);
    }
    return true;
}

// ---- who runs it -------------------------------------------------------------
// REGISTRY[] is in the generated include/lora_netid_table.h; see the top of
// this file.
void lookupDevAddr(uint32_t a, NetInfo& out) {
    memset(&out, 0, sizeof out);
    out.netId = 0xFFFFFF;
    // The type is the run of leading ones, at most seven.
    uint8_t type = 0;
    while (type < 7 && (a & (0x80000000u >> type))) type++;
    static const uint8_t NWKID_BITS[8] = { 6, 6, 9, 11, 12, 13, 15, 17 };
    const uint8_t prefixLen = (uint8_t)(type + 1);
    const uint8_t nb = NWKID_BITS[type];
    out.type = type;
    out.nwkId = (uint32_t)(((uint64_t)a << prefixLen) & 0xFFFFFFFFull) >> (32 - nb);
    const uint32_t idMask = (nb >= 21) ? 0x1FFFFF : ((1u << nb) - 1);
    uint8_t matches = 0;
    for (size_t i = 0; i < sizeof REGISTRY / sizeof REGISTRY[0]; i++) {
        const uint32_t n = REGISTRY[i].netId;
        if ((n >> 21) != type) continue;
        if ((n & idMask) != out.nwkId) continue;
        if (!matches) { out.netId = n; out.op = REGISTRY[i].op; }
        matches++;
    }
    // Types 3-7 hold a 21-bit ID but only nb bits of it reach the air.
    out.ambiguous = type >= 3;
}

struct OuiEntry { uint8_t nibbles; uint8_t b[5]; const char* maker; };
static const OuiEntry OUIS[] = {
    { 9,  { 0x70, 0xB3, 0xD5, 0x7E, 0xD0 }, "TTN Foundation" },
    { 9,  { 0x70, 0xB3, 0xD5, 0x7B, 0xA0 }, "Decentlab" },
    { 9,  { 0x70, 0xB3, 0xD5, 0x71, 0xB0 }, "Elsys" },
    // MA-S, not MA-M: oui36.csv holds 4,092 assignments under 70B3D5 across
    // 3,324 organisations and mam.csv holds none (both counted 2026-09-26).
    // The nine-nibble rows above are three of those 4,092; this row is the
    // pool itself, for the other 4,089 that are not worth 85 kB of table.
    { 6,  { 0x70, 0xB3, 0xD5 }, "IEEE MA-S pool (a small LoRa vendor)" },
    { 6,  { 0x00, 0x16, 0xC0 }, "Semtech" },
    { 6,  { 0x00, 0x08, 0x00 }, "Multitech" },
    { 6,  { 0x00, 0x80, 0x00 }, "Multitech" },
    { 6,  { 0x70, 0x76, 0xFF }, "Kerlink" },
    { 6,  { 0x60, 0x81, 0xF9 }, "Helium Systems" },
    { 6,  { 0xA8, 0x40, 0x41 }, "Dragino" },
    { 6,  { 0xAC, 0x1F, 0x09 }, "RAKwireless" },
    { 6,  { 0x00, 0x16, 0x16 }, "Browan" },
    { 6,  { 0x24, 0xE1, 0x24 }, "Milesight" },
    { 6,  { 0x1C, 0xC3, 0x16 }, "Milesight" },
    { 6,  { 0x64, 0x7F, 0xDA }, "Tektelic" },
    { 6,  { 0x18, 0xA9, 0xA6 }, "Nebra" },
    { 6,  { 0x20, 0x63, 0x5F }, "Abeeway" },
    { 6,  { 0x00, 0x23, 0x3D }, "Laird" },
    { 6,  { 0x04, 0xB6, 0x48 }, "Zenner" },
    { 7,  { 0xA4, 0xDA, 0x22, 0x40 }, "Loriot" },
    { 6,  { 0x2C, 0xF7, 0xF1 }, "Seeed" },
    { 7,  { 0x78, 0x72, 0x64, 0xE0 }, "Heltec" },
    { 6,  { 0x58, 0xA0, 0xCB }, "TrackNet" },
    { 6,  { 0x00, 0x18, 0xB2 }, "Adeunis" },
    { 9,  { 0x00, 0x50, 0xC2, 0x86, 0x20 }, "Elsys" },
};

const char* ouiMaker(const uint8_t eui[8]) {
    for (size_t i = 0; i < sizeof OUIS / sizeof OUIS[0]; i++) {
        const OuiEntry& e = OUIS[i];
        bool ok = true;
        for (uint8_t n = 0; n < e.nibbles && ok; n++) {
            const uint8_t have = (n & 1) ? (eui[n / 2] & 0x0F) : (eui[n / 2] >> 4);
            const uint8_t want = (n & 1) ? (e.b[n / 2] & 0x0F) : (e.b[n / 2] >> 4);
            ok = have == want;
        }
        if (ok) return e.maker;
    }
    return nullptr;
}

const char* joinServerName(const uint8_t j[8]) {
    static const uint8_t TTN[8]  = { 0x70, 0xB3, 0xD5, 0x7E, 0xD0, 0x00, 0x00, 0x00 };
    static const uint8_t SEM[8]  = { 0x00, 0x16, 0xC0, 0x01, 0xFF, 0xFE, 0x00, 0x01 };
    static const uint8_t MIL[8]  = { 0x24, 0xE1, 0x24, 0xC0, 0x00, 0x2A, 0x00, 0x01 };
    static const uint8_t ZERO[8] = { 0 };
    if (!memcmp(j, TTN, 8))  return "The Things Join Server";
    if (!memcmp(j, SEM, 8))  return "Semtech LoRa Cloud (closed 2025)";
    if (!memcmp(j, MIL, 8))  return "Milesight default";
    if (!memcmp(j, ZERO, 8)) return "all zeros";
    if (!memcmp(j, TTN, 5))  return "The Things Industries";
    return nullptr;
}

void euiText(const uint8_t eui[8], char out[17]) { toHex(eui, 8, out); }

// ---- MAC commands ------------------------------------------------------------
struct MacDef { uint8_t cid; uint8_t upLen, downLen; const char* upName; const char* downName; };
static const MacDef MACS[] = {
    { 0x01, 1, 1, "ResetInd",         "ResetConf" },
    { 0x02, 0, 2, "LinkCheckReq",     "LinkCheckAns" },
    { 0x03, 1, 4, "LinkADRAns",       "LinkADRReq" },
    { 0x04, 0, 1, "DutyCycleAns",     "DutyCycleReq" },
    { 0x05, 1, 4, "RXParamSetupAns",  "RXParamSetupReq" },
    { 0x06, 2, 0, "DevStatusAns",     "DevStatusReq" },
    { 0x07, 1, 5, "NewChannelAns",    "NewChannelReq" },
    { 0x08, 0, 1, "RXTimingSetupAns", "RXTimingSetupReq" },
    { 0x09, 0, 1, "TxParamSetupAns",  "TxParamSetupReq" },
    { 0x0A, 1, 4, "DlChannelAns",     "DlChannelReq" },
    { 0x0B, 1, 1, "RekeyInd",         "RekeyConf" },
    { 0x0C, 0, 1, "ADRParamSetupAns", "ADRParamSetupReq" },
    { 0x0D, 0, 5, "DeviceTimeReq",    "DeviceTimeAns" },
    { 0x0E, 0, 2, "-",                "ForceRejoinReq" },
    { 0x0F, 1, 1, "RejoinParamSetupAns", "RejoinParamSetupReq" },
    { 0x10, 1, 0, "PingSlotInfoReq",  "PingSlotInfoAns" },
    { 0x11, 1, 4, "PingSlotChannelAns", "PingSlotChannelReq" },
    { 0x12, 0, 3, "BeaconTimingReq",  "BeaconTimingAns" },
    { 0x13, 1, 3, "BeaconFreqAns",    "BeaconFreqReq" },
    { 0x20, 1, 1, "DeviceModeInd",    "DeviceModeConf" },
};

static const MacDef* macDef(uint8_t cid) {
    for (size_t i = 0; i < sizeof MACS / sizeof MACS[0]; i++) if (MACS[i].cid == cid) return &MACS[i];
    return nullptr;
}

const char* macCmdName(uint8_t cid, bool uplink) {
    const MacDef* d = macDef(cid);
    if (!d) return "MAC ?";
    return uplink ? d->upName : d->downName;
}

uint8_t parseMacCommands(const uint8_t* p, uint8_t len, bool uplink, MacCmd* out, uint8_t cap) {
    uint8_t n = 0, off = 0;
    while (off < len && n < cap) {
        const MacDef* d = macDef(p[off]);
        if (!d) break;
        const uint8_t plen = uplink ? d->upLen : d->downLen;
        if (off + 1 + plen > len) break;
        out[n].cid = p[off]; out[n].payload = p + off + 1; out[n].len = plen;
        n++; off = (uint8_t)(off + 1 + plen);
    }
    return n;
}

void macCmdText(const MacCmd& c, bool uplink, char* out, size_t cap) {
    const char* name = macCmdName(c.cid, uplink);
    const uint8_t* p = c.payload;
    switch (c.cid) {
        case 0x02:
            if (!uplink) snprintf(out, cap, "%s margin %u dB, %u gateways", name, p[0], p[1]);
            else snprintf(out, cap, "%s", name);
            return;
        case 0x03:
            if (!uplink) snprintf(out, cap, "%s DR%u TX%u mask %04x nb%u", name, p[0] >> 4, p[0] & 15, rd16le(p + 1), p[3] & 15);
            else snprintf(out, cap, "%s %s%s%s", name, (p[0] & 4) ? "pwr " : "", (p[0] & 2) ? "dr " : "", (p[0] & 1) ? "mask" : "");
            return;
        case 0x04:
            if (!uplink) snprintf(out, cap, "%s 1/%u", name, 1u << (p[0] & 15)); else snprintf(out, cap, "%s", name);
            return;
        case 0x05:
            if (!uplink) snprintf(out, cap, "%s rx1off %u rx2 DR%u %lu.%03lu MHz", name, (p[0] >> 4) & 7, p[0] & 15,
                                  (unsigned long)(rd24le(p + 1) / 10000), (unsigned long)((rd24le(p + 1) / 10) % 1000));
            else snprintf(out, cap, "%s", name);
            return;
        case 0x06:
            if (uplink) {
                const int8_t margin = (int8_t)((p[1] & 0x20) ? (p[1] | 0xC0) : (p[1] & 0x3F));
                if (p[0] == 0) snprintf(out, cap, "%s external power, snr %d dB", name, margin);
                else if (p[0] == 255) snprintf(out, cap, "%s battery ?, snr %d dB", name, margin);
                else snprintf(out, cap, "%s battery %u%%, snr %d dB", name, (unsigned)(p[0] * 100 / 254), margin);
            } else snprintf(out, cap, "%s", name);
            return;
        case 0x07:
            if (!uplink) snprintf(out, cap, "%s ch%u %lu.%03lu MHz DR%u-%u", name, p[0],
                                  (unsigned long)(rd24le(p + 1) / 10000), (unsigned long)((rd24le(p + 1) / 10) % 1000), p[4] & 15, p[4] >> 4);
            else snprintf(out, cap, "%s", name);
            return;
        case 0x08:
            if (!uplink) snprintf(out, cap, "%s rx1 %u s", name, (p[0] & 15) ? (p[0] & 15) : 1); else snprintf(out, cap, "%s", name);
            return;
        case 0x0D:
            if (!uplink) snprintf(out, cap, "%s gps %lu s", name, (unsigned long)rd32le(p)); else snprintf(out, cap, "%s", name);
            return;
        default:
            snprintf(out, cap, "%s", name);
            return;
    }
}

// ---- with the keys ----------------------------------------------------------
static const uint8_t MAX_SESSIONS = 6;
static Session s_sessions[MAX_SESSIONS];
static uint8_t s_nSessions = 0;

uint8_t        sessionCount() { return s_nSessions; }
const Session& session(uint8_t i) { return s_sessions[i < s_nSessions ? i : 0]; }
void           clearSessions() { s_nSessions = 0; }

bool addSession(const char* name, uint32_t devAddr, const char* nwkHex, const char* appHex, uint8_t codec) {
    if (s_nSessions >= MAX_SESSIONS) return false;
    Session& s = s_sessions[s_nSessions];
    memset(&s, 0, sizeof s);
    strncpy(s.name, name ? name : "", sizeof s.name - 1);
    s.devAddr = devAddr;
    s.codec = codec;
    if (fromHex(nwkHex, s.nwkSKey, 16) != 16) return false;
    if (fromHex(appHex, s.appSKey, 16) != 16) return false;
    s_nSessions++;
    return true;
}

static void b0(const Frame& f, uint16_t fCntMsb, uint8_t first, uint8_t block[16], uint8_t last) {
    memset(block, 0, 16);
    block[0] = first;
    block[5] = f.uplink ? 0 : 1;
    wr32le(block + 6, f.devAddr);
    wr32le(block + 10, ((uint32_t)fCntMsb << 16) | f.fCnt);
    block[15] = last;
}

bool verifyMic(const Frame& f, const uint8_t nwkSKey[16], uint16_t fCntMsb) {
    if (f.mtype < UNCONF_UP || f.mtype > CONF_DOWN) return false;
    uint8_t buf[16 + 255];
    b0(f, fCntMsb, 0x49, buf, f.msgLen);
    memcpy(buf + 16, f.msg, f.msgLen);
    Aes128 k; k.setKey(nwkSKey);
    uint8_t mac[16];
    cmac(k, buf, 16 + f.msgLen, mac);
    return rd32be(mac) == f.mic;
}

void decryptFrm(const Frame& f, const uint8_t key[16], uint16_t fCntMsb, uint8_t* out) {
    Aes128 k; k.setKey(key);
    uint8_t a[16], s[16];
    for (uint8_t i = 0; i * 16 < f.frmLen; i++) {
        b0(f, fCntMsb, 0x01, a, (uint8_t)(i + 1));
        k.encryptBlock(a, s);
        for (uint8_t j = 0; j < 16 && i * 16 + j < f.frmLen; j++) out[i * 16 + j] = f.frm[i * 16 + j] ^ s[j];
    }
}

bool openJoinAccept(const uint8_t* d, uint8_t len, const uint8_t appKey[16], JoinAccept& out) {
    memset(&out, 0, sizeof out);
    if ((len != 17 && len != 33) || (d[0] >> 5) != JOIN_ACCEPT) return false;
    // The network encrypted with AES *decrypt* so the device can open it
    // with the one AES operation it has: encrypt. Everything after MHDR,
    // MIC included.
    Aes128 k; k.setKey(appKey);
    uint8_t plain[33];
    plain[0] = d[0];
    for (uint8_t off = 1; off < len; off = (uint8_t)(off + 16)) k.encryptBlock(d + off, plain + off);
    out.joinNonce = rd24le(plain + 1);
    out.netId     = rd24le(plain + 4);
    out.devAddr   = rd32le(plain + 7);
    out.rx1DrOffset = (plain[11] >> 4) & 7;
    out.rx2Dr     = plain[11] & 15;
    out.rxDelay   = plain[12];
    if (len == 33) { out.hasCfList = true; memcpy(out.cfList, plain + 13, 16); }
    uint8_t mac[16];
    cmac(k, plain, (size_t)(len - 4), mac);
    out.micOk = memcmp(mac, plain + len - 4, 4) == 0;
    return true;
}

void deriveSession(const uint8_t appKey[16], uint32_t joinNonce, uint32_t netId, uint16_t devNonce,
                   uint8_t nwkSKey[16], uint8_t appSKey[16]) {
    Aes128 k; k.setKey(appKey);
    uint8_t block[16] = {0};
    block[1] = (uint8_t)joinNonce; block[2] = (uint8_t)(joinNonce >> 8); block[3] = (uint8_t)(joinNonce >> 16);
    block[4] = (uint8_t)netId; block[5] = (uint8_t)(netId >> 8); block[6] = (uint8_t)(netId >> 16);
    wr16le(block + 7, devNonce);
    block[0] = 0x01; k.encryptBlock(block, nwkSKey);
    block[0] = 0x02; k.encryptBlock(block, appSKey);
}

// ---- Class B beacon ------------------------------------------------------------
bool parseBeacon(const uint8_t* d, uint8_t len, Beacon& out) {
    memset(&out, 0, sizeof out);
    if (len != 17) return false;
    out.param   = d[1];
    out.gpsTime = rd32le(d + 2);
    out.crc1Ok  = crc16ccitt(d, 6, 0x0000) == rd16le(d + 6);
    out.infoDesc = d[8];
    out.crc2Ok  = crc16ccitt(d + 8, 7, 0x0000) == rd16le(d + 15);
    if (out.infoDesc <= 2) {
        int32_t lat = (int32_t)rd24le(d + 9), lon = (int32_t)rd24le(d + 12);
        if (lat & 0x800000) lat -= 0x1000000;
        if (lon & 0x800000) lon -= 0x1000000;
        // +-2^23 is +-90 degrees of latitude and +-180 of longitude.
        out.latE7 = (int32_t)(((int64_t)lat * 900000000LL) / 8388608LL);
        out.lonE7 = (int32_t)(((int64_t)lon * 1800000000LL) / 8388608LL);
    } else if (out.infoDesc == 3) {
        out.netId = rd24le(d + 9);
        out.gwId  = rd24le(d + 12);
    }
    return true;
}

// ---- Cayenne LPP -------------------------------------------------------------
uint8_t cayenneText(const uint8_t* p, uint8_t len, char* out, size_t cap) {
    size_t o = 0; uint8_t n = 0, off = 0;
    if (cap) out[0] = '\0';
    while (off + 2 <= len && o + 8 < cap) {
        const uint8_t ch = p[off], t = p[off + 1];
        const uint8_t* v = p + off + 2;
        uint8_t used = 0;
        char item[48];
        switch (t) {
            case 0x00: case 0x01: if (off + 3 > len) return n; used = 1; snprintf(item, sizeof item, "%u dig %u", ch, v[0]); break;
            case 0x02: case 0x03: if (off + 4 > len) return n; used = 2; snprintf(item, sizeof item, "%u ana %.2f", ch, (int16_t)rd16be(v) / 100.0); break;
            case 0x65: if (off + 4 > len) return n; used = 2; snprintf(item, sizeof item, "%u lux %u", ch, rd16be(v)); break;
            case 0x66: if (off + 3 > len) return n; used = 1; snprintf(item, sizeof item, "%u presence %u", ch, v[0]); break;
            case 0x67: if (off + 4 > len) return n; used = 2; snprintf(item, sizeof item, "%u temp %.1fC", ch, (int16_t)rd16be(v) / 10.0); break;
            case 0x68: if (off + 3 > len) return n; used = 1; snprintf(item, sizeof item, "%u hum %.1f%%", ch, v[0] / 2.0); break;
            case 0x71: if (off + 8 > len) return n; used = 6; snprintf(item, sizeof item, "%u acc %.2f,%.2f,%.2f", ch, (int16_t)rd16be(v) / 1000.0, (int16_t)rd16be(v + 2) / 1000.0, (int16_t)rd16be(v + 4) / 1000.0); break;
            case 0x73: if (off + 4 > len) return n; used = 2; snprintf(item, sizeof item, "%u %.1fhPa", ch, rd16be(v) / 10.0); break;
            case 0x86: if (off + 8 > len) return n; used = 6; snprintf(item, sizeof item, "%u gyro", ch); break;
            case 0x88: {
                if (off + 11 > len) return n;
                used = 9;
                int32_t lat = (int32_t)rd24be(v), lon = (int32_t)rd24be(v + 3), alt = (int32_t)rd24be(v + 6);
                if (lat & 0x800000) lat -= 0x1000000;
                if (lon & 0x800000) lon -= 0x1000000;
                if (alt & 0x800000) alt -= 0x1000000;
                snprintf(item, sizeof item, "%u gps %.4f,%.4f %ldm", ch, lat / 10000.0, lon / 10000.0, (long)(alt / 100));
                break;
            }
            default: return n;   // not LPP after all
        }
        o += (size_t)snprintf(out + o, cap - o, "%s%s", n ? "  " : "", item);
        n++;
        off = (uint8_t)(off + 2 + used);
    }
    return n;
}

// True when cayenneText() would read every byte: a payload that stops
// mid-item is not LPP.
bool cayenneConsumed(const uint8_t* p, uint8_t len) {
    uint8_t off = 0;
    while (off + 2 <= len) {
        uint8_t used;
        switch (p[off + 1]) {
            case 0x00: case 0x01: case 0x66: case 0x68: used = 1; break;
            case 0x02: case 0x03: case 0x65: case 0x67: case 0x73: used = 2; break;
            case 0x71: case 0x86: used = 6; break;
            case 0x88: used = 9; break;
            default: return false;
        }
        off = (uint8_t)(off + 2 + used);
    }
    return off == len;
}

// ---- all of it -----------------------------------------------------------------
bool decode(const Lora::Packet& pk, Decoded& out) {
    memset(&out, 0, sizeof out);
    if (pk.flags & Lora::PK_IMPLICIT) {
        out.isBeacon = parseBeacon(pk.data, pk.len, out.beacon);
        return out.isBeacon;
    }
    if (!parse(pk.data, pk.len, out.f)) return false;
    const Frame& f = out.f;
    if (f.mtype == JOIN_REQUEST) {
        out.maker = ouiMaker(f.devEui);
        out.joinServer = joinServerName(f.joinEui);
        return true;
    }
    if (f.mtype == JOIN_ACCEPT || f.mtype == PROPRIETARY) return true;
    lookupDevAddr(f.devAddr, out.net);
    if (f.fOptsLen) out.nMac = parseMacCommands(f.fOpts, f.fOptsLen, f.uplink, out.mac, 8);
    for (uint8_t i = 0; i < s_nSessions; i++) {
        const Session& s = s_sessions[i];
        if (s.devAddr != f.devAddr) continue;
        out.haveKeys = true;
        out.codec = s.codec;
        // The counter's upper half is not on the air; the low one, then
        // the next few, is how a long-running device is followed.
        for (uint16_t msb = 0; msb < 4 && !out.micOk; msb++) {
            if (!verifyMic(f, s.nwkSKey, msb)) continue;
            out.micOk = true;
            if (f.hasFPort && f.frmLen) {
                decryptFrm(f, f.fPort == 0 ? s.nwkSKey : s.appSKey, msb, out.plain);
                out.plainLen = f.frmLen;
                if (f.fPort == 0 && !out.nMac) out.nMac = parseMacCommands(out.plain, out.plainLen, f.uplink, out.mac, 8);
            }
        }
        break;
    }
    return true;
}

// The opened payload as the session's codec says, or as hex.
void payloadText(const Decoded& d, char* out, size_t cap) {
    if (!cap) return;
    out[0] = '\0';
    if (!d.plainLen) return;
    uint8_t codec = d.codec;
    if (codec == CODEC_AUTO) {
        // Cayenne only when it reads the whole payload as at least two
        // channels; anything shorter is as likely to be four letters that
        // happen to look like one.
        char probe[96];
        codec = (cayenneText(d.plain, d.plainLen, probe, sizeof probe) >= 2 && cayenneConsumed(d.plain, d.plainLen)) ? CODEC_CAYENNE : CODEC_HEX;
    }
    if (codec == CODEC_CAYENNE && cayenneText(d.plain, d.plainLen, out, cap)) return;
    if (codec == CODEC_TEXT) {
        size_t n = d.plainLen < cap - 1 ? d.plainLen : cap - 1;
        for (size_t i = 0; i < n; i++) out[i] = (d.plain[i] >= 0x20 && d.plain[i] < 0x7F) ? (char)d.plain[i] : '.';
        out[n] = '\0';
        return;
    }
    const uint8_t n = d.plainLen < 24 ? d.plainLen : 24;
    if (cap < 2u * n + 3) { out[0] = '\0'; return; }
    toHex(d.plain, n, out);
    if (d.plainLen > 24) strncat(out, "..", cap - strlen(out) - 1);
}

void summary(const Lora::Packet& pk, char* out, size_t cap) {
    Decoded d;
    if (!decode(pk, d)) { snprintf(out, cap, "WAN bad frame"); return; }
    if (d.isBeacon) {
        if (d.beacon.infoDesc <= 2)
            snprintf(out, cap, "beacon gps %lu %s gw %.4f,%.4f", (unsigned long)d.beacon.gpsTime,
                     d.beacon.crc1Ok && d.beacon.crc2Ok ? "ok" : "crc?", d.beacon.latE7 / 1e7, d.beacon.lonE7 / 1e7);
        else
            snprintf(out, cap, "beacon gps %lu %s net %06lx gw %06lx", (unsigned long)d.beacon.gpsTime,
                     d.beacon.crc1Ok && d.beacon.crc2Ok ? "ok" : "crc?", (unsigned long)d.beacon.netId, (unsigned long)d.beacon.gwId);
        return;
    }
    const Frame& f = d.f;
    if (f.mtype == JOIN_REQUEST) {
        char je[17], de[17];
        euiText(f.joinEui, je); euiText(f.devEui, de);
        snprintf(out, cap, "join-req dev %s %s join %s %s nonce %04x", de, d.maker ? d.maker : "?", je,
                 d.joinServer ? d.joinServer : "", (unsigned)f.devNonce);
        return;
    }
    if (f.mtype == JOIN_ACCEPT) { snprintf(out, cap, "join-accept %u B (encrypted)", (unsigned)pk.len); return; }
    if (f.mtype == PROPRIETARY) { snprintf(out, cap, "proprietary %u B%s", (unsigned)pk.len, pk.len == 56 ? " (Helium PoC beacon?)" : ""); return; }
    size_t o = (size_t)snprintf(out, cap, "%s %08lx %s fcnt %u", mtypeName(f.mtype), (unsigned long)f.devAddr,
                                d.net.op ? d.net.op : "?", (unsigned)f.fCnt);
    if (o >= cap) return;
    if (f.hasFPort) o += (size_t)snprintf(out + o, cap - o, " port %u %uB", (unsigned)f.fPort, (unsigned)f.frmLen);
    if (o < cap && f.adr) o += (size_t)snprintf(out + o, cap - o, " adr");
    if (o < cap && f.adrAckReq) o += (size_t)snprintf(out + o, cap - o, " adrack");
    if (o < cap && f.ack) o += (size_t)snprintf(out + o, cap - o, " ack");
    if (o < cap && f.fPending) o += (size_t)snprintf(out + o, cap - o, " pending");
    if (o < cap && f.classB) o += (size_t)snprintf(out + o, cap - o, " classB");
    for (uint8_t i = 0; i < d.nMac && o + 4 < cap; i++) {
        char m[64];
        macCmdText(d.mac[i], f.uplink, m, sizeof m);
        o += (size_t)snprintf(out + o, cap - o, " [%s]", m);
    }
    if (d.haveKeys && o + 4 < cap) {
        if (!d.micOk) { snprintf(out + o, cap - o, " MIC BAD"); return; }
        char body[96];
        payloadText(d, body, sizeof body);
        snprintf(out + o, cap - o, " mic ok %s", body);
    }
}

}
