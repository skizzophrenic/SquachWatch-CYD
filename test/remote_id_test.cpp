// ASTM F3411 Remote ID decoder — src/remote_id.cpp
//
// Every offset in that file was read off a specification, and a wrong one
// produces a coordinate somewhere else in the world rather than an error.
// These build adverts from known values and check what comes back.
#include "remote_id.h"
#include "test_util.h"
#include <cstring>

static uint8_t adv[64];
static uint8_t advLen;

// Wraps a 25-byte message in the framing NimBLE's setServiceData produces:
// length, AD type 0x16, the 16-bit UUID little endian, then the body.
static void frame(const uint8_t* msg) {
    uint8_t i = 0;
    // A decoy structure first, so the AD walker has something to skip.
    adv[i++] = 2; adv[i++] = 0x01; adv[i++] = 0x06;      // Flags
    adv[i++] = (uint8_t)(1 + 2 + 1 + 1 + 25);
    adv[i++] = 0x16;                                     // service data, 16-bit
    adv[i++] = 0xFA; adv[i++] = 0xFF;                    // 0xFFFA
    adv[i++] = 0x0D;                                     // ODID application code
    adv[i++] = 0x01;                                     // message counter
    memcpy(adv + i, msg, 25); i = (uint8_t)(i + 25);
    advLen = i;
}

static void put32(uint8_t* p, int32_t v) {
    p[0] = (uint8_t)(v & 0xFF);         p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF); p[3] = (uint8_t)((v >> 24) & 0xFF);
}
static void put16(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFF); p[1] = (uint8_t)((v >> 8) & 0xFF);
}

int main() {
    RemoteId::Info info;
    uint8_t m[25];

    suite("Basic ID (message type 0)");
    memset(m, 0, sizeof(m));
    m[0] = (uint8_t)(0x0 << 4) | 0x2;     // type 0, protocol 2
    m[1] = (uint8_t)(0x1 << 4) | 0x2;     // IDType 1 high, UAType 2 low
    memcpy(m + 2, "SQUACH1234567890ABCD", 20);
    frame(m);
    ck("decoded", RemoteId::merge(adv, advLen, info, 1000));
    ck("haveBasic set", info.haveBasic);
    ck("serial", strcmp(info.serial, "SQUACH1234567890ABCD") == 0);
    ck("UAType is the LOW nibble of byte 1", info.uaType == 2);
    ck("named QUAD", strcmp(RemoteId::uaTypeName(info.uaType), "QUAD") == 0);

    suite("Location (message type 1)");
    memset(m, 0, sizeof(m));
    m[0] = (uint8_t)(0x1 << 4) | 0x2;
    put32(m + 5,  (int32_t)( 40.7128 * 10000000.0));
    put32(m + 9,  (int32_t)(-74.0060 * 10000000.0));
    put16(m + 15, (uint16_t)((120.0f + 1000.0f) / 0.5f));   // (alt + 1000) * 2
    frame(m);
    ck("decoded", RemoteId::merge(adv, advLen, info, 2000));
    ck("haveLoc set", info.haveLoc);
    ckf("latitude", info.lat, 40.7128f, 0.00002f);
    ckf("longitude (negative, sign preserved)", info.lon, -74.0060f, 0.00002f);
    ckf("altitude, offset by 1000m", info.altM, 120.0f, 0.6f);

    suite("System (message type 4) — the operator");
    memset(m, 0, sizeof(m));
    m[0] = (uint8_t)(0x4 << 4) | 0x2;
    put32(m + 2, (int32_t)( 40.7580 * 10000000.0));
    put32(m + 6, (int32_t)(-73.9855 * 10000000.0));
    frame(m);
    ck("decoded", RemoteId::merge(adv, advLen, info, 3000));
    ck("haveOperator set", info.haveOperator);
    ckf("operator latitude", info.opLat, 40.7580f, 0.00002f);
    ckf("operator longitude", info.opLon, -73.9855f, 0.00002f);

    suite("Accumulation across adverts");
    ck("basic survived two later merges", info.haveBasic && info.serial[0] == 'S');
    ck("location survived", info.haveLoc);
    ck("all three parts held at once",
       info.haveBasic && info.haveLoc && info.haveOperator);
    ck("aircraft and operator are different places",
       fabsf(info.opLat - info.lat) > 0.01f);

    suite("Malformed input");
    RemoteId::Info fresh;
    ck("null buffer", !RemoteId::merge(nullptr, 0, fresh, 1));
    uint8_t junk[8] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    ck("AD length running past the buffer", !RemoteId::merge(junk, sizeof(junk), fresh, 1));
    uint8_t zeros[16] = { 0 };
    ck("all zeros", !RemoteId::merge(zeros, sizeof(zeros), fresh, 1));
    uint8_t trunc[10] = { 9, 0x16, 0xFA, 0xFF, 0x0D, 0x01, 0, 0, 0, 0 };
    ck("claims Remote ID but the message is short",
       !RemoteId::merge(trunc, sizeof(trunc), fresh, 1));
    // Right UUID, wrong application code -- must not be read as ODID.
    uint8_t badapp[36];
    memset(badapp, 0, sizeof(badapp));
    badapp[0] = 1 + 2 + 1 + 1 + 25; badapp[1] = 0x16;
    badapp[2] = 0xFA; badapp[3] = 0xFF; badapp[4] = 0x0E; badapp[5] = 0x01;
    ck("wrong application code", !RemoteId::merge(badapp, 31, fresh, 1));
    ck("nothing written by any reject",
       !fresh.haveBasic && !fresh.haveLoc && !fresh.haveOperator);

    suite("Zero coordinates are not a fix");
    memset(m, 0, sizeof(m));
    m[0] = (uint8_t)(0x1 << 4) | 0x2;      // Location, both coords zero
    frame(m);
    RemoteId::Info z;
    RemoteId::merge(adv, advLen, z, 1);
    ck("0,0 refused — it is a real place off Africa", !z.haveLoc);

    return report();
}
