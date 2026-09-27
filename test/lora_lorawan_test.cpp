// LoRaWAN off the air -- src/lora_lorawan.cpp.
//
// The MIC and the payload cipher are checked against lora-packet's
// published vector (docs/LORA.md recomputed it: DevAddr 49BE7DF1, FCnt 2,
// port 1, "test", MIC 2b11ff0d). The join request is lora-packet's README
// example. The DevAddr rule is checked on the prefixes the registry lists.
// The beacon CRCs use the worked example in RP002.
#include "lora_lorawan.h"
#include "lora_crypto.h"
#include "test_util.h"
#include <cstring>
#include <cstdio>

using namespace LoRaWAN;

static Lora::Packet pk;
static void load(const char* hex, uint8_t sync = 0x34) {
    memset(&pk, 0, sizeof pk);
    pk.len = (uint8_t)LoraCrypto::fromHex(hex, pk.data, sizeof pk.data);
    pk.sync = sync; pk.sf = 7; pk.bwKhz10 = 1250; pk.freqHz = 868100000;
    pk.flags = Lora::PK_CRC_PRESENT | Lora::PK_CRC_OK;
}

int main() {
    suite("An unconfirmed uplink (lora-packet's vector)");
    {
        load("40F17DBE4900020001954378762B11FF0D");
        Frame f;
        ck("parses", parse(pk.data, pk.len, f));
        ck("unconfirmed up", f.mtype == UNCONF_UP && f.uplink);
        ck("DevAddr 49BE7DF1", f.devAddr == 0x49BE7DF1u);
        ck("FCnt 2", f.fCnt == 2);
        ck("port 1, 4 bytes", f.hasFPort && f.fPort == 1 && f.frmLen == 4);
        ck("MIC 2b11ff0d", f.mic == 0x2B11FF0Du);
        uint8_t nwk[16], app[16];
        LoraCrypto::fromHex("44024241ed4ce9a68c6a8bc055233fd3", nwk, 16);
        LoraCrypto::fromHex("ec925802ae430ca77fd3dd73cb2cc588", app, 16);
        ck("MIC verifies with the NwkSKey", verifyMic(f, nwk, 0));
        ck("...and not with the wrong counter half", !verifyMic(f, nwk, 1));
        uint8_t plain[8];
        decryptFrm(f, app, 0, plain);
        ck("payload is 'test'", memcmp(plain, "test", 4) == 0);

        clearSessions();
        ck("session added", addSession("dev1", 0x49BE7DF1u, "44024241ed4ce9a68c6a8bc055233fd3", "ec925802ae430ca77fd3dd73cb2cc588"));
        Decoded d;
        ck("decodes", decode(pk, d));
        ck("keys matched and MIC ok", d.haveKeys && d.micOk);
        ck("plaintext in the record", d.plainLen == 4 && memcmp(d.plain, "test", 4) == 0);
        char line[160]; summary(pk, line, sizeof line);
        ck("summary", strstr(line, "up 49be7df1") && strstr(line, "fcnt 2") && strstr(line, "mic ok 74657374"));
        clearSessions();
        ck("without keys it is an envelope", decode(pk, d) && !d.haveKeys);
    }

    suite("A join request (lora-packet's README example)");
    {
        load("00DC0000D07ED5B3701E6FEDF57CEEAF0085CC587FE913");
        Frame f;
        ck("parses", parse(pk.data, pk.len, f));
        ck("join request", f.mtype == JOIN_REQUEST);
        char je[17], de[17];
        euiText(f.joinEui, je); euiText(f.devEui, de);
        ck("JoinEUI 70B3D57ED00000DC", strcmp(je, "70b3d57ed00000dc") == 0);
        ck("DevEUI 00AFEE7CF5ED6F1E", strcmp(de, "00afee7cf5ed6f1e") == 0);
        ck("DevNonce CC85", f.devNonce == 0xCC85);
        ck("join server is The Things Industries", joinServerName(f.joinEui) && strstr(joinServerName(f.joinEui), "Things"));
        uint8_t dragino[8] = { 0xA8, 0x40, 0x41, 0x00, 0x01, 0x02, 0x03, 0x04 };
        ck("OUI A8:40:41 is Dragino", ouiMaker(dragino) && strcmp(ouiMaker(dragino), "Dragino") == 0);
        uint8_t ttnf[8] = { 0x70, 0xB3, 0xD5, 0x7E, 0xD0, 0x06, 0x12, 0x34 };
        ck("70B3D57ED0 is the TTN Foundation block", strcmp(ouiMaker(ttnf), "TTN Foundation") == 0);
        uint8_t other[8] = { 0x70, 0xB3, 0xD5, 0x12, 0x34, 0x56, 0x78, 0x9A };
        // MA-S: 70B3D5 is an OUI-36 pool (oui36.csv 4,092 rows, mam.csv none).
        ck("70B3D5 elsewhere is the MA-S pool", strstr(ouiMaker(other), "MA-S") != nullptr);
        ck("22 bytes is not a join request", ({ pk.len = 22; !parse(pk.data, pk.len, f); }));
    }

    suite("DevAddr to operator");
    {
        NetInfo n;
        lookupDevAddr(0x26011B2Cu, n);
        ck("26xxxxxx is TTN (type 0, NwkID 0x13)", n.type == 0 && n.nwkId == 0x13 && n.op && strcmp(n.op, "The Things Network") == 0);
        lookupDevAddr(0x27FFFFFFu, n);
        ck("27xxxxxx too", n.op && strcmp(n.op, "The Things Network") == 0);
        lookupDevAddr(0x78000001u, n);
        ck("78xxxxxx is Helium", n.op && strcmp(n.op, "Helium") == 0);
        // Names come from the published allocation verbatim now that the table
        // is generated (tools/gen_netid.py), so Helium's rows read as the legal
        // entity that holds them and the TTN row spells its foundation out.
        // Longer than the 24-character node row, and truncated there like every
        // other long name -- better a name that is the registry's than a
        // shorter one this firmware invented.
        lookupDevAddr(0xE05A0001u, n);
        ck("E05Axxxx is Helium's type 3", n.type == 3 && n.nwkId == 0x2D && n.ambiguous &&
                                          n.op && strcmp(n.op, "Decentralized Wireless Foundation Inc") == 0);
        lookupDevAddr(0xFC014C01u, n);
        ck("FC014Cxx is Helium's type 6", n.type == 6 && n.op &&
                                          strcmp(n.op, "Decentralized Wireless Foundation Inc") == 0);
        lookupDevAddr(0xFC016001u, n);
        ck("FC0160xx is the TTN Foundation", n.op && strcmp(n.op, "The Things Network Foundation") == 0);
        lookupDevAddr(0x74123456u, n);
        ck("74xxxxxx is Minol ZENNER", n.op && strcmp(n.op, "Minol ZENNER") == 0);
        lookupDevAddr(0x00000001u, n);
        ck("00xxxxxx is a private network", n.op && strstr(n.op, "rivate"));
        lookupDevAddr(0x5E000000u, n);
        ck("an unlisted NwkID has no operator", n.op == nullptr);
        // Rows the hand-written 39-row table never had, from the generated
        // 143: one type 0 and one type 7, the type this firmware could not
        // name at all before.
        lookupDevAddr(0x1E000001u, n);
        ck("0Fxxxxxx (type 0) now names Orange", n.type == 0 && n.op && strcmp(n.op, "Orange") == 0);
        lookupDevAddr(0xFE002001u, n);
        ck("a type 7 NetID resolves at all", n.type == 7 && n.op != nullptr);
    }

    suite("MAC commands in the clear");
    {
        // Downlink: LinkADRReq DR5 TX1 mask 00FF nb 1, then DevStatusReq.
        const uint8_t opts[] = { 0x03, 0x51, 0xFF, 0x00, 0x01, 0x06 };
        MacCmd c[4];
        ck("two commands", parseMacCommands(opts, sizeof opts, false, c, 4) == 2 && c[1].cid == 0x06);
        char t[64]; macCmdText(c[0], false, t, sizeof t);
        ck("LinkADRReq text", strcmp(t, "LinkADRReq DR5 TX1 mask 00ff nb1") == 0);
        // Uplink: DevStatusAns battery 127, snr -3
        const uint8_t ans[] = { 0x06, 127, 0x3D };
        ck("one command", parseMacCommands(ans, sizeof ans, true, c, 4) == 1);
        macCmdText(c[0], true, t, sizeof t);
        ck("DevStatusAns text", strcmp(t, "DevStatusAns battery 50%, snr -3 dB") == 0);
        const uint8_t bad[] = { 0x7F, 0x01 };
        ck("an unknown CID stops the walk", parseMacCommands(bad, 2, true, c, 4) == 0);
    }

    suite("Join accept and the session it starts");
    {
        // Build one: plaintext, MIC with the AppKey, then the network's
        // AES-decrypt; open it and expect the same fields and a good MIC.
        uint8_t appKey[16];
        LoraCrypto::fromHex("000102030405060708090a0b0c0d0e0f", appKey, 16);
        uint8_t plain[17] = { 0x20, 0x11, 0x22, 0x33, 0x13, 0x00, 0x00, 0x2C, 0x1B, 0x01, 0x26, 0x03, 0x05 };
        LoraCrypto::Aes128 k; k.setKey(appKey);
        uint8_t mac[16]; LoraCrypto::cmac(k, plain, 13, mac);
        memcpy(plain + 13, mac, 4);
        uint8_t wire[17]; wire[0] = plain[0];
        k.decryptBlock(plain + 1, wire + 1);
        JoinAccept ja;
        ck("opens", openJoinAccept(wire, 17, appKey, ja));
        ck("MIC ok", ja.micOk);
        ck("JoinNonce 332211", ja.joinNonce == 0x332211);
        ck("NetID 000013", ja.netId == 0x000013);
        ck("DevAddr 26011B2C", ja.devAddr == 0x26011B2Cu);
        ck("RX2 DR3, RX1 offset 0, delay 5", ja.rx2Dr == 3 && ja.rx1DrOffset == 0 && ja.rxDelay == 5);
        uint8_t nwk[16], app[16];
        deriveSession(appKey, ja.joinNonce, ja.netId, 0xCC85, nwk, app);
        ck("the two session keys differ", memcmp(nwk, app, 16) != 0);
        wire[5] ^= 1;
        ck("a flipped byte fails the MIC", openJoinAccept(wire, 17, appKey, ja) && !ja.micOk);
    }

    suite("The Class B beacon (RP002's worked example)");
    {
        load("0000" "000002CC" "A27E" "00" "012000" "008103" "DE55");
        pk.flags |= Lora::PK_IMPLICIT;
        Beacon b;
        ck("17 bytes parse", parseBeacon(pk.data, pk.len, b));
        ck("first CRC", b.crc1Ok);
        ck("second CRC", b.crc2Ok);
        ck("GPS time", b.gpsTime == 0xCC020000u);
        ck("a position", b.infoDesc == 0);
        Decoded d;
        ck("decode() sees a beacon on an implicit frame", decode(pk, d) && d.isBeacon);
    }

    suite("Cayenne LPP");
    {
        const uint8_t lpp[] = { 0x01, 0x67, 0x00, 0xEA, 0x02, 0x68, 0x50, 0x03, 0x88, 0x07, 0xBD, 0x2E, 0x01, 0x15, 0x2C, 0x00, 0x2E, 0xE0 };
        char t[128];
        ck("three channels", cayenneText(lpp, sizeof lpp, t, sizeof t) == 3);
        ck("temperature 23.4", strstr(t, "1 temp 23.4C") != nullptr);
        ck("humidity 40", strstr(t, "2 hum 40.0%") != nullptr);
        ck("gps", strstr(t, "3 gps 50.7") && strstr(t, "120m"));
        const uint8_t not_lpp[] = { 0x40, 0xF1, 0x7D };
        ck("not LPP reads as nothing", cayenneText(not_lpp, 3, t, sizeof t) == 0);
    }

    return report();
}
