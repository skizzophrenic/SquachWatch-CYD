// SquachWatch-CYD — the WiGLE file (src/wardrive_fmt.cpp) and the WiFi
// security it describes (src/wifi_auth.cpp).
//
// WiGLE rejects or misfiles what does not match its format, and nothing on the
// watch would ever say so. So the lines are checked whole, against the column
// list in https://api.wigle.net/csvFormat.html, and the security strings
// against beacons built the way a real access point sends them.
#include "wardrive.h"
#include "wifi_auth.h"
#include "test_util.h"
#include <cstdio>
#include <cstring>
#include <initializer_list>

static uint8_t ies[128];
static uint16_t ieLen;

static void add(uint8_t id, const uint8_t* body, uint8_t len) {
    ies[ieLen++] = id; ies[ieLen++] = len;
    memcpy(ies + ieLen, body, len); ieLen = (uint16_t)(ieLen + len);
}
static void ssid(const char* s) { add(0, (const uint8_t*)s, (uint8_t)strlen(s)); }

// RSN: version 1, group CCMP, the given pairwise ciphers and AKMs, caps 0.
static void rsn(std::initializer_list<uint8_t> pair, std::initializer_list<uint8_t> akm) {
    uint8_t b[64]; uint8_t n = 0;
    b[n++] = 1; b[n++] = 0;
    b[n++] = 0x00; b[n++] = 0x0F; b[n++] = 0xAC; b[n++] = 4;
    b[n++] = (uint8_t)pair.size(); b[n++] = 0;
    for (uint8_t c : pair) { b[n++] = 0x00; b[n++] = 0x0F; b[n++] = 0xAC; b[n++] = c; }
    b[n++] = (uint8_t)akm.size(); b[n++] = 0;
    for (uint8_t a : akm) { b[n++] = 0x00; b[n++] = 0x0F; b[n++] = 0xAC; b[n++] = a; }
    b[n++] = 0; b[n++] = 0;
    add(48, b, n);
}
static void wpa1() {
    const uint8_t b[] = { 0x00, 0x50, 0xF2, 0x01, 1, 0,  0x00, 0x50, 0xF2, 2,
                          1, 0, 0x00, 0x50, 0xF2, 2,  1, 0, 0x00, 0x50, 0xF2, 2 };
    add(221, b, sizeof b);
}
static const char* authOf(uint16_t cap) {
    static char s[96];
    WifiAuth::wigle(WifiAuth::parse(cap, ies, ieLen), s, sizeof s);
    return s;
}

