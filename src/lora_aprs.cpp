// SquachWatch-CYD — LoRa APRS and MeshCom. See include/lora_aprs.h.
// APRS formats from APRS 1.01 (chapters 6, 8, 9, 10, 14); the LoRa framing
// from richonguzman/LoRa_APRS_iGate; MeshCom's from icssw-org's
// aprs_functions.cpp (v4.35).
#include "lora_aprs.h"
#include "lora_crypto.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

namespace Aprs {

static bool isDigits(const char* p, size_t n) {
    for (size_t i = 0; i < n; i++) if (p[i] < '0' || p[i] > '9') return false;
    return true;
}

bool parseUncompressed(const char* p, size_t len, int32_t& latE7, int32_t& lonE7, char& table, char& code) {
    // DDMM.mmN T DDDMM.mmE C  -- 19 characters
    if (len < 19) return false;
    if (p[4] != '.' || p[14] != '.') return false;
    if (p[7] != 'N' && p[7] != 'S') return false;
    if (p[17] != 'E' && p[17] != 'W') return false;
    // Ambiguity replaces trailing digits with spaces; read what is there.
    char buf[10];
    memcpy(buf, p, 7); buf[7] = '\0';
    for (int i = 0; i < 7; i++) if (buf[i] == ' ') buf[i] = '0';
    const int latDeg = (buf[0] - '0') * 10 + (buf[1] - '0');
    const int latMin100 = (buf[2] - '0') * 1000 + (buf[3] - '0') * 100 + (buf[5] - '0') * 10 + (buf[6] - '0');
    memcpy(buf, p + 9, 8); buf[8] = '\0';
    for (int i = 0; i < 8; i++) if (buf[i] == ' ') buf[i] = '0';
    const int lonDeg = (buf[0] - '0') * 100 + (buf[1] - '0') * 10 + (buf[2] - '0');
    const int lonMin100 = (buf[3] - '0') * 1000 + (buf[4] - '0') * 100 + (buf[6] - '0') * 10 + (buf[7] - '0');
    if (latDeg > 90 || lonDeg > 180 || latMin100 >= 6000 || lonMin100 >= 6000) return false;
    // degrees*1e7 + minutes/100 * 1e7/60
    int64_t lat = (int64_t)latDeg * 10000000LL + ((int64_t)latMin100 * 10000000LL) / 6000LL;
    int64_t lon = (int64_t)lonDeg * 10000000LL + ((int64_t)lonMin100 * 10000000LL) / 6000LL;
    if (p[7] == 'S') lat = -lat;
    if (p[17] == 'W') lon = -lon;
    latE7 = (int32_t)lat; lonE7 = (int32_t)lon;
    table = p[8]; code = p[18];
    return true;
}

// The compressed form: table, then YYYY XXXX in base 91, symbol, cs, T.
static bool parseCompressed(const char* p, size_t len, Info& out) {
    if (len < 13) return false;
    for (int i = 1; i < 9; i++) if (p[i] < '!' || p[i] > '{') return false;
    double y = 0, x = 0;
    for (int i = 1; i <= 4; i++) y = y * 91 + (p[i] - 33);
    for (int i = 5; i <= 8; i++) x = x * 91 + (p[i] - 33);
    const double lat = 90.0 - y / 380926.0;
    const double lon = -180.0 + x / 190463.0;
    out.hasPos = true;
    out.latE7 = (int32_t)(lat * 1e7); out.lonE7 = (int32_t)(lon * 1e7);
    out.symTable = p[0]; out.symCode = p[9];
    // cs: course/speed when T says so (bits 4-3 of T-33 == 0 for GGA... keep to the common case)
    if (p[10] != ' ' && p[12] != ' ') {
        const int t = p[12] - 33;
        const int c = p[10] - 33, s = p[11] - 33;
        if (((t >> 3) & 3) != 2 && c >= 0 && c <= 89) {     // not an altitude
            out.hasCourseSpeed = true;
            out.course = (uint16_t)(c * 4);
            // speed = 1.08^s - 1 knots
            double kn = 1.0;
            for (int i = 0; i < s; i++) kn *= 1.08;
            out.speedKmh = (uint16_t)((kn - 1.0) * 1.852 + 0.5);
        }
    }
    return true;
}

// Mic-E: the latitude, the message bits and the longitude offsets sit in the
// destination; the longitude, course and speed in the first bytes of the
// information field. APRS 1.01 chapter 10.
static bool parseMicE(const char* info, size_t len, const char* dest, Info& out) {
    if (!dest || strlen(dest) < 6 || len < 9) return false;
    int digits[6]; bool north = false, west = false; int lonOff = 0;
    for (int i = 0; i < 6; i++) {
        const char c = dest[i];
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'A' && c <= 'J') d = c - 'A';
        else if (c >= 'P' && c <= 'Y') d = c - 'P';
        else if (c == 'K' || c == 'L' || c == 'Z') { d = 0; out.ambiguous = true; }
        else return false;
        digits[i] = d;
        if (i == 3) north = (c >= 'P');
        if (i == 4) lonOff = (c >= 'P') ? 100 : 0;
        if (i == 5) west = (c >= 'P');
    }
    const int latDeg = digits[0] * 10 + digits[1];
    const int latMin100 = digits[2] * 1000 + digits[3] * 100 + digits[4] * 10 + digits[5];
    int d = (uint8_t)info[1] - 28 + lonOff;
    if (d >= 180 && d <= 189) d -= 80;
    else if (d >= 190 && d <= 199) d -= 190;
    int m = (uint8_t)info[2] - 28;
    if (m >= 60) m -= 60;
    const int h = (uint8_t)info[3] - 28;
    if (latDeg > 90 || d > 180 || m > 59 || h > 99) return false;
    int64_t lat = (int64_t)latDeg * 10000000LL + ((int64_t)latMin100 * 10000000LL) / 6000LL;
    int64_t lon = (int64_t)d * 10000000LL + ((int64_t)(m * 100 + h) * 10000000LL) / 6000LL;
    if (!north) lat = -lat;
    if (west) lon = -lon;
    out.hasPos = true; out.latE7 = (int32_t)lat; out.lonE7 = (int32_t)lon;
    int sp = ((uint8_t)info[4] - 28) * 10;
    const int dc = (uint8_t)info[5] - 28;
    sp += dc / 10;
    int course = (dc % 10) * 100 + ((uint8_t)info[6] - 28);
    if (sp >= 800) sp -= 800;
    if (course >= 400) course -= 400;
    out.hasCourseSpeed = true;
    out.speedKmh = (uint16_t)(sp * 1.852 + 0.5);
    out.course = (uint16_t)course;
    out.symCode = info[7]; out.symTable = info[8];
    size_t off = 9;
    // An altitude rides as "xxx}" in base 91, metres above -10000.
    if (len >= off + 4 && info[off + 3] == '}') {
        const int32_t a = ((info[off] - 33) * 91 + (info[off + 1] - 33)) * 91 + (info[off + 2] - 33);
        out.hasAlt = true; out.altM = a - 10000;
        off += 4;
    }
    const size_t n = len > off ? len - off : 0;
    const size_t m2 = n < sizeof out.text - 1 ? n : sizeof out.text - 1;
    memcpy(out.text, info + off, m2); out.text[m2] = '\0';
    return true;
}

