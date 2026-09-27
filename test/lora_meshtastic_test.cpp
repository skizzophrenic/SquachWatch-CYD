// Meshtastic off the air -- src/lora_meshtastic.cpp.
//
// The header fields, the channel hash and the default key were each checked
// by recomputing them (docs/LORA.md); here they are pinned. The cipher has no
// test vector we could find in the open, so it is checked the way the
// firmware itself checks it: a Data protobuf built by hand, encrypted with the
// published key and the documented nonce, must come back through decode() as
// the text that went in -- and with the wrong key it must not.
#include "lora_meshtastic.h"
#include "lora_crypto.h"
#include "test_util.h"
#include <cstring>
#include <cstdio>

using namespace Meshtastic;

static Lora::Packet pk;

static void put32(uint8_t* p, uint32_t v) { LoraCrypto::wr32le(p, v); }

// A packet with the given header and a plaintext Data payload, encrypted
// with `key` (nullptr for none).
static void build(uint32_t to, uint32_t from, uint32_t id, uint8_t flags, uint8_t chHash,
                  const uint8_t* data, uint8_t dlen, const uint8_t* key) {
    memset(&pk, 0, sizeof pk);
    pk.sync = 0x2B; pk.sf = 11; pk.bwKhz10 = 2500; pk.freqHz = 869525000;
    pk.flags = Lora::PK_CRC_PRESENT | Lora::PK_CRC_OK;
    put32(pk.data, to); put32(pk.data + 4, from); put32(pk.data + 8, id);
    pk.data[12] = flags; pk.data[13] = chHash; pk.data[14] = 0; pk.data[15] = (uint8_t)from;
    memcpy(pk.data + 16, data, dlen);
    pk.len = (uint8_t)(16 + dlen);
    if (key) {
        Header h; parseHeader(pk.data, pk.len, h);
        Channel c; memset(&c, 0, sizeof c); memcpy(c.key, key, 16); c.keyLen = 16;
        decrypt(h, c, pk.data + 16, dlen);   // CTR: encrypting is the same operation
    }
}

// Data{portnum=1, payload="hello"}: 08 01 12 05 'h' 'e' 'l' 'l' 'o'
static const uint8_t TEXT_DATA[] = { 0x08, 0x01, 0x12, 0x05, 'h', 'e', 'l', 'l', 'o' };

