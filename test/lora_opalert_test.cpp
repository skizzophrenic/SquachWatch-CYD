// Lora::OpAlert -- src/lora_opalert.cpp -- which LoRaWAN frames name an
// operator worth a Detection, and what that Detection is called.
#include "lora_opalert.h"
#include "lora_lorawan.h"
#include "lora_crypto.h"
#include "test_util.h"
#include <cstring>
#include <cstdio>

using namespace Lora;

static Packet pk;
static LoRaWAN::Decoded dec;

// A data frame: MHDR, DevAddr, FCtrl, FCnt, a port, four bytes of payload and
// a MIC nobody checks. `crc` is whether the radio saw a payload CRC and it
// held -- an uplink's does, a downlink has none.
static bool frame(uint8_t mtype, uint32_t devAddr, bool crc, int16_t rssi) {
    memset(&pk, 0, sizeof pk);
    pk.sync = 0x34; pk.sf = 9; pk.bwKhz10 = 1250; pk.freqHz = 869525000; pk.cr = 5;
    pk.flags = crc ? (PK_CRC_PRESENT | PK_CRC_OK) : 0;
    pk.rssi = rssi;
    uint8_t n = 0;
    pk.data[n++] = (uint8_t)(mtype << 5);
    LoraCrypto::wr32le(pk.data + n, devAddr); n = (uint8_t)(n + 4);
    pk.data[n++] = 0x00;                       // FCtrl
    pk.data[n++] = 0x07; pk.data[n++] = 0x00;  // FCnt
    pk.data[n++] = 0x02;                       // FPort
    for (int i = 0; i < 4; i++) pk.data[n++] = (uint8_t)(0xA0 + i);
    for (int i = 0; i < 4; i++) pk.data[n++] = (uint8_t)(0xD0 + i);
    pk.len = n;
    pk.proto = Proto::LORAWAN;
    return LoRaWAN::decode(pk, dec);
}

// Oresundskraft is NetID 0x60000A: type 3, NwkID 0x00A, so DevAddr E014xxxx
// and E015xxxx (docs: the downlinks heard on RX2 SF9, 2026-09-26).
static const uint32_t ORESUND = 0xE0141234u;
// The Things Network is NetID 0x000013: type 0, NwkID 0x13, DevAddr 26xxxxxx.
static const uint32_t TTN = 0x26011B2Cu;