// The comment after a position may carry "/A=nnnnnn" feet.
static void commentAltitude(const char* s, Info& out) {
    const char* a = strstr(s, "/A=");
    if (a && strlen(a) >= 9 && isDigits(a + 3, 6)) {
        out.hasAlt = true;
        out.altM = (int32_t)(atol(a + 3) * 3048 / 10000);
    }
}

bool parseInfo(const char* info, size_t len, const char* dest, Info& out) {
    memset(&out, 0, sizeof out);
    if (!len) return false;
    const char t = info[0];
    size_t off = 1;
    switch (t) {
        case '`': case '\'': case 0x1C: case 0x1D:
            out.kind = INFO_MICE;
            return parseMicE(info, len, dest, out);
        case '/': case '@':
            out.kind = INFO_POSITION_TS;
            if (len < 8) return false;
            off = 8;   // DDHHMMz or HHMMSSh
            break;
        case '!': case '=':
            out.kind = INFO_POSITION;
            break;
        case ':': {
            out.kind = INFO_MESSAGE;
            if (len < 11 || info[10] != ':') return false;
            memcpy(out.addressee, info + 1, 9); out.addressee[9] = '\0';
            for (int i = 8; i >= 0 && out.addressee[i] == ' '; i--) out.addressee[i] = '\0';
            const size_t n = len - 11 < sizeof out.text - 1 ? len - 11 : sizeof out.text - 1;
            memcpy(out.text, info + 11, n); out.text[n] = '\0';
            return true;
        }
        case '>': {
            out.kind = INFO_STATUS;
            const size_t n = len - 1 < sizeof out.text - 1 ? len - 1 : sizeof out.text - 1;
            memcpy(out.text, info + 1, n); out.text[n] = '\0';
            return true;
        }
        case ';': out.kind = INFO_OBJECT; off = 1 + 9 + 1 + 7; break;   // name, live/killed, timestamp
        case ')': out.kind = INFO_ITEM; break;
        case 'T': out.kind = INFO_TELEMETRY; return true;
        default: {
            out.kind = INFO_OTHER;
            const size_t n = len < sizeof out.text - 1 ? len : sizeof out.text - 1;
            memcpy(out.text, info, n); out.text[n] = '\0';
            return true;
        }
    }
    if (off >= len) return false;
    const char* p = info + off;
    const size_t n = len - off;
    size_t used = 0;
    if (p[0] >= '0' && p[0] <= '9') {
        if (!parseUncompressed(p, n, out.latE7, out.lonE7, out.symTable, out.symCode)) return false;
        out.hasPos = true; used = 19;
        // Course/speed "ccc/sss" may follow
        if (n >= used + 7 && p[used + 3] == '/' && isDigits(p + used, 3) && isDigits(p + used + 4, 3)) {
            out.hasCourseSpeed = true;
            out.course = (uint16_t)atoi(p + used);
            out.speedKmh = (uint16_t)(atoi(p + used + 4) * 1.852 + 0.5);
            used += 7;
        }
    } else {
        if (!parseCompressed(p, n, out)) return false;
        used = 13;
    }
    const size_t cn = n > used ? n - used : 0;
    const size_t m = cn < sizeof out.text - 1 ? cn : sizeof out.text - 1;
    memcpy(out.text, p + used, m); out.text[m] = '\0';
    commentAltitude(out.text, out);
    return true;
}

