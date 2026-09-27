// MeshCore off the air -- src/lora_meshcore.cpp.
//
// The Public channel's hash (0x11) and the hashtag rule (#test -> 0xD9) were
// recomputed independently for docs/LORA.md and are pinned here. The seal is
// checked the way the firmware checks it: a group text built with the
// published key must open and read back, and must not open under a key one
// bit off. The advert is a constructed one; its signature is carried, not
// verified, and the test says so.
#include "lora_meshcore.h"
#include "lora_crypto.h"
#include "test_util.h"
#include <cstring>
#include <cstdio>

using namespace MeshCore;

static Lora::Packet pk;

static void frame(uint8_t header, const uint8_t* path, uint8_t hops, uint8_t hashSize,
                  const uint8_t* payload, uint8_t plen) {
    memset(&pk, 0, sizeof pk);
    pk.sync = 0x12; pk.sf = 8; pk.bwKhz10 = 625; pk.freqHz = 869618000;
    pk.flags = Lora::PK_CRC_PRESENT | Lora::PK_CRC_OK;
    uint8_t n = 0;
    pk.data[n++] = header;
    pk.data[n++] = (uint8_t)(((hashSize - 1) << 6) | hops);
    memcpy(pk.data + n, path, (size_t)hops * hashSize); n = (uint8_t)(n + hops * hashSize);
    memcpy(pk.data + n, payload, plen); n = (uint8_t)(n + plen);
    pk.len = n;
}

// Seal a plaintext the way MeshCore does: zero-pad to 16, AES-128-ECB, then
// a 2-byte HMAC-SHA256 over the ciphertext with the 32-byte secret slot.
static uint8_t seal(const uint8_t key[16], const uint8_t* plain, uint8_t len, uint8_t* out) {
    uint8_t padded[192] = {0};
    memcpy(padded, plain, len);
    const uint8_t clen = (uint8_t)((len + 15) & ~15);
    LoraCrypto::Aes128 a; a.setKey(key);
    for (uint8_t off = 0; off < clen; off = (uint8_t)(off + 16)) a.encryptBlock(padded + off, out + 2 + off);
    uint8_t secret[32] = {0}; memcpy(secret, key, 16);
    uint8_t mac[32];
    LoraCrypto::hmacSha256(secret, 32, out + 2, clen, mac);
    out[0] = mac[0]; out[1] = mac[1];
    return (uint8_t)(2 + clen);
}