int main() {
    uint8_t key[16];
    expandPsk(1, key);

    suite("The default key and the channel hashes");
    {
        char hex[33]; LoraCrypto::toHex(key, 16, hex);
        ck("AQ== expands to d4f1bb3a...6901", strcmp(hex, "d4f1bb3a20290759f0bcffabcf4e6901") == 0);
        uint8_t k2[16]; expandPsk(2, k2);
        ck("psk 2 raises the last byte", k2[15] == 0x02 && memcmp(k2, key, 15) == 0);
        ck("LongFast hash 0x08",   channelHash("LongFast", key, 16) == 0x08);
        ck("MediumFast hash 0x1F", channelHash("MediumFast", key, 16) == 0x1F);
        ck("ShortSlow hash 0x77",  channelHash("ShortSlow", key, 16) == 0x77);
        ck("NarrowSlow in ham mode 0x10", channelHash("NarrowSlow", nullptr, 0) == 0x10);
        ck("built-in channel 0 is LongFast on the default key", channel(0).hash == 0x08 && channel(0).keyLen == 16);
    }

    suite("The header");
    {
        // flags: hop_limit 2, want_ack, hop_start 3 -> 0b011_0_1_010 = 0x6A
        build(BROADCAST, 0x3b1c9a2e, 0x1a2b3c4d, 0x6A, 0x08, TEXT_DATA, sizeof TEXT_DATA, nullptr);
        Header h;
        ck("parses", parseHeader(pk.data, pk.len, h));
        ck("to is broadcast", h.to == BROADCAST);
        ck("from", h.from == 0x3b1c9a2e);
        ck("id", h.id == 0x1a2b3c4d);
        ck("hop_limit 2", h.hopLimit == 2);
        ck("hop_start 3", h.hopStart == 3);
        ck("want_ack", h.wantAck && !h.viaMqtt);
        ck("one hop away", hopsAway(h) == 1);
        ck("relay_node is the sender's last byte", h.relayNode == 0x2e);
        char id[12]; nodeId(h.from, id);
        ck("node id string", strcmp(id, "!3b1c9a2e") == 0);
        ck("a zero id is not a packet", ({ uint8_t z[16] = {0}; Header hz; !parseHeader(z, 16, hz); }));
    }

    suite("A text on LongFast, through the cipher and back");
    {
        build(BROADCAST, 0x3b1c9a2e, 0x1a2b3c4d, 0x63, 0x08, TEXT_DATA, sizeof TEXT_DATA, key);
        ck("the bytes on the air are not the plaintext", memcmp(pk.data + 16, TEXT_DATA, sizeof TEXT_DATA) != 0);
        Decoded d;
        ck("decodes", decode(pk, d));
        ck("opened by a channel", d.haveData);
        ck("portnum TEXT", d.data.portnum == PORT_TEXT);
        ck("the text", strcmp(d.text, "hello") == 0);
        char line[96]; summary(pk, line, sizeof line);
        ck("summary names it", strstr(line, "!3b1c9a2e>all") && strstr(line, "TEXT hello"));
        // The same bytes under a hash nobody has a key for stay shut.
        pk.data[13] = 0x5A;
        ck("an unknown channel hash stays private", decode(pk, d) && !d.haveData);
        // Change one byte of the key and the plaintext must not come back.
        uint8_t wrong[16]; memcpy(wrong, key, 16); wrong[3] ^= 1;
        build(BROADCAST, 0x3b1c9a2e, 0x1a2b3c4d, 0x63, 0x08, TEXT_DATA, sizeof TEXT_DATA, wrong);
        ck("a wrong key does not parse as Data", decode(pk, d) && !d.haveData);
    }

    suite("A long payload crosses a CTR block boundary");
    {
        // Data{portnum=1, payload=40 x 'a'}: 08 01 12 28 aaaa...
        uint8_t data[4 + 40] = { 0x08, 0x01, 0x12, 40 };
        memset(data + 4, 'a', 40);
        build(BROADCAST, 0x11223344, 0x55667788, 0x63, 0x08, data, sizeof data, key);
        Decoded d;
        ck("decodes", decode(pk, d) && d.haveData);
        ck("all forty come back", d.data.payloadLen == 40 && d.text[39] == 'a' && d.text[40] == '\0');
    }

    suite("NodeInfo");
    {
        // User{id="!3b1c9a2e", long_name="Squachy", short_name="SQCH", hw_model=43, role=2}
        const uint8_t user[] = { 0x0A, 9, '!', '3','b','1','c','9','a','2','e',
                                 0x12, 7, 'S','q','u','a','c','h','y',
                                 0x1A, 4, 'S','Q','C','H',
                                 0x28, 43, 0x38, 2 };
        uint8_t data[64] = { 0x08, 0x04, 0x12, (uint8_t)sizeof user };
        memcpy(data + 4, user, sizeof user);
        build(BROADCAST, 0x3b1c9a2e, 0x0000abcd, 0x63, 0x08, data, (uint8_t)(4 + sizeof user), key);
        Decoded d;
        ck("decodes", decode(pk, d) && d.haveData && d.data.portnum == PORT_NODEINFO);
        ck("long name", strcmp(d.user.longName, "Squachy") == 0);
        ck("short name", strcmp(d.user.shortName, "SQCH") == 0);
        char hw[12];
        ck("hardware Heltec V3", strcmp(hwModelName(d.user.hwModel, hw, sizeof hw), "Heltec V3") == 0);
        ck("role ROUTER", strcmp(roleName(d.user.role), "ROUTER") == 0);
    }

    suite("Position and telemetry");
    {
        // Position{latitude_i=507000000 (sfixed32), longitude_i=70000000, altitude=42, precision_bits=16}
        uint8_t pos[32]; uint8_t n = 0;
        pos[n++] = 0x0D; put32(pos + n, 507000000u); n += 4;
        pos[n++] = 0x15; put32(pos + n, 70000000u);  n += 4;
        pos[n++] = 0x18; pos[n++] = 42;
        pos[n++] = 0xB8; pos[n++] = 0x01; pos[n++] = 16;   // field 23 varint
        uint8_t data[64] = { 0x08, 0x03, 0x12, n };
        memcpy(data + 4, pos, n);
        build(BROADCAST, 0x3b1c9a2e, 0x0000abce, 0x63, 0x08, data, (uint8_t)(4 + n), key);
        Decoded d;
        ck("position decodes", decode(pk, d) && d.haveData && d.data.portnum == PORT_POSITION);
        ck("lat 50.7", d.pos.hasLatLon && d.pos.latI == 507000000);
        ck("lon 7.0", d.pos.lonI == 70000000);
        ck("altitude 42", d.pos.altitude == 42);
        ck("precision 16 bits", d.pos.precisionBits == 16);

        // Telemetry{device_metrics{battery_level=87, voltage=3.9f, channel_utilization=12.5f, air_util_tx=1.5f}}
        uint8_t dm[32]; n = 0;
        dm[n++] = 0x08; dm[n++] = 87;
        float v = 3.9f, cu = 12.5f, at = 1.5f; uint32_t u;
        dm[n++] = 0x15; memcpy(&u, &v, 4);  put32(dm + n, u); n += 4;
        dm[n++] = 0x1D; memcpy(&u, &cu, 4); put32(dm + n, u); n += 4;
        dm[n++] = 0x25; memcpy(&u, &at, 4); put32(dm + n, u); n += 4;
        uint8_t tel[40] = { 0x12, n };
        memcpy(tel + 2, dm, n);
        uint8_t data2[64] = { 0x08, 67, 0x12, (uint8_t)(2 + n) };
        memcpy(data2 + 4, tel, 2 + n);
        build(BROADCAST, 0x3b1c9a2e, 0x0000abcf, 0x63, 0x08, data2, (uint8_t)(4 + 2 + n), key);
        ck("telemetry decodes", decode(pk, d) && d.haveData && d.data.portnum == PORT_TELEMETRY && d.tel.kind == 2);
        ck("battery 87", d.tel.batteryLevel == 87);
        ckf("voltage", d.tel.voltage, 3.9f, 0.001f);
        ckf("channel utilisation", d.tel.channelUtil, 12.5f, 0.001f);
    }

    suite("Traceroute");
    {
        // RouteDiscovery{route=[0x11111111, 0x22222222] packed, snr_towards=[24, -8] packed}
        uint8_t rd[32]; uint8_t n = 0;
        rd[n++] = 0x0A; rd[n++] = 8; put32(rd + n, 0x11111111); n += 4; put32(rd + n, 0x22222222); n += 4;
        rd[n++] = 0x12; rd[n++] = 11; rd[n++] = 24;
        // -8 as a varint is ten bytes of sign extension
        rd[n++] = 0xF8; for (int i = 0; i < 8; i++) rd[n++] = 0xFF; rd[n++] = 0x01;
        uint8_t data[64] = { 0x08, 70, 0x12, n };
        memcpy(data + 4, rd, n);
        build(0x33333333, 0x3b1c9a2e, 0x0000abd0, 0x63, 0x08, data, (uint8_t)(4 + n), key);
        Decoded d;
        ck("decodes", decode(pk, d) && d.haveData && d.data.portnum == PORT_TRACEROUTE);
        ck("two hops", d.route.n == 2 && d.route.route[1] == 0x22222222);
        ck("snr 6.0 and -2.0", d.route.nSnr == 2 && d.route.snrTowards[0] == 24 && d.route.snrTowards[1] == -8);
    }

    suite("The user's own channel");
    {
        clearUserChannels();
        ck("added", addChannel("Squad", "AQ=="));
        const Channel& c = channel(channelCount() - 1);
        ck("hash is name xor default key", c.hash == channelHash("Squad", key, 16));
        ck("a 16-byte hex key", addChannel("Priv", "000102030405060708090a0b0c0d0e0f") && channel(channelCount() - 1).key[15] == 0x0f);
        ck("garbage is refused", !addChannel("Bad", "zz!!"));
        clearUserChannels();
    }

    return report();
}