int main() {
    const uint16_t ESS = 0x0001, PRIV = 0x0010;

    suite("Security from a beacon");
    ieLen = 0; ssid("Coffee");
    ck("open", !strcmp(authOf(ESS), "[ESS]"));
    ck("Privacy with no WPA or RSN is WEP", !strcmp(authOf(ESS | PRIV), "[WEP][ESS]"));
    ieLen = 0; ssid("Home"); rsn({4}, {2});
    ck("WPA2 personal", !strcmp(authOf(ESS | PRIV), "[WPA2-PSK-CCMP][ESS]"));
    ieLen = 0; ssid("Home"); rsn({4}, {2, 8});
    ck("WPA2/WPA3 transition", !strcmp(authOf(ESS | PRIV), "[WPA2-PSK-CCMP][WPA3-SAE-CCMP][ESS]"));
    ieLen = 0; ssid("Home"); rsn({4}, {8});
    ck("WPA3 only", !strcmp(authOf(ESS | PRIV), "[WPA3-SAE-CCMP][ESS]"));
    ieLen = 0; ssid("Corp"); rsn({4}, {1});
    ck("WPA2 enterprise", !strcmp(authOf(ESS | PRIV), "[WPA2-EAP-CCMP][ESS]"));
    ieLen = 0; ssid("Old"); wpa1(); rsn({2, 4}, {2});
    ck("WPA + WPA2 mixed, TKIP and CCMP", !strcmp(authOf(ESS | PRIV), "[WPA-PSK-TKIP+CCMP][WPA2-PSK-TKIP+CCMP][ESS]"));
    ieLen = 0; ssid("Cafe"); rsn({4}, {18});
    ck("Enhanced Open (OWE)", !strcmp(authOf(ESS), "[WPA3-OWE-CCMP][ESS]"));
    ieLen = 0; ssid("x"); ies[ieLen++] = 48; ies[ieLen++] = 200;
    ck("an element running off the frame is not read", !strcmp(authOf(ESS | PRIV), "[WEP][ESS]"));

    suite("Channels");
    ck("1 is 2412", Wardrive::channelMhz(1) == 2412);
    ck("13 is 2472", Wardrive::channelMhz(13) == 2472);
    ck("14 is 2484", Wardrive::channelMhz(14) == 2484);
    ck("0 is unknown", Wardrive::channelMhz(0) == 0);

    suite("Time");
    char t[24];
    Wardrive::utcStamp(1790557324u, t, sizeof t);
    ck("2026-09-28 01:02:04", !strcmp(t, "2026-09-28 01:02:04"));
    Wardrive::utcStamp(951868799u, t, sizeof t);
    ck("the last second of a leap day", !strcmp(t, "2000-02-29 23:59:59"));

    suite("The header");
    char h[512];
    Wardrive::headerLines(h, sizeof h, "1.25.0", "twatch-s3", "T-Watch S3 Plus", "LilyGo");
    const char* pre = "WigleWifi-1.6,appRelease=1.25.0,model=T-Watch S3 Plus,release=1.25.0,device=SquachWatch,";
    ck("pre-header", !strncmp(h, pre, strlen(pre)));
    ck("ends star=Sol,body=3,subBody=0", strstr(h, ",star=Sol,body=3,subBody=0\n") != nullptr);
    ck("columns, exactly", strstr(h, "\nMAC,SSID,AuthMode,FirstSeen,Channel,Frequency,RSSI,CurrentLatitude,CurrentLongitude,AltitudeMeters,AccuracyMeters,RCOIs,MfgrId,Type\n") != nullptr);

    suite("A WiFi row");
    Wardrive::Record r;
    memset(&r, 0, sizeof r);
    r.kind = Wardrive::KIND_WIFI;
    const uint8_t bssid[6] = { 0x1A, 0x9F, 0xEE, 0x5C, 0x71, 0xC6 };
    memcpy(r.mac, bssid, 6);
    r.rssi = -43; r.channel = 6;
    r.auth = WifiAuth::RSN | WifiAuth::PSK | WifiAuth::CCMP | WifiAuth::ESS | WifiAuth::PRIV;
    r.lat7 = 377657802; r.lon7 = -1234591943; r.altM = 67; r.accM = 3;
    r.epoch = 1790557324u;
    memcpy(r.name, "SquachNet", 9); r.nameLen = 9;
    char row[256];
    Wardrive::csvRow(r, row, sizeof row);
    ck("whole line", !strcmp(row, "1a:9f:ee:5c:71:c6,SquachNet,[WPA2-PSK-CCMP][ESS],2026-09-28 01:02:04,6,2437,-43,37.7657802,-123.4591943,67,3,,,WIFI\n"));

    suite("Names that would break a CSV");
    memcpy(r.name, "Bob's \"Net\", 5G", 15); r.nameLen = 15;
    Wardrive::csvRow(r, row, sizeof row);
    ck("quoted, quotes doubled", strstr(row, ",\"Bob's \"\"Net\"\", 5G\",") != nullptr);
    const char hidden[4] = { 0, 0, 0, 0 };
    memcpy(r.name, hidden, 4); r.nameLen = 4;
    Wardrive::csvRow(r, row, sizeof row);
    ck("a hidden SSID's zero bytes become an empty field", !strncmp(row, "1a:9f:ee:5c:71:c6,,[WPA2", 24));
    memcpy(r.name, "two\nlines", 9); r.nameLen = 9;
    Wardrive::csvRow(r, row, sizeof row);
    ck("a newline in a name cannot end the row", strchr(row, '\n') == row + strlen(row) - 1);

    suite("A Bluetooth row");
    memset(&r, 0, sizeof r);
    r.kind = Wardrive::KIND_BLE;
    const uint8_t mac[6] = { 0x5E, 0xC5, 0xC1, 0xCF, 0x45, 0xC4 };
    memcpy(r.mac, mac, 6);
    r.rssi = -80; r.flags = Wardrive::F_HAS_MFGR; r.auth = 76;   // Apple
    r.lat7 = -338500000; r.lon7 = 1512000000; r.altM = -3; r.accM = 12;
    r.epoch = 1790557324u;
    Wardrive::csvRow(r, row, sizeof row);
    ck("whole line", !strcmp(row, "5e:c5:c1:cf:45:c4,,Misc [LE],2026-09-28 01:02:04,0,,-80,-33.8500000,151.2000000,-3,12,,76,BLE\n"));
    r.flags = 0;
    Wardrive::csvRow(r, row, sizeof row);
    ck("no company ID: an empty MfgrId", strstr(row, ",12,,,BLE\n") != nullptr);

    return report();
}
