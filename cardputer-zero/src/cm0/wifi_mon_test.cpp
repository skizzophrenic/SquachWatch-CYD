// wifi_mon unit test -- drives the real parse -> filter -> queue -> pump
// path (wifiMonTestInject) with radiotapped 802.11 frames laid out the way
// the kernel lays them out, so the RX pipeline is proven without a monitor
// radio.
//
//   make test
//
// The radiotap headers below follow the radiotap.org field definitions
// (field number, alignment, size), NOT anything in wifi_mon.cpp: the
// previous version of this test was built from the parser's own wrong
// assumptions (rssi = bit 2, a 4-byte kHz frequency at bit 3, no alignment,
// no extended presence words) and passed while the parser was broken on
// every real frame. Header A is the shape mac80211 (ath9k_htc, mt76) emits:
// TSFT, flags with the FCS bit, rate, channel, combined antenna signal, RX
// flags, then one extra radiotap-namespace word per antenna chain.
#include "wifi_mon.h"
#include <net/if.h>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

static int g_failures = 0;
static int g_checks = 0;

// The 5 GHz bit table (stub esp_wifi.h, verbatim from ESP-IDF 5.5).
static_assert(BIT_NUMBER_TO_CHANNEL(1, WIFI_BAND_5G) == 36,  "bit 1 -> ch 36");
static_assert(BIT_NUMBER_TO_CHANNEL(8, WIFI_BAND_5G) == 64,  "bit 8 -> ch 64");
static_assert(BIT_NUMBER_TO_CHANNEL(9, WIFI_BAND_5G) == 100, "bit 9 -> ch 100");
static_assert(BIT_NUMBER_TO_CHANNEL(20, WIFI_BAND_5G) == 144, "bit 20 -> ch 144");
static_assert(BIT_NUMBER_TO_CHANNEL(21, WIFI_BAND_5G) == 149, "bit 21 -> ch 149");
static_assert(BIT_NUMBER_TO_CHANNEL(28, WIFI_BAND_5G) == 177, "bit 28 -> ch 177");
static_assert(CHANNEL_TO_BIT_NUMBER(36) == 1,  "ch 36 -> bit 1");
static_assert(CHANNEL_TO_BIT_NUMBER(177) == 28, "ch 177 -> bit 28");
static_assert(CHANNEL_TO_BIT_NUMBER(14) == 14, "ch 14 stays 2.4");

#define CHECK(cond, msg) do { \
    g_checks++; \
    if (!(cond)) { \
        std::printf("FAIL: %s  (%s:%d)\n", msg, __FILE__, __LINE__); \
        g_failures++; \
    } else { \
        std::printf("ok  : %s\n", msg); \
    } \
} while (0)

// What the most recent delivered callback saw. The buffer is freed after
// the callback returns, so copy everything out -- as detection.cpp does.
struct Cap {
    int  type = -1;
    int  rssi = 0;
    uint8_t channel = 0;
    uint16_t sig_len = 0;
    uint8_t first = 0;
    uint8_t mac[6] = {0};
    uint8_t tail[4] = {0};   // the 4 bytes at sig_len-4 (the FCS slot)
    int  calls = 0;
};
static Cap g_cap;

static void onPkt(void* buffer, wifi_promiscuous_pkt_type_t type) {
    wifi_promiscuous_pkt_t* p = (wifi_promiscuous_pkt_t*)buffer;
    g_cap.calls++;
    g_cap.type = (int)type;
    g_cap.rssi = p->rx_ctrl.rssi;
    g_cap.channel = p->rx_ctrl.channel;
    g_cap.sig_len = p->rx_ctrl.sig_len;
    g_cap.first = p->payload[0];
    std::memcpy(g_cap.mac, p->payload + 10, 6);
    std::memcpy(g_cap.tail, p->payload + p->rx_ctrl.sig_len - 4, 4);
}

