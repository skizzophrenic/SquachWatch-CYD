// SquachWatch-CYD — FANET, the paraglider network on 868.2 MHz.
//
// Plaintext by design and openly specified (3s1d/fanet-stm32 protocol.txt).
// Received only: it is air-safety traffic. docs/LORA.md section 3.6.
// Standalone, for the host tests.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "lora_pkt.h"

namespace Fanet {

enum Type : uint8_t {
    T_ACK = 0, T_TRACKING = 1, T_NAME = 2, T_MESSAGE = 3, T_SERVICE = 4, T_LANDMARK = 5,
    T_REMOTE_CONFIG = 6, T_GROUND = 7, T_HWINFO = 8, T_THERMAL = 9, T_HWINFO2 = 10
};
const char* typeName(uint8_t t);
const char* manufacturerName(uint8_t m);
const char* aircraftName(uint8_t t);
const char* groundName(uint8_t t);

struct Frame {
    uint8_t  type;
    bool     forward, extended;
    uint8_t  manufacturer;
    uint16_t id;
    // extended header
    uint8_t  ackType;       // 0 none, 1 requested, 2 via forward
    bool     unicast, signature, geoForward;
    uint8_t  destManufacturer;
    uint16_t destId;
    const uint8_t* payload;
    uint8_t  payloadLen;
    // by type
    bool     hasPos;
    int32_t  latE7, lonE7;
    bool     online;
    uint8_t  aircraft;      // tracking: 0 other .. 7 UAV
    int32_t  altM;
    uint16_t speedKmh10;    // km/h x 10
    int16_t  climbCms;      // cm/s
    uint16_t heading;       // degrees
    uint8_t  groundType;    // ground tracking
    char     text[64];      // name, message
    // service
    bool     inetGateway, hasTemp, hasWind, hasHumidity, hasPressure;
    int16_t  tempC2;        // 0.5 C units
    uint16_t windHeading;
    uint16_t windKmh10, gustKmh10;
    uint8_t  humidityPct;
    uint16_t pressureHpa10;
};
bool parse(const uint8_t* d, uint8_t len, Frame& f);
void addressText(uint8_t manufacturer, uint16_t id, char out[8]);   // "11:A3F0"
void summary(const Lora::Packet& pk, char* out, size_t cap);

}
