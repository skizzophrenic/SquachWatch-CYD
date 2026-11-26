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

    // The channel chip's send path: sealGroupText() is what Lora::sendText
    // puts on the air, and decode() is what a receiver holding the same
    // channel makes of it.
    suite("A group text we seal on a hashtag channel opens on that channel");
    {
        clearUserChannels();
        ck("#hiking added", addChannel("#hiking", nullptr));
        const uint8_t hi = (uint8_t)findChannel("#hiking");
        const Channel h = channel(hi);
        uint8_t k[16]; hashtagKey("#hiking", k);
        ck("its key is SHA256(\"#hiking\")", memcmp(h.key, k, 16) == 0 && h.derived);
        uint8_t f[256];
        GroupText echo;
        const uint16_t n = sealGroupText(h, 1790000123u, "SQUACHY", "trailhead at noon", f, &echo);
        ck("a frame came out", n > 5 && ((n - 5) % 16) == 0);
        ck("flood GRP_TXT, no path", f[0] == 0x15 && f[1] == 0x00);
        ck("its hash byte is the channel's", f[2] == h.hash && f[2] == channelHash(k));
        ck("and not Public's", f[2] != 0x11 || h.hash == 0x11);
        ck("the echo is what a relay's copy reads", !strcmp(echo.sender, "SQUACHY") && !strcmp(echo.text, "trailhead at noon") && echo.timestamp == 1790000123u);
        memset(&pk, 0, sizeof pk);
        pk.flags = Lora::PK_CRC_PRESENT | Lora::PK_CRC_OK;
        memcpy(pk.data, f, n); pk.len = (uint8_t)n;
        Decoded d;
        ck("decodes", decode(pk, d) && d.haveGroup);
        ck("on that channel", d.haveChannel && d.channelIdx == hi);
        ck("sender and text", !strcmp(d.grp.sender, "SQUACHY") && !strcmp(d.grp.text, "trailhead at noon"));
        ck("the stamp", d.grp.timestamp == 1790000123u);
        // A board without #hiking cannot read it.
        clearUserChannels();
        ck("without the channel it stays shut", decode(pk, d) && !d.haveGroup);
    }

    suite("A keyed channel, and Public");
    {
        clearUserChannels();
        ck("a keyed channel added", addChannel("RidgeCrew", "3c910e5d77a2481fb62be0946cd3058e"));
        const uint8_t ri = (uint8_t)findChannel("RidgeCrew");
        uint8_t f[256];
        uint16_t n = sealGroupText(channel(ri), 1790000200u, "SQUACHY", "radio check", f);
        memset(&pk, 0, sizeof pk); memcpy(pk.data, f, n); pk.len = (uint8_t)n;
        Decoded d;
        ck("opens on it", decode(pk, d) && d.haveGroup && d.channelIdx == ri && f[2] == channel(ri).hash);
        n = sealGroupText(channel(0), 1790000201u, "SQUACHY", "hi all", f);
        memset(&pk, 0, sizeof pk); memcpy(pk.data, f, n); pk.len = (uint8_t)n;
        ck("Public: hash 0x11, opens on channel 0", f[2] == 0x11 && decode(pk, d) && d.haveGroup && d.channelIdx == 0 && !strcmp(d.grp.text, "hi all"));
        setChannelEnabled(ri, false);
        ck("a muted channel seals nothing", sealGroupText(channel(ri), 1, "a", "b", f) == 0);
        clearUserChannels();
    }

    suite("A direct message, against frames built by Python's cryptography (scratchpad mc_vectors.py)");
    {
        uint8_t pubA[32], pubB[32], prvA[64], prvB[64], seedA[32], seedB[32], secret[32], f[256], want[256];
        LoraCrypto::fromHex("101112131415161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f", seedA, 32);
        LoraCrypto::fromHex("808182838485868788898a8b8c8d8e8f909192939495969798999a9b9c9d9e9f", seedB, 32);
        LoraCrypto::ed25519KeyPair(seedA, pubA, prvA);
        LoraCrypto::ed25519KeyPair(seedB, pubB, prvB);
        ck("the shared secret", LoraCrypto::ed25519KeyExchange(secret, pubB, prvA));

        uint8_t ack[4];
        const uint16_t n = buildDirectText(secret, pubB, pubA, 1790000123u, 0, "anyone at the fire tower?", f, ack);
        const size_t wn = LoraCrypto::fromHex("0900cd77184403dfbee7d563d2c149c5d570a10be0040cb9439d265f150bc31f8a431e77ec9c", want, sizeof want);
        ck("TXT_MSG by flood, byte for byte", n == wn && !memcmp(f, want, wn));
        char hex[80]; LoraCrypto::toHex(ack, 4, hex);
        ck("the ACK it should bring back", !strcmp(hex, "da5b666c"));

        // B's side: the frame decodes as a TXT_MSG to B from A, and opens with B's secret.
        Lora::Packet pk; memset(&pk, 0, sizeof pk); memcpy(pk.data, f, n); pk.len = (uint8_t)n;
        Decoded d;
        ck("decodes: TXT_MSG, dest B, src A", decode(pk, d) && d.f.type == TYPE_TXT_MSG && d.destHash == pubB[0] && d.srcHash == pubA[0]);
        uint8_t sB[32]; LoraCrypto::ed25519KeyExchange(sB, pubA, prvB);
        uint8_t plain[192], pl = 0;
        DirectText t;
        ck("and opens with the other end's secret", openSecret(sB, d.f.payload + 2, (uint8_t)(d.f.payloadLen - 2), plain, pl) &&
           parseDirectText(plain, pl, t) && t.timestamp == 1790000123u && t.attempt == 0 && t.txtType == 0 &&
           !strcmp(t.text, "anyone at the fire tower?"));
        uint8_t ack2[4];
        ackFor(plain, (size_t)(5 + t.textLen), pubA, ack2);
        ck("whose ACK is the one the sender expects", !memcmp(ack, ack2, 4));
        plain[0] ^= 1;
        uint8_t bad[192]; memcpy(bad, d.f.payload + 2, d.f.payloadLen - 2); bad[5] ^= 1;
        ck("a touched frame does not open", !openSecret(sB, bad, (uint8_t)(d.f.payloadLen - 2), plain, pl));

        // B's PATH return with the ACK inside, the way BaseChatMesh answers a flood.
        const uint8_t ack6[6] = { ack[0], ack[1], ack[2], ack[3], 0x00, 0x5A };
        const uint16_t pn = buildPathReturn(sB, pubA[0], pubB[0], 0x00, nullptr, ack6, f);
        const size_t pw = LoraCrypto::fromHex("210077cd077e72f26f15ef64f53fd794d8d488d0486f", want, sizeof want);
        ck("PATH return with the ACK, byte for byte", pn == pw && !memcmp(f, want, pw));
        memset(&pk, 0, sizeof pk); memcpy(pk.data, f, pn); pk.len = (uint8_t)pn;
        PathReturn pr;
        ck("A opens it and finds the ACK", decode(pk, d) && d.f.type == TYPE_PATH &&
           openSecret(secret, d.f.payload + 2, (uint8_t)(d.f.payloadLen - 2), plain, pl) && parsePathReturn(plain, pl, pr) &&
           pr.hops == 0 && pr.extraType == TYPE_ACK && pr.extraLen >= 4 && !memcmp(pr.extra, ack, 4));

        const uint16_t an = buildAck(ack6, f);
        memset(&pk, 0, sizeof pk); memcpy(pk.data, f, an); pk.len = (uint8_t)an;
        ck("a bare ACK decodes with its four bytes", decode(pk, d) && d.f.type == TYPE_ACK && !memcmp(d.ackCrc, ack, 4));
    }

    suite("Our advert");
    {
        uint8_t seedA[32], pubA[32], prvA[64], f[256], sig[64];
        LoraCrypto::fromHex("101112131415161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f", seedA, 32);
        LoraCrypto::ed25519KeyPair(seedA, pubA, prvA);
        const uint16_t n = buildAdvert(pubA, prvA, 1790000000u, "SQUACHY", f);
        Lora::Packet pk; memset(&pk, 0, sizeof pk); memcpy(pk.data, f, n); pk.len = (uint8_t)n;
        Decoded d;
        ck("decodes as a chat node named SQUACHY", decode(pk, d) && d.haveAdvert && d.adv.hasName &&
           !strcmp(d.adv.name, "SQUACHY") && d.adv.nodeType == NODE_CHAT && d.adv.timestamp == 1790000000u &&
           !memcmp(d.adv.pubkey, pubA, 32));
        LoraCrypto::fromHex("ba7d7e548a9935dc61db359a3e1babd1ae0195d8cb25fdb267f79b18b625d71fef201e5e520c502e28a34fd82cc80a552bf9505e028e1a517e934723fa21f004", sig, 64);
        ck("signed exactly as Python's Ed25519 signs key | time | app data", !memcmp(d.adv.signature, sig, 64));
    }

    return report();
}