// A little-endian byte builder; align() pads relative to the radiotap
// header's start, which is how radiotap defines field alignment.
struct B {
    std::vector<uint8_t> v;
    void u8(uint8_t x)   { v.push_back(x); }
    void s8(int x)       { v.push_back((uint8_t)(int8_t)x); }
    void le16(uint16_t x){ u8(x & 0xff); u8(x >> 8); }
    void le32(uint32_t x){ le16(x & 0xffff); le16(x >> 16); }
    void align(size_t a) { while (v.size() % a) u8(0); }
    void setLen()        { v[2] = (uint8_t)(v.size() & 0xff); v[3] = (uint8_t)(v.size() >> 8); }
};

// A 24-byte 802.11 header: FC, duration, addr1 (bcast), addr2 = mac,
// addr3. Then optionally a 4-byte FCS (0xDEADBEEF) as a driver that keeps
// it delivers.
static void frame80211(B& b, uint8_t fc, const uint8_t mac[6], bool fcs) {
    b.u8(fc); b.u8(0); b.u8(0); b.u8(0);
    for (int i = 0; i < 6; i++) b.u8(0xFF);
    for (int i = 0; i < 6; i++) b.u8(mac[i]);
    for (int i = 0; i < 6; i++) b.u8(0xAA);
    b.u8(0x10); b.u8(0x00);   // sequence control
    if (fcs) { b.u8(0xEF); b.u8(0xBE); b.u8(0xAD); b.u8(0xDE); }
}

// Header A: mac80211, two antenna chains, FCS kept.
//   word0 = TSFT|Flags|Rate|Channel|dBmSignal|RXflags|RadiotapNS|Ext
//   word1 = dBmSignal|Antenna|RadiotapNS|Ext     (chain 0)
//   word2 = dBmSignal|Antenna                    (chain 1)
static B headerA(uint16_t mhz, int rssi, uint8_t flags) {
    B b;
    b.u8(0); b.u8(0); b.le16(0);
    b.le32(0xA000402Fu);
    b.le32(0xA0000820u);
    b.le32(0x00000820u);
    b.align(8); b.le32(0x11111111); b.le32(0x22222222);   // TSFT @16
    b.u8(flags);                                          // Flags @24
    b.u8(0x0C);                                           // Rate @25 (6 Mb/s)
    b.align(2); b.le16(mhz); b.le16(0x00A0);              // Channel @26
    b.s8(rssi);                                           // combined @30
    b.align(2); b.le16(0);                                // RX flags @32
    b.s8(rssi - 2); b.u8(0);                              // chain 0 @34
    b.s8(rssi - 3); b.u8(1);                              // chain 1 @36
    b.setLen();
    return b;
}

// Header B: Flags|Channel|dBmSignal, one word -- a pad byte before the
// channel field (alignment 2 after the 1-byte flags).
static B headerB(uint16_t mhz, int rssi) {
    B b;
    b.u8(0); b.u8(0); b.le16(0);
    b.le32(0x0000002Au);
    b.u8(0x00);                                   // Flags @8
    b.align(2); b.le16(mhz); b.le16(0x00A0);      // Channel @10
    b.s8(rssi);                                   // signal @14
    b.setLen();
    return b;
}

// A beacon (fc 0x80) / probe response (0x50) per 802.11: 24-byte header
// with addr2 = addr3 = BSSID, then Timestamp(8) Interval(2) Capability(2),
// then IEs. `ssidLen` < 0 = no SSID IE; `ds` 0 = no DS Parameter Set IE.
static void apFrame(B& b, uint8_t fc, const uint8_t bssid[6], const char* ssid,
                    int ssidLen, uint16_t cap, uint8_t ds) {
    b.u8(fc); b.u8(0); b.u8(0); b.u8(0);
    for (int i = 0; i < 6; i++) b.u8(0xFF);
    for (int i = 0; i < 6; i++) b.u8(bssid[i]);
    for (int i = 0; i < 6; i++) b.u8(bssid[i]);
    b.u8(0x20); b.u8(0x00);
    for (int i = 0; i < 8; i++) b.u8(0x11);      // timestamp
    b.le16(100);                                  // beacon interval
    b.le16(cap);
    if (ssidLen >= 0) {
        b.u8(0); b.u8((uint8_t)ssidLen);
        for (int i = 0; i < ssidLen; i++) b.u8(ssid ? (uint8_t)ssid[i] : 0);
    }
    b.u8(1); b.u8(2); b.u8(0x82); b.u8(0x84);     // Supported Rates
    if (ds) { b.u8(3); b.u8(1); b.u8(ds); }
}