// ---- LoRa APRS ---------------------------------------------------------------
bool parse(const uint8_t* d, uint8_t len, Frame& f) {
    memset(&f, 0, sizeof f);
    if (len < 4 || d[0] != 0x3C || d[1] != 0xFF || d[2] != 0x01) return false;
    const char* s = (const char*)d + 3;
    const size_t n = len - 3;
    const char* gt = (const char*)memchr(s, '>', n);
    if (!gt || gt == s || (size_t)(gt - s) > 9) return false;
    memcpy(f.src, s, (size_t)(gt - s)); f.src[gt - s] = '\0';
    const char* colon = (const char*)memchr(gt + 1, ':', n - (size_t)(gt + 1 - s));
    if (!colon) return false;
    // dest[,path]
    const char* comma = (const char*)memchr(gt + 1, ',', (size_t)(colon - gt - 1));
    const char* destEnd = comma ? comma : colon;
    if (destEnd - gt - 1 > 9 || destEnd == gt + 1) return false;
    memcpy(f.dest, gt + 1, (size_t)(destEnd - gt - 1)); f.dest[destEnd - gt - 1] = '\0';
    if (comma) {
        size_t pl = (size_t)(colon - comma - 1);
        if (pl > sizeof f.path - 1) pl = sizeof f.path - 1;
        memcpy(f.path, comma + 1, pl); f.path[pl] = '\0';
        for (const char* q = f.path; *q; q++) if (*q == '*') f.digipeated++;
    }
    f.infoRaw = colon + 1;
    f.infoLen = n - (size_t)(colon + 1 - s);
    parseInfo(f.infoRaw, f.infoLen, f.dest, f.info);
    return true;
}

static void positionText(const Info& i, char* out, size_t cap) {
    size_t o = (size_t)snprintf(out, cap, "%.4f %.4f %c%c", i.latE7 / 1e7, i.lonE7 / 1e7,
                                i.symTable ? i.symTable : ' ', i.symCode ? i.symCode : ' ');
    if (o < cap && i.hasCourseSpeed) o += (size_t)snprintf(out + o, cap - o, " %u\xF8 %ukm/h", (unsigned)i.course, (unsigned)i.speedKmh);
    if (o < cap && i.hasAlt) o += (size_t)snprintf(out + o, cap - o, " %ldm", (long)i.altM);
    if (o < cap && i.text[0]) snprintf(out + o, cap - o, " %s", i.text);
}

