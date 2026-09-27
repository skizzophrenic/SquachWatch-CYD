// SquachWatch-CYD — the LoRa packet's arithmetic. See include/lora_pkt.h.
#include "lora_pkt.h"

namespace Lora {

const char* protoName(Proto p) {
    switch (p) {
        case Proto::MESHTASTIC: return "MESHTASTIC";
        case Proto::MESHCORE:   return "MESHCORE";
        case Proto::LORAWAN:    return "LORAWAN";
        case Proto::APRS:       return "LORA APRS";
        case Proto::MESHCOM:    return "MESHCOM";
        case Proto::FANET:      return "FANET";
        case Proto::RETICULUM:  return "RETICULUM";
        default:                return "UNKNOWN";
    }
}

const char* protoShort(Proto p) {
    switch (p) {
        case Proto::MESHTASTIC: return "MT";
        case Proto::MESHCORE:   return "MC";
        case Proto::LORAWAN:    return "WAN";
        case Proto::APRS:       return "APRS";
        case Proto::MESHCOM:    return "MCOM";
        case Proto::FANET:      return "FNT";
        case Proto::RETICULUM:  return "RNS";
        default:                return "??";
    }
}

const char* profileGroupName(uint8_t g) {
    switch (g) {
        case PG_MESH_EU: return "MESH 869";
        case PG_LORAWAN: return "LORAWAN";
        case PG_AIR:     return "AIR";
        case PG_HAM_433: return "433 / HAM";
        default:         return "OTHER";
    }
}

uint32_t symbolUs(uint8_t sf, uint16_t bwKhz10) {
    // 2^SF / BW, in microseconds: 2^SF * 10000 / bwKhz10 since BW is in
    // units of 100 Hz and a second is 10^6 us -> 2^SF * 1e6 / (bwKhz10 * 100).
    if (!bwKhz10) return 0;
    return (uint32_t)(((uint64_t)1 << sf) * 10000ull / bwKhz10);
}

bool autoLdro(uint8_t sf, uint16_t bwKhz10) {
    return symbolUs(sf, bwKhz10) >= 16380;
}

uint32_t timeOnAirUs(uint8_t sf, uint16_t bwKhz10, uint8_t cr, uint16_t preamble,
                     uint8_t len, bool crc, bool implicit, bool forceLdro) {
    if (sf < 5 || sf > 12 || !bwKhz10) return 0;
    if (cr < 5) cr = 5;
    if (cr > 8) cr = 8;
    const bool ldro = forceLdro || autoLdro(sf, bwKhz10);
    // n = 8*PL + 16*CRC - 4*SF + 20*explicit + 8*(SF>6), never below zero
    int32_t n = 8 * (int32_t)len + (crc ? 16 : 0) - 4 * (int32_t)sf + (implicit ? 0 : 20) + (sf > 6 ? 8 : 0);
    if (n < 0) n = 0;
    const int32_t den = (sf > 6 && ldro) ? 4 * ((int32_t)sf - 2) : 4 * (int32_t)sf;
    const int32_t payloadSymbols = ((n + den - 1) / den) * (int32_t)cr;
    // symbols = payload + preamble + 8 (sync) + 4.25 (+2 more for SF5/6).
    // Times four to keep the .25; the reference values in docs/LORA.md come
    // out of exactly this.
    const int32_t symbolsX4 = 4 * payloadSymbols + 4 * (int32_t)preamble + 32 + 17 + (sf <= 6 ? 8 : 0);
    const uint64_t symUs = symbolUs(sf, bwKhz10);
    return (uint32_t)((symUs * (uint64_t)symbolsX4) / 4);
}

}
