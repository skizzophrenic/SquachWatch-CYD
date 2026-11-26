// The arithmetic under the LoRa decoders -- src/lora_crypto.cpp -- against
// the published vectors: FIPS-197 for AES, RFC 4493 for CMAC, the SHA-2
// examples, RFC 4231 for HMAC, the CRC check strings, and the two Meshtastic
// numbers this firmware recomputes (the frequency slot and the key). A slip
// in any of these decodes every packet wrongly and never says so.
#include "lora_crypto.h"
#include "test_util.h"
#include <cstring>

using namespace LoraCrypto;

static bool hexIs(const uint8_t* p, size_t n, const char* hex) {
    char buf[130];
    toHex(p, n, buf);
    return strcmp(buf, hex) == 0;
}

int main() {
    suite("AES-128 (FIPS-197 C.1)");
    {
        uint8_t key[16], pt[16], ct[16], back[16];
        fromHex("000102030405060708090a0b0c0d0e0f", key, 16);
        fromHex("00112233445566778899aabbccddeeff", pt, 16);
        Aes128 a; a.setKey(key);
        a.encryptBlock(pt, ct);
        ck("encrypt", hexIs(ct, 16, "69c4e0d86a7b0430d8cdb78070b4c55a"));
        a.decryptBlock(ct, back);
        ck("decrypt round trip", memcmp(back, pt, 16) == 0);
    }

    suite("AES-256 (FIPS-197 C.3)");
    {
        uint8_t key[32], pt[16], ct[16];
        fromHex("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f", key, 32);
        fromHex("00112233445566778899aabbccddeeff", pt, 16);
        Aes256 a; a.setKey(key);
        a.encryptBlock(pt, ct);
        ck("encrypt", hexIs(ct, 16, "8ea2b7ca516745bfeafc49904b496089"));
    }

    suite("AES-256-CCM, 13-byte nonce, 8-byte tag (vector from Python's cryptography)");
    {
        uint8_t key[32], nonce[13], pt[40], ct[40], tag[8], back[40];
        fromHex("202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f", key, 32);
        fromHex("a0a1a2a3a4a5a6a7a8a9aaabac", nonce, 13);
        for (int i = 0; i < 40; i++) pt[i] = (uint8_t)i;
        Aes256 a; a.setKey(key);
        ccmSeal(a, nonce, pt, 40, ct, tag);
        ck("ciphertext", hexIs(ct, 40, "c2c7fca38eeb5eea02618589b190c76857839dd71f9f8c3be9a4671a2da393fdfb080d0a79f37c15"));
        ck("tag", hexIs(tag, 8, "531671729d9efa68"));
        ck("opens", ccmOpen(a, nonce, ct, 40, tag, back) && memcmp(back, pt, 40) == 0);
        ct[7] ^= 0x01;
        ck("a flipped bit is refused", !ccmOpen(a, nonce, ct, 40, tag, back));
        ct[7] ^= 0x01; tag[0] ^= 0x80;
        ck("a flipped tag bit is refused", !ccmOpen(a, nonce, ct, 40, tag, back));
        uint8_t inPlace[40]; memcpy(inPlace, pt, 40);
        ccmSeal(a, nonce, inPlace, 40, inPlace, tag);
        ck("sealing in place gives the same bytes", hexIs(inPlace, 40, "c2c7fca38eeb5eea02618589b190c76857839dd71f9f8c3be9a4671a2da393fdfb080d0a79f37c15"));
    }

    suite("X25519 (RFC 7748)");
    {
        uint8_t k[32], u[32], out[32];
        fromHex("a546e36bf0527c9d3b16154b82465edd62144c0ac1fc5a18506a2244ba449ac4", k, 32);
        fromHex("e6db6867583030db3594c1a424b15f7c726624ec26b3353b10a903a6d0ab1c4c", u, 32);
        ck("section 5.2, first vector", x25519(out, k, u) &&
           hexIs(out, 32, "c3da55379de9c6908e94ea4df28d084f32eccf03491c71f754b4075577a28552"));
        uint8_t ap[32], bp[32], apub[32], bpub[32], s1[32], s2[32];
        fromHex("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a", ap, 32);
        fromHex("5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb", bp, 32);
        x25519Base(apub, ap); x25519Base(bpub, bp);
        ck("section 6.1, Alice's public key", hexIs(apub, 32, "8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a"));
        ck("section 6.1, Bob's public key",   hexIs(bpub, 32, "de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f"));
        ck("section 6.1, the shared secret both ways",
           x25519(s1, ap, bpub) && x25519(s2, bp, apub) && memcmp(s1, s2, 32) == 0 &&
           hexIs(s1, 32, "4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742"));
        uint8_t zero[32] = {0};
        ck("a low-order point is refused (all-zero result)", !x25519(out, ap, zero));
    }

    suite("SHA-512 (FIPS 180-2 example) and Ed25519 (RFC 8032 section 7.1)");
    {
        uint8_t out[64];
        sha512((const uint8_t*)"abc", 3, out);
        ck("SHA-512 of \"abc\"", hexIs(out, 64, "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f"));
        uint8_t big[200]; memset(big, 'a', sizeof big);
        uint8_t one[64], two[64];
        sha512(big, sizeof big, one);
        Sha512Ctx c; sha512Init(c); sha512Update(c, big, 77); sha512Update(c, big + 77, 123); sha512Final(c, two);
        ck("in two pieces across a block edge, the same digest", !memcmp(one, two, 64));

        uint8_t seed[32], pub[32], prv[64], sig[64], msg[1];
        fromHex("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60", seed, 32);
        ed25519KeyPair(seed, pub, prv);
        ck("test 1: public key", hexIs(pub, 32, "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a"));
        ed25519Sign(sig, msg, 0, pub, prv);
        ck("test 1: signature of nothing", hexIs(sig, 64, "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b"));
        fromHex("4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb", seed, 32);
        ed25519KeyPair(seed, pub, prv);
        ck("test 2: public key", hexIs(pub, 32, "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c"));
        msg[0] = 0x72;
        ed25519Sign(sig, msg, 1, pub, prv);
        ck("test 2: signature of 0x72", hexIs(sig, 64, "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00"));
    }

    suite("MeshCore identities (vectors from Python's cryptography, scratchpad ed_vectors.py)");
    {
        uint8_t seedA[32], seedB[32], pubA[32], pubB[32], prvA[64], prvB[64], sig[64], s1[32], s2[32], m[64];
        fromHex("101112131415161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f", seedA, 32);
        fromHex("808182838485868788898a8b8c8d8e8f909192939495969798999a9b9c9d9e9f", seedB, 32);
        ed25519KeyPair(seedA, pubA, prvA);
        ed25519KeyPair(seedB, pubB, prvB);
        ck("A's public key", hexIs(pubA, 32, "7776e870b93354f2a0b24c23f2a36cc4e80e223218c1b97926fdd018396a2b9b"));
        ck("A's 64-byte private key, orlp's layout",
           hexIs(prvA, 64, "30dbf67498dbee33cb5d3bc53761476e5dc6f3a973875ab45bc2538aff29a9454a9a85c0345ee658f8b7094725d0531d9f68e03e333e8659d116b42174fc384c"));
        ck("B's public key", hexIs(pubB, 32, "cd14b37f956e953194ff7fb73b3d81dcc561d61a7538094b7c3e1a643ee5f3aa"));
        const size_t mn = fromHex("7776e870b93354f2a0b24c23f2a36cc4e80e223218c1b97926fdd018396a2b9b803bb16a8153515541434859", m, sizeof m);
        ed25519Sign(sig, m, mn, pubA, prvA);
        ck("an advert's signature: key, time, app data",
           hexIs(sig, 64, "ba7d7e548a9935dc61db359a3e1babd1ae0195d8cb25fdb267f79b18b625d71fef201e5e520c502e28a34fd82cc80a552bf9505e028e1a517e934723fa21f004"));
        ck("the shared secret, both ways", ed25519KeyExchange(s1, pubB, prvA) && ed25519KeyExchange(s2, pubA, prvB) &&
           !memcmp(s1, s2, 32) && hexIs(s1, 32, "e84e0034bda4d6802a17e3539a155aab2cc820d4a6969866b7f4855be7b35d04"));
    }

    suite("AES-CMAC (RFC 4493)");
    {
        uint8_t key[16], msg[64], mac[16];
        fromHex("2b7e151628aed2a6abf7158809cf4f3c", key, 16);
        fromHex("6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e5130c81c46a35ce411e5fbc1191a0a52eff69f2445df4f9b17ad2b417be66c3710", msg, 64);
        Aes128 a; a.setKey(key);
        cmac(a, msg, 0, mac);  ck("empty",    hexIs(mac, 16, "bb1d6929e95937287fa37d129b756746"));
        cmac(a, msg, 16, mac); ck("16 bytes", hexIs(mac, 16, "070a16b46b4d4144f79bdd9dd04a287c"));
        cmac(a, msg, 40, mac); ck("40 bytes", hexIs(mac, 16, "dfa66747de9ae63030ca32611497c827"));
        cmac(a, msg, 64, mac); ck("64 bytes", hexIs(mac, 16, "51f0bebf7e3b9d92fc49741779363cfe"));
    }

    suite("SHA-256 and HMAC");
    {
        uint8_t out[32];
        sha256((const uint8_t*)"abc", 3, out);
        ck("sha256(abc)", hexIs(out, 32, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
        sha256((const uint8_t*)"", 0, out);
        ck("sha256(empty)", hexIs(out, 32, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
        // RFC 4231 case 1 and case 2.
        uint8_t k1[20]; memset(k1, 0x0b, 20);
        hmacSha256(k1, 20, (const uint8_t*)"Hi There", 8, out);
        ck("hmac case 1", hexIs(out, 32, "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7"));
        hmacSha256((const uint8_t*)"Jefe", 4, (const uint8_t*)"what do ya want for nothing?", 28, out);
        ck("hmac case 2", hexIs(out, 32, "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843"));
    }

    suite("CRCs, djb2, codings");
    {
        const uint8_t* s = (const uint8_t*)"123456789";
        ck("crc32 check string", crc32(s, 9) == 0xCBF43926u);
        ck("crc16 ccitt-false (UKHAS seed)", crc16ccitt(s, 9, 0xFFFF) == 0x29B1);
        ck("crc16 xmodem (beacon seed)", crc16ccitt(s, 9, 0x0000) == 0x31C3);
        // Meshtastic's US LongFast slot: 104 slots of 250 kHz, hash 5381*33...
        ck("djb2(LongFast) % 104 == 19", djb2("LongFast") % 104 == 19);
        uint8_t b[32];
        ck("base64 AQ== is one byte 0x01", fromBase64("AQ==", b, sizeof b) == 1 && b[0] == 0x01);
        ck("base64 MeshCore public key", fromBase64("izOH6cXN6mrJ5e26oRXNcg==", b, sizeof b) == 16 &&
                                          hexIs(b, 16, "8b3387e9c5cdea6ac9e5edbaa115cd72"));
        ck("hex with colons", fromHex("d4:f1:bb:3a", b, sizeof b) == 4 && b[3] == 0x3a);
        ck("hex rejects a bad digit", fromHex("d4g1", b, sizeof b) == 0);
    }

    return report();
}