void summary(const Lora::Packet& pk, char* out, size_t cap) {
    Frame f;
    if (!parse(pk.data, pk.len, f)) { snprintf(out, cap, "APRS bad frame"); return; }
    size_t o = (size_t)snprintf(out, cap, "%s>%s", f.src, f.dest);
    if (o < cap && f.path[0]) o += (size_t)snprintf(out + o, cap - o, ",%s", f.path);
    if (o >= cap) return;
    char body[160];
    switch (f.info.kind) {
        case INFO_POSITION: case INFO_POSITION_TS: case INFO_MICE:
            if (f.info.hasPos) { positionText(f.info, body, sizeof body); snprintf(out + o, cap - o, " pos %s", body); }
            else snprintf(out + o, cap - o, " pos ?");
            break;
        case INFO_MESSAGE: snprintf(out + o, cap - o, " msg to %s: %s", f.info.addressee, f.info.text); break;
        case INFO_STATUS:  snprintf(out + o, cap - o, " status %s", f.info.text); break;
        case INFO_OBJECT:  snprintf(out + o, cap - o, " object"); break;
        case INFO_TELEMETRY: snprintf(out + o, cap - o, " telemetry"); break;
        default: {
            const size_t n = f.infoLen < 60 ? f.infoLen : 60;
            snprintf(out + o, cap - o, " %.*s", (int)n, f.infoRaw);
            break;
        }
    }
}

}

