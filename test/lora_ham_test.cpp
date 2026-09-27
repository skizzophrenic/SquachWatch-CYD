// The plaintext networks -- src/lora_aprs.cpp (LoRa APRS and MeshCom) and
// src/lora_fanet.cpp.
//
// APRS positions are built from the spec's own examples where they exist
// (the uncompressed form; the compressed form's worked example), and from
// the documented rules where the spec's example bytes are not printable
// (Mic-E). MeshCom and FANET frames are constructed to their published
// layouts. A coordinate off by one field lands in another country and
// looks fine, which is the reason every one of these is pinned.
#include "lora_aprs.h"
#include "lora_fanet.h"
#include "lora_crypto.h"
#include "test_util.h"
#include <cstring>
#include <cstdio>

static Lora::Packet pk;

static void loadText(const char* prefixHex, const char* text) {
    memset(&pk, 0, sizeof pk);
    size_t n = LoraCrypto::fromHex(prefixHex, pk.data, sizeof pk.data);
    const size_t tl = strlen(text);
    memcpy(pk.data + n, text, tl);
    pk.len = (uint8_t)(n + tl);
    pk.flags = Lora::PK_CRC_PRESENT | Lora::PK_CRC_OK;
}

int main() {
    suite("APRS uncompressed position (APRS 1.01 chapter 8 example)");
    {
        int32_t lat, lon; char t, c;
        ck("4903.50N/07201.75W-", Aprs::parseUncompressed("4903.50N/07201.75W-", 19, lat, lon, t, c));
        ckf("lat 49.0583", lat / 1e7f, 49.05833f, 0.0001f);
        ckf("lon -72.0292", lon / 1e7f, -72.02917f, 0.0001f);
        ck("symbol /-", t == '/' && c == '-');
        ck("ambiguity reads as zeros", Aprs::parseUncompressed("4903.  N/07201.  W-", 19, lat, lon, t, c) && lat / 100000 == 4905);
        ck("a bad minute is refused", !Aprs::parseUncompressed("4963.50N/07201.75W-", 19, lat, lon, t, c));
    }

    suite("A LoRa APRS frame");
    {
        loadText("3cff01", "DH5DAX-7>APLRT1,WIDE1-1,DB0ABC*:!5044.10N/00706.00E>120/018/A=000328 SquachWatch");
        Aprs::Frame f;
        ck("parses", Aprs::parse(pk.data, pk.len, f));
        ck("source", strcmp(f.src, "DH5DAX-7") == 0);
        ck("tocall", strcmp(f.dest, "APLRT1") == 0);
        ck("path", strcmp(f.path, "WIDE1-1,DB0ABC*") == 0 && f.digipeated == 1);
        ck("a position", f.info.kind == Aprs::INFO_POSITION && f.info.hasPos);
        ckf("lat 50.735", f.info.latE7 / 1e7f, 50.735f, 0.0001f);
        ckf("lon 7.1", f.info.lonE7 / 1e7f, 7.1f, 0.0001f);
        ck("course 120, 18 knots = 33 km/h", f.info.hasCourseSpeed && f.info.course == 120 && f.info.speedKmh == 33);
        ck("altitude 328 ft = 99 m", f.info.hasAlt && f.info.altM == 99);
        ck("comment", strcmp(f.info.text, "/A=000328 SquachWatch") == 0);
        char line[160]; Aprs::summary(pk, line, sizeof line);
        ck("summary", strstr(line, "DH5DAX-7>APLRT1,WIDE1-1,DB0ABC*") && strstr(line, "50.7350 7.1000"));
        ck("without the marker it is not APRS", ({ pk.data[1] = 0x00; !Aprs::parse(pk.data, pk.len, f); }));
    }

    suite("APRS message, status, compressed and Mic-E");
    {
        loadText("3cff01", "DB0ABC>APRS::DH5DAX-7 :hello there{42");
        Aprs::Frame f;
        ck("message parses", Aprs::parse(pk.data, pk.len, f) && f.info.kind == Aprs::INFO_MESSAGE);
        ck("addressee trimmed", strcmp(f.info.addressee, "DH5DAX-7") == 0);
        ck("text", strcmp(f.info.text, "hello there{42") == 0);
        loadText("3cff01", "DB0ABC>APRS:>iGate up");
        ck("status", Aprs::parse(pk.data, pk.len, f) && f.info.kind == Aprs::INFO_STATUS && strcmp(f.info.text, "iGate up") == 0);
        // The spec's compressed example: /5L!!<*e7> sT gives 49.5 N, 72.75 W.
        loadText("3cff01", "N0CALL>APRS:=/5L!!<*e7>  sT");
        ck("compressed parses", Aprs::parse(pk.data, pk.len, f) && f.info.hasPos);
        ckf("compressed lat 49.5", f.info.latE7 / 1e7f, 49.5f, 0.001f);
        ckf("compressed lon -72.75", f.info.lonE7 / 1e7f, -72.75f, 0.001f);
        // Mic-E from the rules: lat 33 25.64 N in the destination with the
        // +100 offset and West; lon 112 07.03 W, 20 knots, course 251.
        // d=12 -> '(' ; m=7+60 -> '_' ; h=3 -> 31 is unprintable, so h=30 -> ':'
        // sp=2 -> 30 ; dc = 0*10 + 2 -> 30 ; se = 51 -> 79.
        const char info[] = { '`', '(', '_', ':', 30, 30, 79, 'j', '/', 0 };
        char text[64]; snprintf(text, sizeof text, "N0CALL>S32UVT:%s", info);
        loadText("3cff01", text);
        ck("Mic-E parses", Aprs::parse(pk.data, pk.len, f) && f.info.kind == Aprs::INFO_MICE && f.info.hasPos);
        ckf("Mic-E lat 33.4273", f.info.latE7 / 1e7f, 33.42733f, 0.0001f);
        ckf("Mic-E lon -112.1217", f.info.lonE7 / 1e7f, -112.12167f, 0.0001f);
        ck("Mic-E 20 kn = 37 km/h, course 251", f.info.hasCourseSpeed && f.info.speedKmh == 37 && f.info.course == 251);
        ck("Mic-E symbol j/", f.info.symCode == 'j' && f.info.symTable == '/');
    }

    suite("A MeshCom position");
    {
        // ':' or '!' type, id, flags, "SRC,RELAY>DEST" + type + payload, 0, hw, mod, FCS, fw, lasthop, sub, 7E
        uint8_t d[120]; uint8_t n = 0;
        d[n++] = '!';
        LoraCrypto::wr32le(d + n, 0x12345678u); n += 4;
        d[n++] = 0x40 | 4;                                        // track, 4 hops left
        const char* ascii = "OE1KBC-12,DB0XYZ-12>*!4812.34N/01622.56E#/B=087/A=000560";
        memcpy(d + n, ascii, strlen(ascii)); n = (uint8_t)(n + strlen(ascii));
        d[n++] = 0x00;
        d[n++] = 43;                                              // Heltec V3
        d[n++] = 3;                                               // SF11/CR6/250
        uint16_t sum = 0; for (uint8_t i = 0; i < n; i++) sum = (uint16_t)(sum + d[i]);
        d[n++] = (uint8_t)(sum >> 8); d[n++] = (uint8_t)sum;
        d[n++] = 35; d[n++] = 43; d[n++] = 't'; d[n++] = 0x7E;
        memset(&pk, 0, sizeof pk); memcpy(pk.data, d, n); pk.len = n; pk.flags = Lora::PK_CRC_OK | Lora::PK_CRC_PRESENT;
        MeshCom::Frame f;
        ck("parses", MeshCom::parse(pk.data, pk.len, f));
        ck("position type", f.type == '!');
        ck("message id", f.msgId == 0x12345678u);
        ck("track flag, 4 hops", f.track && !f.viaServer && f.hopsLeft == 4);
        ck("source", strcmp(f.src, "OE1KBC-12") == 0);
        ck("relay path", strcmp(f.path, "DB0XYZ-12") == 0);
        ck("to everyone", strcmp(f.dest, "*") == 0);
        ck("hardware and preset", f.hwId == 43 && f.modId == 3 && strcmp(MeshCom::hwName(43), "Heltec V3") == 0);
        ck("checksum", f.fcsOk);
        ck("firmware 4.35t", f.fwVersion == 35 && f.subVersion == 't');
        ckf("lat 48.2057", f.info.latE7 / 1e7f, 48.20567f, 0.0001f);
        ckf("lon 16.376", f.info.lonE7 / 1e7f, 16.376f, 0.0001f);
        ck("battery 87", f.batteryPct == 87);
        ck("altitude 560 ft = 170 m", f.info.hasAlt && f.info.altM == 170);
        char line[160]; MeshCom::summary(pk, line, sizeof line);
        ck("summary", strstr(line, "OE1KBC-12,DB0XYZ-12>*") && strstr(line, "bat 87%") && strstr(line, "Heltec V3"));
        pk.data[8] ^= 1;
        ck("a changed byte fails the checksum", MeshCom::parse(pk.data, pk.len, f) && !f.fcsOk);
        // An ACK.
        uint8_t a[12] = { 'A', 0x11, 0x22, 0x33, 0x44, 0x83, 0x78, 0x56, 0x34, 0x12, 0x01, 0x00 };
        memcpy(pk.data, a, 12); pk.len = 12;
        ck("an ACK", MeshCom::parse(pk.data, pk.len, f) && f.type == 'A' && f.ackFor == 0x12345678u && f.hopsLeft == 3);
    }

    suite("FANET tracking and name");
    {
        // type 1 from 11:A3F0, paraglider at 47.5 N 11.0 E, 1234 m, 36 km/h, +1.2 m/s, heading 90
        uint8_t d[16]; uint8_t n = 0;
        d[n++] = 0x01; d[n++] = 0x11; d[n++] = 0xF0; d[n++] = 0xA3;
        const int32_t lat = (int32_t)(47.5 * 93206), lon = (int32_t)(11.0 * 46603);
        d[n++] = (uint8_t)lat; d[n++] = (uint8_t)(lat >> 8); d[n++] = (uint8_t)(lat >> 16);
        d[n++] = (uint8_t)lon; d[n++] = (uint8_t)(lon >> 8); d[n++] = (uint8_t)(lon >> 16);
        const uint16_t ta = 0x8000 | (1 << 12) | 1234;
        d[n++] = (uint8_t)ta; d[n++] = (uint8_t)(ta >> 8);
        d[n++] = 72;              // 36 km/h in 0.5 km/h steps
        d[n++] = 12;              // +1.2 m/s in 0.1 steps
        d[n++] = 64;              // 90 degrees
        memset(&pk, 0, sizeof pk); memcpy(pk.data, d, n); pk.len = n; pk.flags = Lora::PK_CRC_OK | Lora::PK_CRC_PRESENT;
        Fanet::Frame f;
        ck("parses", Fanet::parse(pk.data, pk.len, f));
        ck("tracking from 11:A3F0", f.type == Fanet::T_TRACKING && f.manufacturer == 0x11 && f.id == 0xA3F0);
        ckf("lat 47.5", f.latE7 / 1e7f, 47.5f, 0.0001f);
        ckf("lon 11.0", f.lonE7 / 1e7f, 11.0f, 0.0001f);
        ck("paraglider, online, 1234 m", f.aircraft == 1 && f.online && f.altM == 1234);
        ck("36 km/h", f.speedKmh10 == 360);
        ck("+1.2 m/s", f.climbCms == 120);
        ck("heading 90", f.heading == 90);
        char line[160]; Fanet::summary(pk, line, sizeof line);
        ck("summary", strstr(line, "TRACKING 11:A3F0 FANET+") && strstr(line, "paraglider"));
        // A name, with the extended header and a unicast destination.
        uint8_t nm[24] = { 0x82, 0x07, 0x34, 0x12, 0x20, 0x11, 0xF0, 0xA3, 'D', 'H', '5', 'D', 'A', 'X' };
        memcpy(pk.data, nm, 14); pk.len = 14;
        ck("a unicast name", Fanet::parse(pk.data, pk.len, f) && f.type == Fanet::T_NAME && f.unicast && f.destId == 0xA3F0 && strcmp(f.text, "DH5DAX") == 0);
        // A climb of -0.5 m/s: 7-bit two's complement of -5 is 0x7B.
        d[13] = 0x7B; memcpy(pk.data, d, n); pk.len = n;
        ck("a negative climb", Fanet::parse(pk.data, pk.len, f) && f.climbCms == -50);
    }

    return report();
}