static int scanFeed(uint16_t mhz, int rssi, uint8_t fc, const uint8_t bssid[6],
                    const char* ssid, int ssidLen, uint16_t cap, uint8_t ds) {
    B b = headerB(mhz, rssi);
    apFrame(b, fc, bssid, ssid, ssidLen, cap, ds);
    return cm0::wifiMonTestScanAbsorb(b.v.data(), b.v.size());
}

static int inject(const B& b, uint8_t applied) {
    g_cap.calls = 0;
    return cm0::wifiMonTestInject(b.v.data(), b.v.size(), applied);
}

int main() {
    cm0::wifiMonRegisterRx(onPkt);
    // The firmware's boot filter: MGMT and DATA in, CTL out.
    cm0::wifiMonSetFilter(WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA);
    const uint8_t mac[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};

    // -- 1: header A (mac80211, extended presence words, TSFT alignment,
    //       FCS kept by the driver), a beacon on ch 6 at -42 dBm.
    {
        B b = headerA(2437, -42, 0x10 /*FCS at end*/);
        frame80211(b, 0x80, mac, true);
        const int r = inject(b, 1);
        CHECK(r == 0, "A: beacon delivered as mgmt");
        CHECK(g_cap.calls == 1, "A: callback fired once");
        CHECK(g_cap.rssi == -42, "A: rssi = the combined dBm signal (-42), not a chain");
        CHECK(g_cap.channel == 6, "A: channel from the radiotap channel field (2437 MHz = 6)");
        CHECK(g_cap.sig_len == 24 + 4, "A: driver FCS stripped, ESP-style 4 counted once");
        CHECK(std::memcmp(g_cap.tail, "\0\0\0\0", 4) == 0, "A: FCS slot is the zero pad, not the driver FCS");
        CHECK(std::memcmp(g_cap.mac, mac, 6) == 0, "A: transmitter MAC at +10 round-trips");
        CHECK(g_cap.first == 0x80, "A: FC byte passes through unmodified (0x80)");
    }
    // -- 2: header A without the FCS flag: the same frame bytes minus FCS.
    {
        B b = headerA(2412, -77, 0x00);
        frame80211(b, 0x40 /*probe req*/, mac, false);
        const int r = inject(b, 9);
        CHECK(r == 0, "A/noFCS: probe-req delivered");
        CHECK(g_cap.rssi == -77, "A/noFCS: rssi -77");
        CHECK(g_cap.channel == 1, "A/noFCS: ch 1 from radiotap, not the applied 9");
        CHECK(g_cap.sig_len == 24 + 4, "A/noFCS: sig_len = frame + 4");
    }
    // -- 3: bad-FCS flag: dropped.
    {
        B b = headerA(2412, -50, 0x10 | 0x40);
        frame80211(b, 0x80, mac, true);
        CHECK(inject(b, 1) == -1 && g_cap.calls == 0, "bad-FCS frame dropped");
    }
    // -- 4: header B (alignment pad before the channel field), QoS data.
    {
        B b = headerB(2462, -63);
        frame80211(b, 0x88 /*QoS data*/, mac, false);
        const int r = inject(b, 1);
        CHECK(r == 2 && g_cap.type == WIFI_PROMISC_RX_DATA, "B: data frame delivered as DATA");
        CHECK(g_cap.rssi == -63, "B: rssi -63 read after the alignment pad");
        CHECK(g_cap.channel == 11, "B: 2462 MHz = ch 11");
    }
    // -- 5: a vendor namespace between the channel and the signal: the
    //       walk must skip the vendor data by its skip length.
    //   word0 = Channel|VendorNS|Ext, word1 = (vendor bit 0)|RadiotapNS|Ext,
    //   word2 = dBmSignal
    {
        B b;
        b.u8(0); b.u8(0); b.le16(0);
        b.le32(0xC0000008u);
        b.le32(0xA0000001u);
        b.le32(0x00000020u);
        b.align(2); b.le16(2462); b.le16(0x00A0);   // Channel @16
        b.align(2); b.u8(0x00); b.u8(0x11); b.u8(0x22); b.u8(0x01); b.le16(3);   // vendor hdr @20
        b.u8(0xEE); b.u8(0xEE); b.u8(0xEE);         // 3 vendor bytes
        b.s8(-66);                                  // signal @29
        b.setLen();
        frame80211(b, 0x40, mac, false);
        const int r = inject(b, 1);
        CHECK(r == 0, "vendor-ns: delivered");
        CHECK(g_cap.rssi == -66, "vendor-ns: signal found after skipping vendor data");
        CHECK(g_cap.channel == 11, "vendor-ns: channel 11");
    }
    // -- 6: no signal field: the floor, never 0 dBm ("right next to you").
    {
        B b;
        b.u8(0); b.u8(0); b.le16(0);
        b.le32(0x00000008u);
        b.le16(2412); b.le16(0x00A0);
        b.setLen();
        frame80211(b, 0x40, mac, false);
        CHECK(inject(b, 1) == 0 && g_cap.rssi == -100, "no signal field -> rssi -100");
    }
    // -- 7: control frames are filtered out (CTL not in the mask).
    {
        B b = headerB(2412, -70);
        frame80211(b, 0xC4 /*CTS*/, mac, false);
        CHECK(inject(b, 1) == -1 && g_cap.calls == 0, "control frame dropped (CTL not in mask)");
    }
    // -- 8: malformed headers.
    {
        uint8_t f[40];
        std::memset(f, 0, sizeof f);
        f[2] = 5;   // length 5 < 8
        CHECK(cm0::wifiMonTestInject(f, sizeof f, 1) == -1, "radiotap length < 8 rejected");
        std::memset(f, 0, sizeof f);
        f[2] = 8; f[4] = 0; f[5] = 0; f[6] = 0; f[7] = 0x80;   // Ext, but no room for word 2
        CHECK(cm0::wifiMonTestInject(f, sizeof f, 1) == -1, "presence words past the header rejected");
        std::memset(f, 0, sizeof f);
        f[2] = 200;  // longer than the packet
        CHECK(cm0::wifiMonTestInject(f, sizeof f, 1) == -1, "header longer than the packet rejected");
        B b = headerB(2412, -50);
        b.u8(0x80); b.u8(0);   // a 2-byte "frame"
        CHECK(inject(b, 1) == -1, "frame shorter than an 802.11 header rejected");
    }
    // -- 9: the channel plan, through the radiotap field.
    {
        struct { uint16_t mhz; uint8_t want; const char* what; } cases[] = {
            {2484, 14,  "2484 MHz = ch 14"},
            {5180, 36,  "5180 MHz = ch 36"},  {5320, 64,  "5320 MHz = ch 64"},
            {5490, 100, "5490 MHz = ch 100"}, {5710, 144, "5710 MHz = ch 144"},
            {5745, 149, "5745 MHz = ch 149"}, {5885, 177, "5885 MHz = ch 177"},
            {5400, 7,   "5400 MHz (gap) -> applied 7"},
            {5955, 7,   "5955 MHz (6 GHz ch 1) -> applied 7, never 2.4 ch 1"},
        };
        for (auto& c : cases) {
            B b = headerB(c.mhz, -40);
            frame80211(b, 0x40, mac, false);
            CHECK(inject(b, 7) == 0 && g_cap.channel == c.want, c.what);
        }
    }
    // -- 10: `iw phy info` parsing for the regulatory channel list.
    {
        const char* info =
            "Wiphy phy1\n"
            "\tBand 1:\n"
            "\t\tFrequencies:\n"
            "\t\t\t* 2412.0 MHz [1] (20.0 dBm)\n"
            "\t\t\t* 2467.0 MHz [12] (disabled)\n"
            "\t\t\t* 2472 MHz [13] (disabled)\n"
            "\tBand 2:\n"
            "\t\tFrequencies:\n"
            "\t\t\t* 5180.0 MHz [36] (23.0 dBm)\n"
            "\t\t\t* 5260.0 MHz [52] (20.0 dBm) (no IR, radar detection)\n"
            "\t\t\t* 5865.0 MHz [173] (disabled)\n"
            "\tBand 4:\n"
            "\t\tFrequencies:\n"
            "\t\t\t* 6015.0 MHz [13] (20.0 dBm)\n"
            "\tSupported commands:\n"
            "\t\t * new_interface\n";
        bool u[256];
        cm0::wifiMonTestParsePhyInfo(info, u);
        CHECK(u[1], "phy: ch 1 usable");
        CHECK(!u[12] && !u[13], "phy: disabled 12/13 not usable (and 6 GHz [13] does not count)");
        CHECK(u[36], "phy: ch 36 usable");
        CHECK(u[52], "phy: radar/no-IR ch 52 usable for monitor RX");
        CHECK(!u[173], "phy: disabled ch 173 not usable");
        CHECK(!u[40], "phy: unlisted ch 40 not usable");
    }
    // -- 11: the nl80211 SET_WIPHY wire format. Literal values from
    //        <linux/nl80211.h> / <linux/netlink.h> on purpose -- the first
    //        hand-rolled version of this message had IFINDEX=2, FREQ=7, no
    //        genlmsghdr and a kHz frequency.
    {
        uint8_t m[64];
        const size_t n = cm0::wifiMonTestBuildSetFreq(m, sizeof m, 0x1C, 5, 2437);
        auto le16 = [&](size_t o) { return (unsigned)(m[o] | (m[o+1] << 8)); };
        auto le32 = [&](size_t o) { return (unsigned)(m[o] | (m[o+1] << 8) | (m[o+2] << 16) | ((unsigned)m[o+3] << 24)); };
        CHECK(n == 44 && le32(0) == 44, "nl: 16 nlmsghdr + 4 genlmsghdr + 3x8 attrs = 44");
        CHECK(le16(4) == 0x1C, "nl: nlmsg_type = the resolved nl80211 family id");
        CHECK(le16(6) == (0x01 | 0x04), "nl: NLM_F_REQUEST | NLM_F_ACK");
        CHECK(m[16] == 2, "nl: genl cmd = NL80211_CMD_SET_WIPHY (2)");
        CHECK(le16(20) == 8 && le16(22) == 3 && le32(24) == 5, "nl: attr NL80211_ATTR_IFINDEX (3) = 5");
        CHECK(le16(28) == 8 && le16(30) == 38 && le32(32) == 2437, "nl: attr NL80211_ATTR_WIPHY_FREQ (38) = 2437 MHz");
        CHECK(le16(36) == 8 && le16(38) == 39 && le32(40) == 0, "nl: attr WIPHY_CHANNEL_TYPE (39) = NO_HT");
    }
    // -- 12: a REAL header, captured on the device (EDUP EP-AX1672,
    //        mt7921u, kernel 6.18, 2026-11-19, tcpdump on sqwmon0). Verbatim
    //        56 bytes: header A's layout plus bit 22 (12-byte timestamp,
    //        8-aligned) between RX flags and the per-chain words. tshark
    //        decoded it as 2412 MHz, no FCS, dBm -78 / -86 / -78 (combined,
    //        chain 0, chain 1). The 802.11 frame after it is synthetic.
    {
        static const uint8_t rt[56] = {
            0x00, 0x00, 0x38, 0x00, 0x2f, 0x40, 0x40, 0xa0, 0x20, 0x08, 0x00, 0xa0,
            0x20, 0x08, 0x00, 0x00, 0x0d, 0xd3, 0x7a, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x02, 0x6c, 0x09, 0xa0, 0x00, 0xb2, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x0d, 0xd3, 0x7a, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x01, 0x01, 0xaa, 0x00, 0xb2, 0x01,
        };
        B b;
        for (uint8_t x : rt) b.u8(x);
        frame80211(b, 0x80, mac, false);
        const int r = inject(b, 11);
        CHECK(r == 0 && g_cap.calls == 1, "real mt7921u: beacon delivered as mgmt");
        CHECK(g_cap.rssi == -78, "real mt7921u: rssi = combined -78, not chain 0's -86");
        CHECK(g_cap.channel == 1, "real mt7921u: ch 1 from 2412 MHz, not the applied 11");
        CHECK(g_cap.first == 0x80, "real mt7921u: frame starts right after the 56-byte header");
        CHECK(std::memcmp(g_cap.mac, mac, 6) == 0, "real mt7921u: transmitter MAC round-trips");
        CHECK(g_cap.sig_len == 24 + 4, "real mt7921u: no FCS flag -> nothing stripped");

        B s;
        for (uint8_t x : rt) s.u8(x);
        const uint8_t ap[6] = {0x44, 0x44, 0x44, 0x44, 0x44, 0x44};
        apFrame(s, 0x80, ap, "Real", 4, 0x0011, 0);
        CHECK(cm0::wifiMonTestScanAbsorb(s.v.data(), s.v.size()) == 1,
              "real mt7921u: scan table takes the beacon");
        cm0::wifiMonScanClear();
    }

    // ---- the passive scan's AP table (the raw Wi-Fi screen) ----
    {
        const uint8_t home[6]  = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60};
        const uint8_t five[6]  = {0x10, 0x20, 0x30, 0x40, 0x50, 0x61};
        const uint8_t other[6] = {0x22, 0x22, 0x22, 0x22, 0x22, 0x22};
        const uint8_t hid[6]   = {0x33, 0x33, 0x33, 0x33, 0x33, 0x33};
        // Capability: ESS (0x0001), +Privacy (0x0010) = protected.
        CHECK(scanFeed(2437, -50, 0x80, home, "HomeNet", 7, 0x0011, 6) == 1,
              "scan: beacon adds an AP");
        CHECK(scanFeed(2437, -40, 0x80, home, "HomeNet", 7, 0x0011, 6) == 1,
              "scan: same BSSID again refreshes, no duplicate");
        CHECK(scanFeed(5180, -60, 0x80, five, "Home5", 5, 0x0001, 0) == 2,
              "scan: 5 GHz beacon without DS IE is added");
        CHECK(scanFeed(2412, -30, 0x80, hid, nullptr, 0, 0x0011, 1) == 0,
              "scan: hidden (empty SSID) skipped");
        CHECK(scanFeed(2412, -30, 0x80, hid, nullptr, 5, 0x0011, 1) == 0,
              "scan: hidden (all-NUL SSID) skipped");
        CHECK(scanFeed(2412, -30, 0x40, hid, "Probe", 5, 0x0011, 1) == 0,
              "scan: probe request is not an AP");
        CHECK(scanFeed(2462, -70, 0x50, other, "Other", 5, 0x0001, 11) == 3,
              "scan: probe response adds an AP");
        // A truncated IE (length runs past the frame) must not read past it.
        {
            B b = headerB(2437, -55);
            apFrame(b, 0x80, hid, "X", 1, 0x0001, 0);
            b.u8(0xDD); b.u8(200); b.u8(0x00);
            CHECK(cm0::wifiMonTestScanAbsorb(b.v.data(), b.v.size()) == 4,
                  "scan: truncated trailing IE tolerated (SSID before it kept)");
        }
        cm0::wifiMonTestScanFinish();
        CHECK(cm0::wifiMonScanState() == 4, "scan: state = 4 networks once finished");
        cm0::WifiScanAp r[8];
        const size_t n = cm0::wifiMonScanResults(r, 8);
        CHECK(n == 4, "scan: 4 results");
        CHECK(n >= 1 && std::strcmp(r[0].ssid, "HomeNet") == 0 && r[0].rssi == -40,
              "scan: strongest first, strongest RSSI kept (-40 over -50)");
        CHECK(n >= 1 && r[0].channel == 6 && r[0].privacy, "scan: HomeNet ch 6 (DS IE), protected");
        CHECK(n >= 1 && std::memcmp(r[0].bssid, home, 6) == 0, "scan: BSSID from addr3");
        bool sawFive = false, sawOther = false;
        for (size_t i = 0; i < n; i++) {
            if (!std::strcmp(r[i].ssid, "Home5"))
                sawFive = r[i].channel == 36 && !r[i].privacy && r[i].rssi == -60;
            if (!std::strcmp(r[i].ssid, "Other"))
                sawOther = r[i].channel == 11 && r[i].rssi == -70;
        }
        CHECK(sawFive, "scan: Home5 ch 36 from radiotap 5180 MHz, open, -60");
        CHECK(sawOther, "scan: Other ch 11 (DS IE), -70");
        CHECK(n >= 4 && r[3].rssi == -70, "scan: weakest last");
        cm0::wifiMonScanClear();
        CHECK(cm0::wifiMonScanState() == -2, "scan: clear -> not started (-2)");
        CHECK(!cm0::wifiMonScanStart(150), "scan: start without a sniffer is refused");
    }
    // -- 14: adapter loss (2026-11-22). The USB adapter dropped off the bus
    //        mid-run; sqwmon0 died with it and the feed thread kept polling
    //        a dead socket for the rest of the session. Liveness = the
    //        bound name still resolves to the bound ifindex (a re-created
    //        vif gets a new index, so a stale bind counts as lost).
    {
        const int lo = (int)if_nametoindex("lo");
        CHECK(lo > 0 && cm0::wifiMonTestLinkAlive("lo", lo), "link: lo at its own index is alive");
        CHECK(!cm0::wifiMonTestLinkAlive("lo", lo + 1000), "link: same name, other index = lost (re-created)");
        CHECK(!cm0::wifiMonTestLinkAlive("sqwgone9", 5), "link: vanished interface = lost");
        CHECK(!cm0::wifiMonTestLinkAlive("", 5), "link: no interface = lost");
        CHECK(cm0::wifiMonTestLossErrno(ENETDOWN) && cm0::wifiMonTestLossErrno(ENODEV) &&
              cm0::wifiMonTestLossErrno(ENXIO), "link: ENETDOWN/ENODEV/ENXIO from recv mean check now");
        CHECK(!cm0::wifiMonTestLossErrno(EAGAIN) && !cm0::wifiMonTestLossErrno(EINTR),
              "link: EAGAIN/EINTR are normal");
    }
    // -- 15: a refused channel the wiphy lists as (disabled) is the
    //        regulatory domain (US: 12/13), not the driver. The old log
    //        called it a driver refusal and sent a diagnosis the wrong way.
    {
        bool u[256];
        std::memset(u, 0, sizeof u);
        for (int c = 1; c <= 11; c++) u[c] = true;
        u[36] = true;
        CHECK(cm0::wifiMonTestRegulatoryBlocked(12, true, u), "reg: ch 12 not in the phy list = regulatory");
        CHECK(!cm0::wifiMonTestRegulatoryBlocked(6, true, u), "reg: ch 6 listed = a real driver refusal");
        CHECK(!cm0::wifiMonTestRegulatoryBlocked(12, false, u), "reg: phy not probed yet = cannot say");
    }

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