int main() {
    suite("Channels");
    {
        ck("Public is channel 0 with hash 0x11", channel(0).hash == 0x11 && channelHash(channel(0).key) == 0x11);
        uint8_t k[16]; hashtagKey("#test", k);
        char hex[33]; LoraCrypto::toHex(k, 16, hex);
        ck("#test key", strcmp(hex, "9cd8fcf22a47333b591d96a2b848b73f") == 0);
        ck("#test hash 0xD9", channelHash(k) == 0xD9);
        clearUserChannels();
        ck("adding 'test' derives the same key", addChannel("test", nullptr) && channel(1).hash == 0xD9);
        ck("adding '#test' too", addChannel("#test", nullptr) && channel(2).hash == 0xD9);
        ck("a hex key", addChannel("Priv", "000102030405060708090a0b0c0d0e0f") && channel(3).key[15] == 0x0f);
        ck("a short key is refused", !addChannel("Bad", "0001"));
        clearUserChannels();
    }

    suite("The frame");
    {
        const uint8_t path[] = { 0xA1, 0xB2 };
        const uint8_t pay[] = { 1, 2, 3, 4, 5 };
        frame(0x15, path, 2, 1, pay, sizeof pay);          // 0x15 = flood GRP_TXT
        Frame f;
        ck("parses", parse(pk.data, pk.len, f));
        ck("flood", f.route == ROUTE_FLOOD);
        ck("GRP_TXT", f.type == TYPE_GRP_TXT);
        ck("two hops of one byte", f.hops == 2 && f.hashSize == 1 && f.path[1] == 0xB2);
        ck("payload", f.payloadLen == 5 && f.payload[4] == 5);
        // Transport flood carries four bytes of codes first.
        uint8_t d[16] = { 0x10, 0x34, 0x12, 0x00, 0x00, 0x00, 0xAA };
        ck("transport codes", parse(d, 7, f) && f.hasTransport && f.transport1 == 0x1234 && f.payloadLen == 1);
        ck("version 1 is refused", ({ uint8_t v[4] = { 0x51, 0, 0, 0 }; !parse(v, 4, f); }));
        ck("a path past the end is refused", ({ uint8_t v[4] = { 0x11, 0x05, 0, 0 }; !parse(v, 4, f); }));
    }

    suite("An advert");
    {
        uint8_t pay[140]; uint8_t n = 0;
        for (int i = 0; i < 32; i++) pay[n++] = (uint8_t)(0x40 + i);   // a key starting 0x40
        LoraCrypto::wr32le(pay + n, 1750000000u); n += 4;
        for (int i = 0; i < 64; i++) pay[n++] = 0xEE;                   // a signature we do not check
        pay[n++] = 0x10 | 0x80 | NODE_REPEATER;                          // lat/lon and a name
        LoraCrypto::wr32le(pay + n, (uint32_t)(int32_t)50733000); n += 4;
        LoraCrypto::wr32le(pay + n, (uint32_t)(int32_t)7099000); n += 4;
        memcpy(pay + n, "DL Repeater 1", 13); n += 13;
        frame(0x11, nullptr, 0, 1, pay, n);                              // flood ADVERT, no path yet
        Decoded d;
        ck("decodes", decode(pk, d) && d.haveAdvert);
        ck("node hash is the key's first byte", d.adv.pubkey[0] == 0x40);
        ck("timestamp", d.adv.timestamp == 1750000000u);
        ck("repeater", d.adv.nodeType == NODE_REPEATER);
        ck("position", d.adv.hasLatLon && d.adv.latE6 == 50733000 && d.adv.lonE6 == 7099000);
        ck("name", d.adv.hasName && strcmp(d.adv.name, "DL Repeater 1") == 0);
        char line[120]; summary(pk, line, sizeof line);
        ck("summary", strstr(line, "ADVERT flood") && strstr(line, "REPEATER") && strstr(line, "DL Repeater 1"));
        ck("too short is not an advert", ({ frame(0x11, nullptr, 0, 1, pay, 100); Decoded e; decode(pk, e) && !e.haveAdvert; }));
    }

    suite("A Public channel message, sealed and opened");
    {
        uint8_t plain[64]; uint8_t n = 0;
        LoraCrypto::wr32le(plain, 1750000100u); n = 4;
        plain[n++] = 0x00;
        memcpy(plain + n, "Squachy: hello mesh", 19); n = (uint8_t)(n + 19);
        uint8_t pay[80];
        pay[0] = 0x11;
        const uint8_t sealed = seal(channel(0).key, plain, n, pay + 1);
        const uint8_t path[] = { 0x40 };
        frame(0x15, path, 1, 1, pay, (uint8_t)(1 + sealed));
        Decoded d;
        ck("decodes", decode(pk, d));
        ck("opened on Public", d.haveGroup && d.channelIdx == 0);
        ck("sender", strcmp(d.grp.sender, "Squachy") == 0);
        ck("text", strcmp(d.grp.text, "hello mesh") == 0);
        ck("timestamp", d.grp.timestamp == 1750000100u);
        char line[120]; summary(pk, line, sizeof line);
        ck("summary", strstr(line, "via 40") && strstr(line, "[Public] Squachy: hello mesh"));
        // header, path_len, one path byte, then the channel hash and the MAC.
        // Flip one bit of the MAC: the seal must refuse.
        pk.data[4] ^= 0x01;
        ck("a broken MAC does not open", decode(pk, d) && !d.haveGroup);
        // Another channel hash with no key known: private.
        pk.data[4] ^= 0x01; pk.data[3] = 0x5A;
        ck("an unknown hash stays shut", decode(pk, d) && !d.haveGroup);
    }

    suite("A trace with SNR per hop");
    {
        uint8_t pay[16]; uint8_t n = 0;
        LoraCrypto::wr32le(pay, 0xCAFEF00Du); n = 4;
        LoraCrypto::wr32le(pay + n, 0x11223344u); n += 4;
        pay[n++] = 0x00;                          // 1-byte hashes
        pay[n++] = 0x40; pay[n++] = 0x77;         // asked to go via these two
        const int8_t snr[] = { 26, -10 };         // 6.5 dB, then -2.5 dB
        frame(0x26, (const uint8_t*)snr, 2, 1, pay, n);   // direct TRACE
        Decoded d;
        ck("decodes", decode(pk, d) && d.haveTrace);
        ck("tag", d.trace.tag == 0xCAFEF00Du);
        ck("two repeaters named", d.trace.nHashes == 2 && d.trace.hashes[1] == 0x77);
        ck("two SNRs", d.trace.nSnr == 2 && d.trace.snr4[0] == 26 && d.trace.snr4[1] == -10);
    }

    return report();
}
