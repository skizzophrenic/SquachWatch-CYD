// SquachWatch-CYD — FANET. See include/lora_fanet.h; layouts from
// 3s1d/fanet-stm32 Src/fanet/radio/protocol.txt.
#include "lora_fanet.h"
#include "lora_crypto.h"
#include <stdio.h>
#include <string.h>

namespace Fanet {

using namespace LoraCrypto;

const char* typeName(uint8_t t) {
    switch (t) {
        case T_ACK: return "ACK";            case T_TRACKING: return "TRACKING";
        case T_NAME: return "NAME";          case T_MESSAGE: return "MESSAGE";
        case T_SERVICE: return "SERVICE";    case T_LANDMARK: return "LANDMARK";
        case T_REMOTE_CONFIG: return "CONFIG"; case T_GROUND: return "GROUND";
        case T_HWINFO: return "HWINFO";      case T_THERMAL: return "THERMAL";
        case T_HWINFO2: return "HWINFO2";    default: return "TYPE ?";
    }
}

const char* manufacturerName(uint8_t m) {
    switch (m) {
        case 0x01: return "Skytraxx";   case 0x07: return "SoftRF";     case 0x11: return "FANET+";
        case 0xE0: return "OGN";        case 0xFB: return "ESP32 base"; case 0xFC: case 0xFD: return "unregistered";
        case 0xFE: return "multicast";  default: return nullptr;
    }
}

const char* aircraftName(uint8_t t) {
    switch (t) {
        case 0: return "other";      case 1: return "paraglider"; case 2: return "hang glider";
        case 3: return "balloon";    case 4: return "glider";     case 5: return "powered";
        case 6: return "helicopter"; case 7: return "UAV";        default: return "?";
    }
}

const char* groundName(uint8_t t) {
    switch (t) {
        case 0: return "other";    case 1: return "walking";     case 2: return "vehicle";
        case 3: return "bike";     case 4: return "boot";        case 8: return "need a ride";
        case 9: return "landed well"; case 12: return "need technical support"; case 13: return "need medical help";
        case 14: return "distress call"; case 15: return "distress call (auto)"; default: return "?";
    }
}

static void coords(const uint8_t* p, int32_t& latE7, int32_t& lonE7) {
    int32_t lat = (int32_t)rd24le(p), lon = (int32_t)rd24le(p + 3);
    if (lat & 0x800000) lat -= 0x1000000;
    if (lon & 0x800000) lon -= 0x1000000;
    // lat = raw / 93206, lon = raw / 46603 degrees
    latE7 = (int32_t)(((int64_t)lat * 10000000LL) / 93206LL);
    lonE7 = (int32_t)(((int64_t)lon * 10000000LL) / 46603LL);
}

bool parse(const uint8_t* d, uint8_t len, Frame& f) {
    memset(&f, 0, sizeof f);
    if (len < 4) return false;
    f.type = d[0] & 0x3F;
    f.forward = (d[0] & 0x40) != 0;
    f.extended = (d[0] & 0x80) != 0;
    f.manufacturer = d[1];
    f.id = rd16le(d + 2);
    size_t off = 4;
    if (f.extended) {
        if (off >= len) return false;
        const uint8_t e = d[off++];
        f.ackType = e >> 6;
        f.unicast = (e & 0x20) != 0;
        f.signature = (e & 0x10) != 0;
        f.geoForward = (e & 0x08) != 0;
        if (f.unicast) {
            if (off + 3 > len) return false;
            f.destManufacturer = d[off]; f.destId = rd16le(d + off + 1); off += 3;
        }
        if (f.signature) { if (off + 4 > len) return false; off += 4; }
    }
    f.payload = d + off;
    f.payloadLen = (uint8_t)(len - off);
    const uint8_t* p = f.payload;
    const uint8_t n = f.payloadLen;
    switch (f.type) {
        case T_TRACKING: {
            if (n < 11) return false;
            coords(p, f.latE7, f.lonE7); f.hasPos = true;
            const uint16_t ta = rd16le(p + 6);
            f.online = (ta & 0x8000) != 0;
            f.aircraft = (ta >> 12) & 7;
            f.altM = (ta & 0x07FF) * ((ta & 0x0800) ? 4 : 1);
            f.speedKmh10 = (uint16_t)((p[8] & 0x7F) * 5 * ((p[8] & 0x80) ? 5 : 1));   // 0.5 km/h units
            int8_t c = (int8_t)(p[9] & 0x7F); if (c & 0x40) c = (int8_t)(c | 0x80);       // 7-bit two's complement
            f.climbCms = (int16_t)(c * 10 * ((p[9] & 0x80) ? 5 : 1));
            f.heading = (uint16_t)((p[10] * 360u) / 256u);
            break;
        }
        case T_NAME: case T_MESSAGE: {
            const uint8_t skip = (f.type == T_MESSAGE) ? 1 : 0;   // the message's subheader
            const size_t tl = n > skip ? n - skip : 0;
            const size_t m = tl < sizeof f.text - 1 ? tl : sizeof f.text - 1;
            memcpy(f.text, p + skip, m); f.text[m] = '\0';
            break;
        }
        case T_SERVICE: {
            if (n < 7) return false;
            const uint8_t h = p[0];
            f.inetGateway = (h & 0x80) != 0;
            coords(p + 1, f.latE7, f.lonE7); f.hasPos = true;
            size_t o = 7;
            if (h & 0x40) { if (o + 1 > n) break; f.hasTemp = true; f.tempC2 = (int8_t)p[o]; o++; }
            if (h & 0x20) {
                if (o + 3 > n) break;
                f.hasWind = true;
                f.windHeading = (uint16_t)((p[o] * 360u) / 256u);
                f.windKmh10 = (uint16_t)((p[o + 1] & 0x7F) * 2 * ((p[o + 1] & 0x80) ? 5 : 1));
                f.gustKmh10 = (uint16_t)((p[o + 2] & 0x7F) * 2 * ((p[o + 2] & 0x80) ? 5 : 1));
                o += 3;
            }
            if (h & 0x10) { if (o + 1 > n) break; f.hasHumidity = true; f.humidityPct = (uint8_t)((p[o] * 4u) / 10u); o++; }
            if (h & 0x08) { if (o + 2 > n) break; f.hasPressure = true; f.pressureHpa10 = (uint16_t)(rd16le(p + o) + 4300); o += 2; }
            break;
        }
        case T_GROUND: {
            if (n < 7) return false;
            coords(p, f.latE7, f.lonE7); f.hasPos = true;
            f.groundType = p[6] >> 4;
            f.online = (p[6] & 1) != 0;
            break;
        }
        default: break;
    }
    return true;
}

void addressText(uint8_t m, uint16_t id, char out[8]) { snprintf(out, 8, "%02X:%04X", m, id); }

void summary(const Lora::Packet& pk, char* out, size_t cap) {
    Frame f;
    if (!parse(pk.data, pk.len, f)) { snprintf(out, cap, "FANET bad frame"); return; }
    char addr[8]; addressText(f.manufacturer, f.id, addr);
    const char* mk = manufacturerName(f.manufacturer);
    size_t o = (size_t)snprintf(out, cap, "%s %s%s%s", typeName(f.type), addr, mk ? " " : "", mk ? mk : "");
    if (o >= cap) return;
    if (f.unicast) {
        char to[8]; addressText(f.destManufacturer, f.destId, to);
        o += (size_t)snprintf(out + o, cap - o, " to %s", to);
        if (o >= cap) return;
    }
    switch (f.type) {
        case T_TRACKING:
            snprintf(out + o, cap - o, " %s %.4f %.4f %ldm %u.%ukm/h %+d.%dm/s %u\xF8%s", aircraftName(f.aircraft),
                     f.latE7 / 1e7, f.lonE7 / 1e7, (long)f.altM, f.speedKmh10 / 10, f.speedKmh10 % 10,
                     f.climbCms / 100, (f.climbCms < 0 ? -f.climbCms : f.climbCms) % 100 / 10, (unsigned)f.heading, f.online ? "" : " offline");
            break;
        case T_NAME: snprintf(out + o, cap - o, " \"%s\"", f.text); break;
        case T_MESSAGE: snprintf(out + o, cap - o, " \"%s\"", f.text); break;
        case T_SERVICE: {
            o += (size_t)snprintf(out + o, cap - o, " %.4f %.4f%s", f.latE7 / 1e7, f.lonE7 / 1e7, f.inetGateway ? " inet-gw" : "");
            if (o < cap && f.hasTemp) o += (size_t)snprintf(out + o, cap - o, " %d.%dC", f.tempC2 / 2, (f.tempC2 % 2) ? 5 : 0);
            if (o < cap && f.hasWind) o += (size_t)snprintf(out + o, cap - o, " wind %u\xF8 %u.%u/%u.%ukm/h", (unsigned)f.windHeading,
                                                            f.windKmh10 / 10, f.windKmh10 % 10, f.gustKmh10 / 10, f.gustKmh10 % 10);
            if (o < cap && f.hasHumidity) o += (size_t)snprintf(out + o, cap - o, " %u%%", (unsigned)f.humidityPct);
            if (o < cap && f.hasPressure) o += (size_t)snprintf(out + o, cap - o, " %u.%uhPa", f.pressureHpa10 / 10, f.pressureHpa10 % 10);
            break;
        }
        case T_GROUND: snprintf(out + o, cap - o, " %s %.4f %.4f", groundName(f.groundType), f.latE7 / 1e7, f.lonE7 / 1e7); break;
        default: snprintf(out + o, cap - o, " %u B", (unsigned)f.payloadLen); break;
    }
}

}
