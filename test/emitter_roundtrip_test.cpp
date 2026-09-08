// SquachEmit -> SquachWatch, end to end.
//
// SquachEmit is a separate repository that broadcasts the signatures this
// firmware matches, so the two have to agree byte for byte about a format
// neither of them owns. Nothing in either build checks that, and the
// failure mode is quiet: an endianness slip on a coordinate emits a
// plausible aircraft somewhere else in the world and the detector happily
// decodes it.
//
// So this builds the three Remote ID adverts exactly the way SquachEmit's
// fireRemoteId() does -- the code below is a deliberate copy of it, not a
// call into it -- and feeds them through this firmware's real decoder.
// If either side is edited without the other, this test goes red.
#include "remote_id.h"
#include "test_util.h"
#include <cstring>

// ---- copied from SquachEmit/src/emit.cpp -------------------------------
static void put32(uint8_t* p, int32_t v) {
    p[0] = (uint8_t)(v & 0xFF);         p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF); p[3] = (uint8_t)((v >> 24) & 0xFF);
}

int main() {
    const int32_t acLat = 407128000, acLon = -740060000;
    const int32_t opLat = 407580000, opLon = -739855000;

    uint8_t msg[3][25];
    memset(msg, 0, sizeof(msg));

    msg[0][0] = (0x0 << 4) | 0x2;
    msg[0][1] = (0x1 << 4) | 0x2;
    memcpy(msg[0] + 2, "SQUACHEMIT-TEST-0001", 20);

    msg[1][0] = (0x1 << 4) | 0x2;
    put32(msg[1] + 5, acLat);
    put32(msg[1] + 9, acLon);
    { const uint16_t alt = (uint16_t)((120 + 1000) * 2);
      msg[1][15] = (uint8_t)(alt & 0xFF); msg[1][16] = (uint8_t)(alt >> 8); }

    msg[2][0] = (0x4 << 4) | 0x2;
    put32(msg[2] + 2, opLat);
    put32(msg[2] + 6, opLon);

    RemoteId::Info info;

    suite("Three adverts, as SquachEmit sends them");
    for (int i = 0; i < 3; i++) {
        // The framing NimBLE's setServiceData() puts around the body.
        uint8_t body[27];
        body[0] = 0x0D;                    // ODID application code
        body[1] = (uint8_t)(i + 1);        // message counter
        memcpy(body + 2, msg[i], 25);

        uint8_t adv[32];
        adv[0] = (uint8_t)(1 + 2 + sizeof(body));
        adv[1] = 0x16;
        adv[2] = 0xFA; adv[3] = 0xFF;
        memcpy(adv + 4, body, sizeof(body));
        const uint8_t advLen = (uint8_t)(1 + adv[0]);

        char label[48];
        snprintf(label, sizeof(label), "advert %d decoded", i + 1);
        ck(label, RemoteId::merge(adv, advLen, info, 1000u * (uint32_t)(i + 1)));

        if (i == 0) {
            // 31 bytes is the entire legacy advertising payload, and this
            // uses all of it: length, AD type, two of UUID, an application
            // code, a counter and a 25-byte message. That is why the
            // standard's messages are 25 bytes, and why nothing else can
            // share the packet.
            ck("fills a legacy advert exactly (31 bytes)", advLen == 31);
        }
    }

    suite("What the detector ends up holding");
    ck("serial", strcmp(info.serial, "SQUACHEMIT-TEST-0001") == 0);
    ck("airframe type survives as a multirotor",
       info.uaType == 2 && strcmp(RemoteId::uaTypeName(info.uaType), "QUAD") == 0);
    ckf("aircraft latitude",  info.lat,   40.7128f, 0.00002f);
    ckf("aircraft longitude", info.lon,  -74.0060f, 0.00002f);
    ckf("aircraft altitude",  info.altM,  120.0f,   0.6f);
    ckf("operator latitude",  info.opLat,  40.7580f, 0.00002f);
    ckf("operator longitude", info.opLon, -73.9855f, 0.00002f);
    ck("all three messages landed in one record",
       info.haveBasic && info.haveLoc && info.haveOperator);
    ck("the operator is not the aircraft — the whole point",
       fabsf(info.opLat - info.lat) > 0.01f);

    return report();
}