int main() {
    OpAlert::Hit h;

    suite("The name fits the panel");
    {
        char out[20];
        OpAlert::asciiName("\xC3\x96resundskraft", out, sizeof out);
        ck("Oresundskraft, without the dots", strcmp(out, "Oresundskraft") == 0);
        OpAlert::asciiName("\xC3\xA5\xC3\xA4\xC3\xB6 \xC3\x85\xC3\x84\xC3\x96 \xC3\xA9\xC3\xBC\xC3\x9F", out, sizeof out);
        ck("the letters with marks keep their letter", strcmp(out, "aao AAO eus") == 0);
        OpAlert::asciiName("A\xE2\x82\xAC" "B\xC3\x97" "C", out, sizeof out);
        ck("what has no letter is dropped whole", strcmp(out, "ABC") == 0);
        OpAlert::asciiName("International Centre for Free and Open Source Software", out, sizeof out);
        ck("a long name is cut to the field", strlen(out) == 19 && strncmp(out, "International Centr", 19) == 0);
        OpAlert::asciiName("abc\xC3", out, sizeof out);
        ck("a sequence cut short at the end is survived", strcmp(out, "abc") == 0);
    }

    suite("An uplink whose CRC held counts at once");
    {
        OpAlert::clear();
        ck("the frame decodes", frame(LoRaWAN::UNCONF_UP, TTN, true, -88));
        ck("and names its operator", OpAlert::note(dec, pk, 1000, h));
        ck("The Things Network", strcmp(h.name, "The Things Network") == 0);
        ck("NetID 0x000013", h.netId == 0x000013);
        const uint8_t want[6] = { 0x02, 0x4C, 0x57, 0x00, 0x00, 0x13 };
        ck("address 02:4C:57 and the NetID", memcmp(h.mac, want, 6) == 0);
        ck("the packet's signal", h.rssi == -88);
        ck("type 0 carries the whole NetID", h.exact);
    }

    suite("A downlink has no CRC, so one is not enough");
    {
        OpAlert::clear();
        ck("the frame decodes", frame(LoRaWAN::UNCONF_DOWN, ORESUND, false, -101));
        ck("the first one names nobody", !OpAlert::note(dec, pk, 1000, h));
        frame(LoRaWAN::UNCONF_DOWN, ORESUND + 0x10000, false, -97);   // another meter, E015xxxx
        ck("a second device of the same operator does", OpAlert::note(dec, pk, 61000, h));
        ck("Oresundskraft", strcmp(h.name, "Oresundskraft") == 0);
        ck("NetID 0x60000A", h.netId == 0x60000A);
        ck("type 3 is the likeliest owner, not the only one", !h.exact);
        frame(LoRaWAN::CONF_DOWN, ORESUND, false, -70);
        ck("and every frame after it", OpAlert::note(dec, pk, 62000, h) && h.rssi == -70);
    }
    {
        OpAlert::clear();
        frame(LoRaWAN::UNCONF_DOWN, ORESUND, false, -101);
        OpAlert::note(dec, pk, 1000, h);
        frame(LoRaWAN::UNCONF_DOWN, ORESUND, false, -101);
        ck("a second one over an hour later is a first again",
           !OpAlert::note(dec, pk, 1000 + OpAlert::CONFIRM_MS + 1, h));
        ck("and the one after that counts",
           OpAlert::note(dec, pk, 1000 + OpAlert::CONFIRM_MS + 5000, h));
    }
    {
        OpAlert::clear();
        frame(LoRaWAN::UNCONF_DOWN, ORESUND, false, -101);
        OpAlert::note(dec, pk, 1000, h);
        frame(LoRaWAN::UNCONF_DOWN, TTN, false, -90);
        ck("two operators do not vouch for each other", !OpAlert::note(dec, pk, 2000, h));
    }

    suite("One address per operator");
    {
        OpAlert::clear();
        OpAlert::Hit a, b, c;
        frame(LoRaWAN::UNCONF_UP, ORESUND, true, -80);           OpAlert::note(dec, pk, 1000, a);
        frame(LoRaWAN::UNCONF_UP, ORESUND + 0x1ABCD, true, -60); OpAlert::note(dec, pk, 2000, b);
        frame(LoRaWAN::UNCONF_UP, TTN, true, -60);               OpAlert::note(dec, pk, 3000, c);
        ck("two meters, one address", memcmp(a.mac, b.mac, 6) == 0);
        ck("another operator, another address", memcmp(a.mac, c.mac, 6) != 0);
        ck("locally administered, unicast", (a.mac[0] & 0x03) == 0x02);
    }

    suite("What is not an operator");
    {
        OpAlert::clear();
        frame(LoRaWAN::UNCONF_UP, 0x00000000u, true, -60);
        ck("an all-zero address", !OpAlert::note(dec, pk, 1000, h));
        frame(LoRaWAN::UNCONF_UP, 0x03ABCDEFu, true, -60);
        ck("NetID 1, private/experimental", !OpAlert::note(dec, pk, 2000, h));
        // Type 0, NwkID 0x05: the registry has no row 0x000005.
        frame(LoRaWAN::UNCONF_UP, 0x0A123456u, true, -60);
        ck("a prefix the registry does not know", !OpAlert::note(dec, pk, 3000, h));
        memset(&pk, 0, sizeof pk);
        pk.sync = 0x34; pk.flags = PK_CRC_PRESENT | PK_CRC_OK; pk.len = 23;   // MHDR 0x00: a join request
        pk.proto = Proto::LORAWAN;
        ck("a join request decodes", LoRaWAN::decode(pk, dec));
        ck("and carries no DevAddr to name anybody by", !OpAlert::note(dec, pk, 4000, h));
    }

    suite("More operators than slots");
    {
        OpAlert::clear();
        // TTN first, then enough others to push it out, then TTN again: it
        // still counts, because its CRC held, and nothing went out of bounds.
        frame(LoRaWAN::UNCONF_UP, TTN, true, -60);
        ck("heard", OpAlert::note(dec, pk, 1000, h));
        uint8_t named = 0;
        for (uint32_t nwk = 2; nwk < 64; nwk++) {
            frame(LoRaWAN::UNCONF_UP, (nwk << 25) | 0x1234u, true, -60);
            if (OpAlert::note(dec, pk, 2000 + nwk, h)) named++;
        }
        printf("    %u type-0 operators named\n", (unsigned)named);
        ck("more than the table holds went through it", named > OpAlert::CAP);
        frame(LoRaWAN::UNCONF_UP, TTN, true, -60);
        ck("and the first is still heard", OpAlert::note(dec, pk, 9000, h) && h.netId == 0x000013);
    }

    return report();
}