// ---- MeshCom ---------------------------------------------------------------------
namespace MeshCom {

using namespace LoraCrypto;

const char* hwName(uint8_t id) {
    // The ids MeshCom's README lists for the boards one meets.
    switch (id) {
        case 1: return "T-Beam";       case 2: return "T-Beam 1268";  case 3: return "T-Beam 0.7";
        case 4: return "T-Echo";       case 5: return "T-Deck";       case 6: return "T-Deck Plus";
        case 7: return "TLORA V2";     case 8: return "TLORA 1.6";    case 9: return "E22";
        case 10: return "RAK4631";     case 11: return "Heltec V2";   case 12: return "Heltec V3";
        case 39: return "Heltec V2.1"; case 43: return "Heltec V3";   case 44: return "Heltec E290";
        case 45: return "Heltec T114"; case 46: return "Heltec Tracker"; case 48: return "T-Beam Supreme";
        default: return nullptr;
    }
}

const char* modName(uint8_t id) {
    switch (id) {
        case 3: return "SF11/CR6/250";  case 4: return "SF12/CR8/125";  case 7: return "SF11/CR5/250";
        case 8: return "SF10/CR6/250";  case 9: return "SF9/CR6/250";   case 10: return "SF8/CR6/250";
        default: return nullptr;
    }
}

bool parse(const uint8_t* d, uint8_t len, Frame& f) {
    memset(&f, 0, sizeof f);
    f.batteryPct = -1;
    if (len < 12) return false;
    f.type = (char)d[0];
    f.msgId = rd32le(d + 1);
    f.hopsLeft  = d[5] & 0x0F;
    f.viaServer = (d[5] & 0x80) != 0;
    f.track     = (d[5] & 0x40) != 0;
    if (f.type == 'A') {
        if (len != 12) return false;
        f.ackFor = rd32le(d + 6);
        f.fcsOk = true;
        return true;
    }
    if (f.type != ':' && f.type != '!' && f.type != '@') return false;
    if (d[len - 1] != 0x7E || len < 6 + 1 + 9) return false;
    // From the end: 7E, subversion, last-hop hw, fw, FCS(2), mod, hw, 0x00.
    f.subVersion = (char)d[len - 2];
    f.lastHopHw  = d[len - 3];
    f.fwVersion  = d[len - 4];
    const uint16_t fcs = rd16be(d + len - 6);
    f.modId = d[len - 7];
    f.hwId  = d[len - 8];
    if (d[len - 9] != 0x00) return false;
    uint16_t sum = 0;
    for (size_t i = 0; i < (size_t)len - 6; i++) sum = (uint16_t)(sum + d[i]);
    f.fcsOk = sum == fcs;
    // The ASCII: SRC[,RELAY...]>DEST then the type char again, then the payload.
    const char* s = (const char*)d + 6;
    const size_t n = (size_t)len - 9 - 6;
    const char* gt = (const char*)memchr(s, '>', n);
    if (!gt || gt == s) return false;
    const char* comma = (const char*)memchr(s, ',', (size_t)(gt - s));
    const size_t srcLen = (size_t)((comma ? comma : gt) - s);
    if (srcLen > sizeof f.src - 1) return false;
    memcpy(f.src, s, srcLen); f.src[srcLen] = '\0';
    if (comma) {
        size_t pl = (size_t)(gt - comma - 1);
        if (pl > sizeof f.path - 1) pl = sizeof f.path - 1;
        memcpy(f.path, comma + 1, pl); f.path[pl] = '\0';
    }
    const char* tc = (const char*)memchr(gt + 1, f.type, n - (size_t)(gt + 1 - s));
    if (!tc) return false;
    size_t dl = (size_t)(tc - gt - 1);
    if (dl > sizeof f.dest - 1) dl = sizeof f.dest - 1;
    memcpy(f.dest, gt + 1, dl); f.dest[dl] = '\0';
    f.payload = tc + 1;
    f.payloadLen = n - (size_t)(tc + 1 - s);
    if (f.type == '!' && f.payloadLen >= 19) {
        f.hasInfo = Aprs::parseUncompressed(f.payload, f.payloadLen, f.info.latE7, f.info.lonE7, f.info.symTable, f.info.symCode);
        if (f.hasInfo) {
            f.info.hasPos = true;
            f.info.kind = Aprs::INFO_POSITION;
            const size_t cn = f.payloadLen - 19;
            const size_t m = cn < sizeof f.info.text - 1 ? cn : sizeof f.info.text - 1;
            memcpy(f.info.text, f.payload + 19, m); f.info.text[m] = '\0';
            const char* b = strstr(f.info.text, "/B=");
            if (b) f.batteryPct = (int16_t)atoi(b + 3);
            const char* a = strstr(f.info.text, "/A=");
            if (a) { f.info.hasAlt = true; f.info.altM = (int32_t)(atol(a + 3) * 3048 / 10000); }
        }
    }
    return true;
}

void summary(const Lora::Packet& pk, char* out, size_t cap) {
    Frame f;
    if (!parse(pk.data, pk.len, f)) { snprintf(out, cap, "MeshCom bad frame"); return; }
    if (f.type == 'A') { snprintf(out, cap, "ACK %08lx for %08lx hops %u", (unsigned long)f.msgId, (unsigned long)f.ackFor, (unsigned)f.hopsLeft); return; }
    size_t o = (size_t)snprintf(out, cap, "%s%s%s>%s", f.src, f.path[0] ? "," : "", f.path, f.dest);
    if (o >= cap) return;
    if (f.type == '!') {
        if (f.hasInfo) {
            o += (size_t)snprintf(out + o, cap - o, " pos %.4f %.4f", f.info.latE7 / 1e7, f.info.lonE7 / 1e7);
            if (o < cap && f.info.hasAlt) o += (size_t)snprintf(out + o, cap - o, " %ldm", (long)f.info.altM);
            if (o < cap && f.batteryPct >= 0) o += (size_t)snprintf(out + o, cap - o, " bat %d%%", (int)f.batteryPct);
        } else o += (size_t)snprintf(out + o, cap - o, " pos %.*s", (int)(f.payloadLen < 40 ? f.payloadLen : 40), f.payload);
    } else if (f.type == '@') {
        o += (size_t)snprintf(out + o, cap - o, " HEY %.*s", (int)(f.payloadLen < 60 ? f.payloadLen : 60), f.payload);
    } else {
        o += (size_t)snprintf(out + o, cap - o, " %.*s", (int)(f.payloadLen < 80 ? f.payloadLen : 80), f.payload);
    }
    if (o >= cap) return;
    const char* hw = hwName(f.hwId);
    if (hw) o += (size_t)snprintf(out + o, cap - o, " [%s", hw);
    else    o += (size_t)snprintf(out + o, cap - o, " [hw%u", (unsigned)f.hwId);
    if (o < cap) snprintf(out + o, cap - o, " fw%u%c hop%u%s%s]", (unsigned)f.fwVersion, f.subVersion == '#' ? ' ' : f.subVersion,
                          (unsigned)f.hopsLeft, f.viaServer ? " srv" : "", f.fcsOk ? "" : " FCS?");
}

}
