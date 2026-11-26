// SquachWatch-CM0 -- ble_scan unit test.
//
// Every expected byte below is written from the Bluetooth Core spec /
// BlueZ ABI by hand, not from ble_scan.cpp: the socket filter, the two LE
// commands, and LE Advertising Report events (Core Vol 4 Part E §7.7.65.2).
#include "ble_scan.h"

#include <cstdio>
#include <cstring>
#include <vector>

static int g_failures = 0;
static int g_checks = 0;
#define CHECK(cond, msg) do { \
    g_checks++; \
    if (cond) std::printf("ok  : %s\n", msg); \
    else { std::printf("FAIL: %s\n", msg); g_failures++; } \
} while (0)

static bool bytesEq(const uint8_t* a, const std::vector<uint8_t>& b, size_t n) {
    return n == b.size() && std::memcmp(a, b.data(), n) == 0;
}

static std::vector<cm0::BleAdvRec> drain() {
    std::vector<cm0::BleAdvRec> v;
    cm0::bleScanDrain(v, 64);
    return v;
}

int main() {
    uint8_t out[64];
    size_t n;

    // ---- socket filters (struct hci_filter: u32 type_mask, u32 event_mask[2], u16 opcode)
    n = cm0::bleScanTestFilter(out, true);
    // type_mask: HCI_EVENT_PKT (4) -> 0x10. event 0x3E (62) -> mask[1] bit 30.
    CHECK(bytesEq(out, {0x10,0,0,0, 0,0,0,0, 0,0,0,0x40, 0,0}, n),
          "filter (reports): events only, LE Meta 0x3E = event_mask[1] bit 30");
    n = cm0::bleScanTestFilter(out, false);
    // 0x0E (14) and 0x0F (15) -> mask[0] = 0x0000C000.
    CHECK(bytesEq(out, {0x10,0,0,0, 0x00,0xC0,0,0, 0,0,0,0, 0,0}, n),
          "filter (commands): Command Complete 0x0E + Status 0x0F");

    // ---- LE Set Scan Parameters, opcode 0x200B (OGF 8, OCF 0x0B), 7 params
    n = cm0::bleScanTestBuildParams(out, false, 100, 75);
    // 100 ms = 160 units (0x00A0), 75 ms = 120 (0x0078); public addr; accept all.
    CHECK(bytesEq(out, {0x01, 0x0B,0x20, 0x07, 0x00, 0xA0,0x00, 0x78,0x00, 0x00, 0x00}, n),
          "params: passive 100/75 ms (the firmware's init) wire bytes");
    n = cm0::bleScanTestBuildParams(out, true, 100, 75);
    CHECK(n == 11 && out[4] == 0x01, "params: active scan type = 1");
    n = cm0::bleScanTestBuildParams(out, false, 50, 100);
    CHECK(n == 11 && out[5] == 0x50 && out[7] == 0x50, "params: window clamped to interval (80 units)");
    n = cm0::bleScanTestBuildParams(out, false, 1, 1);
    CHECK(n == 11 && out[5] == 0x04 && out[6] == 0x00, "params: interval floored at 0x0004");

    // ---- LE Set Scan Enable, opcode 0x200C, 2 params
    n = cm0::bleScanTestBuildEnable(out, true, false);
    CHECK(bytesEq(out, {0x01, 0x0C,0x20, 0x02, 0x01, 0x00}, n), "enable: on, duplicates kept");
    n = cm0::bleScanTestBuildEnable(out, false, false);
    CHECK(bytesEq(out, {0x01, 0x0C,0x20, 0x02, 0x00, 0x00}, n), "enable: off");

    // ---- Read_BD_ADDR, opcode 0x1009 (OGF 4, OCF 9), no parameters
    n = cm0::bleScanTestBuildReadBdAddr(out);
    CHECK(bytesEq(out, {0x01, 0x09,0x10, 0x00}, n), "read bd_addr: 0x1009, no params");

    // ---- LE Set Advertising Parameters, 0x2006, 15 params. The firmware's
    // idle squad cadence: 1500 ms * 8/5 = 2400 (0x0960), +16 = 0x0970.
    n = cm0::bleScanTestBuildAdvParams(out, 2400, 2416);
    CHECK(bytesEq(out, {0x01, 0x06,0x20, 0x0F, 0x60,0x09, 0x70,0x09, 0x03, 0x00,
                        0x00, 0,0,0,0,0,0, 0x07, 0x00}, n),
          "adv params: 2400/2416 units, ADV_NONCONN_IND, public, ch 37-39");
    n = cm0::bleScanTestBuildAdvParams(out, 0x10, 0x05);
    CHECK(n == 19 && out[4] == 0x20 && out[6] == 0x20, "adv params: clamped to 0x0020, max >= min");

    // ---- LE Set Advertising Data, 0x2008: length + 31 bytes, zero-padded
    {
        const uint8_t ad[] = { 0x05, 0xFF, 0xE5, 0x02, 0xAA, 0xBB };
        n = cm0::bleScanTestBuildAdvData(out, ad, sizeof ad);
        std::vector<uint8_t> want = { 0x01, 0x08,0x20, 0x20, 0x06,
                                      0x05, 0xFF, 0xE5, 0x02, 0xAA, 0xBB };
        while (want.size() < 36) want.push_back(0);
        CHECK(bytesEq(out, want, n), "adv data: 0x2008, len 6 + 25 zero pad (32 params)");
    }
    n = cm0::bleScanTestBuildAdvEnable(out, true);
    CHECK(bytesEq(out, {0x01, 0x0A,0x20, 0x01, 0x01}, n), "adv enable: on");
    n = cm0::bleScanTestBuildAdvEnable(out, false);
    CHECK(bytesEq(out, {0x01, 0x0A,0x20, 0x01, 0x00}, n), "adv enable: off");

    // ---- LE Advertising Report events
    {
        // ADV_IND, random address 4E:1D:00:77:28:BE (wire LSB first), AD:
        // Flags(02 01 06) + Complete Local Name "MELK" (05 09 'M''E''L''K'),
        // RSSI -61 (0xC3).
        const std::vector<uint8_t> pkt = {
            0x04, 0x3E, 0x15,                 // event, LE Meta, plen 21
            0x02, 0x01,                       // Adv Report, 1 report
            0x00, 0x01,                       // ADV_IND, random
            0xBE,0x28,0x77,0x00,0x1D,0x4E,    // address, LSB first
            0x09,                             // data length
            0x02,0x01,0x06, 0x05,0x09,'M','E','L','K',
            0xC3,                             // RSSI -61
        };
        CHECK(cm0::bleScanTestParse(pkt.data(), pkt.size()) == 1, "report: one queued");
        auto v = drain();
        CHECK(v.size() == 1, "report: drained one");
        if (v.size() == 1) {
            const uint8_t addr[6] = {0xBE,0x28,0x77,0x00,0x1D,0x4E};
            CHECK(std::memcmp(v[0].addr, addr, 6) == 0, "report: address kept LSB first (NimBLE val order)");
            CHECK(v[0].addrType == 1 && v[0].evtType == 0, "report: random / ADV_IND");
            CHECK(v[0].rssi == -61, "report: RSSI -61 (signed, after the data)");
            CHECK(v[0].len == 9 && v[0].data[4] == 0x09 && v[0].data[5] == 'M', "report: AD data intact");
        }
    }
    {
        // Two reports in one event: a NONCONN with no data and RSSI 127
        // ("not available"), then a SCAN_RSP with 3 bytes.
        const std::vector<uint8_t> pkt = {
            0x04, 0x3E, 0x19,
            0x02, 0x02,
            0x03, 0x00, 1,2,3,4,5,6, 0x00, 0x7F,
            0x04, 0x00, 6,5,4,3,2,1, 0x03, 0x02,0x0A,0x00, 0xB0,
        };
        CHECK(cm0::bleScanTestParse(pkt.data(), pkt.size()) == 2, "two reports: both queued");
        auto v = drain();
        CHECK(v.size() == 2 && v[0].evtType == 3 && v[0].len == 0 && v[0].rssi == -127,
              "two reports: NONCONN, empty, RSSI 127 -> -127");
        CHECK(v.size() == 2 && v[1].evtType == 4 && v[1].len == 3 && v[1].rssi == -80,
              "two reports: SCAN_RSP, 3 bytes, -80");
    }
    {
        const std::vector<uint8_t> trunc = { 0x04, 0x3E, 0x0C, 0x02, 0x01, 0x00, 0x00,
                                             1,2,3,4,5,6, 0x1F, 0x02 };   // len 31, no data
        CHECK(cm0::bleScanTestParse(trunc.data(), trunc.size()) == -1, "truncated report rejected");
        const std::vector<uint8_t> big = { 0x04, 0x3E, 0x0B, 0x02, 0x01, 0x00, 0x00,
                                           1,2,3,4,5,6, 0x20 };          // len 32 > 31
        CHECK(cm0::bleScanTestParse(big.data(), big.size()) == -1, "data length > 31 rejected");
        const std::vector<uint8_t> ext = { 0x04, 0x3E, 0x03, 0x0D, 0x01, 0x00 };
        CHECK(cm0::bleScanTestParse(ext.data(), ext.size()) == -1, "extended report (0x0D) not parsed as legacy");
        const std::vector<uint8_t> cc = { 0x04, 0x0E, 0x04, 0x01, 0x0C, 0x20, 0x00 };
        CHECK(cm0::bleScanTestParse(cc.data(), cc.size()) == -1, "Command Complete is not a report");
        const std::vector<uint8_t> shortPlen = { 0x04, 0x3E, 0x40, 0x02, 0x01 };
        CHECK(cm0::bleScanTestParse(shortPlen.data(), shortPlen.size()) == -1, "plen past the packet rejected");
        CHECK(drain().empty(), "nothing queued by the rejects");
    }

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
