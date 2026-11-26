// squachd --selftest: the detection path end to end, headless.
//
// The unit tests (wifi_mon_test, ble_scan_test) stop at the edge of the
// port: bytes in, ESP/NimBLE-shaped callbacks out. This drives the REAL
// firmware behind them -- setup(), the promiscuous callback and
// BleScanCallbacks in detection.cpp, DetectionEngine's queues, signature
// tables and deauth burst logic -- with synthetic radio input, and checks
// what lands in the engine's log. Virtual time; no display, no keyboard.
//
// Radio input goes in through the same test hooks the unit tests use
// (wifiMonTestInject: radiotap parse -> filter -> queue -> pump on this
// thread; bleScanTestParse: an HCI LE Advertising Report into the feed
// queue, delivered by blePump()). No sniffer is started. The BLE scan does
// try hci0 (NimBLEScan::start), so on the device under sudo real adverts
// may also arrive: every check is keyed on this test's own addresses, and
// counts are compared as deltas.
#include <cstdio>
#include <cstring>
#include <vector>

#include "state.h"
#include "detection.h"
#include "wifi_mon.h"
#include "ble_scan.h"
#include "NimBLEAdvertisedDevice.h"
#include "NimBLEUUID.h"
#include "NimBLEDevice.h"
#include "NimBLEAdvertising.h"
#include "settings.h"
#include "theme.h"
#include "ota_core.h"
#include "ota_ble.h"
#include "ota_wifi.h"

extern DetectionEngine engine;

#include "coming_soon.h"

namespace {

int s_checks = 0, s_failures = 0;
void check(bool ok, const char* what) {
    s_checks++;
    if (!ok) s_failures++;
    std::printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
}

void (*s_step)() = nullptr;
const uint32_t STEP_MS = 33;

// Run the loop for `ms` of virtual time.
void advance(uint32_t ms) {
    for (uint32_t t = 0; t < ms; t += STEP_MS) {
        SimClock::nowMs += STEP_MS;
        s_step();
    }
}

// A log row for this exact (printed-order) address and type, or null.
const Detection* findRow(const uint8_t mac[6], DetectionType t) {
    for (uint8_t i = 0; i < engine.logCount(); i++) {
        const Detection* d = engine.logAt(i);
        if (d && d->type == t && std::memcmp(d->mac, mac, 6) == 0) return d;
    }
    return nullptr;
}

// ------------------------------------------------------------------ Wi-Fi

// Radiotap: Flags | Channel | dBm signal, one presence word (as the unit
// test's header B), no FCS.
void radiotap(std::vector<uint8_t>& b, uint16_t mhz, int8_t rssi) {
    const uint8_t h[] = { 0x00, 0x00, 15, 0x00, 0x2A, 0x00, 0x00, 0x00,
                          0x00,                                   // Flags
                          0x00,                                   // pad
                          (uint8_t)(mhz & 0xFF), (uint8_t)(mhz >> 8), 0xA0, 0x00,
                          (uint8_t)rssi };
    b.assign(h, h + sizeof h);
}

// A deauthentication (subtype 12, fc 0xC0) or disassociation (10, 0xA0)
// from `ta`, to broadcast or to a client, reason 7.
int deauth(const uint8_t ta[6], bool bcast, bool disassoc = false) {
    std::vector<uint8_t> b;
    radiotap(b, 2437, -48);
    b.push_back(disassoc ? 0xA0 : 0xC0); b.push_back(0x00);
    b.push_back(0x3A); b.push_back(0x01);                     // duration
    const uint8_t client[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    for (int i = 0; i < 6; i++) b.push_back(bcast ? 0xFF : client[i]);
    for (int i = 0; i < 6; i++) b.push_back(ta[i]);
    for (int i = 0; i < 6; i++) b.push_back(ta[i]);
    b.push_back(0x10); b.push_back(0x00);                     // sequence
    b.push_back(0x07); b.push_back(0x00);                     // reason
    return cm0::wifiMonTestInject(b.data(), b.size(), 6);
}

void testDeauth() {
    const uint8_t a[6] = {0xDE, 0xAD, 0x00, 0x00, 0x00, 0x01};
    const uint8_t u[6] = {0xDE, 0xAD, 0x00, 0x00, 0x00, 0x02};
    const uint8_t q[6] = {0xDE, 0xAD, 0x00, 0x00, 0x00, 0x03};
    const uint16_t base = engine.countByType(DetectionType::DEAUTH);
    // The engine's 15 s cooldown is global and starts from millis() 0, so
    // nothing can fire in the first 15 s after start (the firmware's own
    // behaviour after a boot). Get past it.
    advance(16000);

    // One isolated deauth is ordinary Wi-Fi: nothing.
    check(deauth(q, true) == 0, "deauth: frame delivered to the firmware as mgmt");
    advance(5000);
    check(!findRow(q, DetectionType::DEAUTH), "deauth: one frame is not a flood");

    // Broadcast: DEAUTH_BCAST (6) within the window fires; 5 does not. The
    // queue holds 8, so a step between frames drains it as loop() would.
    for (int i = 0; i < 5; i++) { deauth(a, true); advance(STEP_MS); }
    check(!findRow(a, DetectionType::DEAUTH), "deauth: 5 broadcast frames stay under the threshold");
    deauth(a, true); advance(STEP_MS * 3);
    const Detection* d = findRow(a, DetectionType::DEAUTH);
    check(d != nullptr, "deauth: 6th broadcast frame in 3 s fires DEAUTH");
    check(d && d->channel == 6 && d->rssi == -48, "deauth: row carries ch 6 / -48 dBm from radiotap");
    check(d && d->hits >= 6, "deauth: hits = frames in the burst");
    check(engine.countByType(DetectionType::DEAUTH) == base + 1, "deauth: counted once");

    // Unicast (band steering looks like this): needs 12. Past the 15 s
    // cooldown first, which is global.
    advance(16000);
    for (int i = 0; i < 11; i++) { deauth(u, false, i & 1); advance(STEP_MS); }
    check(!findRow(u, DetectionType::DEAUTH), "deauth: 11 unicast deauth/disassoc stay under 12");
    deauth(u, false); advance(STEP_MS * 3);
    check(findRow(u, DetectionType::DEAUTH) != nullptr,
          "deauth: 12th unicast frame (disassoc counted alongside) fires");

    // A burst spread over more than the 3 s window never fires.
    advance(16000);
    const uint8_t s[6] = {0xDE, 0xAD, 0x00, 0x00, 0x00, 0x04};
    for (int i = 0; i < 8; i++) { deauth(s, true); advance(4000); }
    check(!findRow(s, DetectionType::DEAUTH), "deauth: 8 broadcast frames 4 s apart do not fire");
}

// -------------------------------------------------------------------- BLE

// An HCI LE Advertising Report event carrying one advert. `addr` is in
// printed order (as a phone shows it); the wire is LSB first.
int bleAdvert(const uint8_t addr[6], uint8_t addrType, uint8_t evtType,
              const std::vector<uint8_t>& ad, int8_t rssi) {
    std::vector<uint8_t> p = { 0x04, 0x3E, 0x00, 0x02, 0x01, evtType, addrType };
    for (int i = 5; i >= 0; i--) p.push_back(addr[i]);
    p.push_back((uint8_t)ad.size());
    p.insert(p.end(), ad.begin(), ad.end());
    p.push_back((uint8_t)rssi);
    p[2] = (uint8_t)(p.size() - 3);
    return cm0::bleScanTestParse(p.data(), p.size());
}

// The NimBLEAdvertisedDevice stub's AD walk, against NimBLE's semantics.
void testAdParse() {
    const uint8_t a[6] = {1, 2, 3, 4, 5, 6};
    {
        // flags, 16-bit list (0xFEED, 0x180F), name, mfg (company 0x02E5).
        const uint8_t ad[] = { 0x02, 0x01, 0x06,
                               0x05, 0x03, 0xED, 0xFE, 0x0F, 0x18,
                               0x05, 0x09, 'L', 'a', 'm', 'p',
                               0x05, 0xFF, 0xE5, 0x02, 0xAB, 0xCD };
        NimBLEAdvertisedDevice dev(ad, sizeof ad, -40, a, 0);
        check(dev.getName() == "Lamp", "ad: name after flags + UUID list");
        check(dev.getServiceUUIDCount() == 2 &&
              dev.getServiceUUID(0).equals(NimBLEUUID((uint16_t)0xFEED)) &&
              dev.getServiceUUID(1).equals(NimBLEUUID((uint16_t)0x180F)),
              "ad: 16-bit UUID list (type 0x03), both entries");
        const std::string md = dev.getManufacturerData();
        check(md.size() == 4 && (uint8_t)md[0] == 0xE5 && (uint8_t)md[1] == 0x02 &&
              (uint8_t)md[3] == 0xCD, "ad: manufacturer data keeps the company ID first");
    }
    {
        // One 31-byte structure (a squad peer's / an AirTag's shape).
        uint8_t ad[31] = { 0x1E, 0xFF, 0x4C, 0x00, 0x12, 0x19 };
        for (int i = 6; i < 31; i++) ad[i] = (uint8_t)i;
        NimBLEAdvertisedDevice dev(ad, sizeof ad, -40, a, 3);
        check(dev.getManufacturerDataCount() == 1 && dev.getManufacturerData().size() == 29,
              "ad: a 31-byte single structure is parsed (29 bytes of mfg data)");
    }
    {
        // 128-bit list (type 0x07), little-endian on air; shortened name.
        uint8_t ad[] = { 0x11, 0x07, 0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80,
                         0x00, 0x10, 0x00, 0x00, 0x0D, 0x18, 0x00, 0x00,
                         0x03, 0x08, 'H', 'R' };
        NimBLEAdvertisedDevice dev(ad, sizeof ad, -40, a, 0);
        check(dev.getServiceUUIDCount() == 1 &&
              dev.getServiceUUID(0).equals(NimBLEUUID(std::string("0000180d-0000-1000-8000-00805f9b34fb"))),
              "ad: 128-bit UUID (type 0x07) matches its string form");
        check(dev.getName() == "HR", "ad: shortened name used when no complete name");
    }
    {
        // 0x11 is Security Manager OOB flags, not a UUID list; a truncated
        // trailing structure is ignored.
        const uint8_t ad[] = { 0x02, 0x11, 0x01, 0x05, 0x09, 'X' };
        NimBLEAdvertisedDevice dev(ad, sizeof ad, -40, a, 0);
        check(!dev.haveServiceUUID() && !dev.haveName(), "ad: 0x11 not a UUID list; truncated name dropped");
    }
}

void testTrackers() {
    // AirTag, separated from its owner: FindMy (Apple 0x004C, type 0x12,
    // length 0x19), the 30-byte manufacturer-data structure, random
    // static address, non-connectable.
    {
        const uint8_t mac[6] = {0xC4, 0x01, 0x02, 0x03, 0x04, 0x05};
        std::vector<uint8_t> ad = { 0x1E, 0xFF, 0x4C, 0x00, 0x12, 0x19, 0x10 };
        while (ad.size() < 31) ad.push_back(0xA5);
        check(bleAdvert(mac, 1, 3, ad, -61) == 1, "airtag: report parsed and queued");
        advance(STEP_MS * 10);
        const Detection* d = findRow(mac, DetectionType::AIRTAG);
        check(d != nullptr, "airtag: FindMy advert -> AIRTAG row, address in printed order");
        check(d && d->rssi == -61, "airtag: rssi -61 from the report");
    }
    // Tile: 0xFEED in the complete 16-bit service UUID list (the NimBLE
    // stub's UUID parsing, not just raw bytes).
    {
        const uint8_t mac[6] = {0xE6, 0x11, 0x12, 0x13, 0x14, 0x15};
        const std::vector<uint8_t> ad = { 0x02, 0x01, 0x06, 0x03, 0x03, 0xED, 0xFE };
        check(bleAdvert(mac, 1, 0, ad, -70) == 1, "tile: report queued");
        advance(STEP_MS * 10);
        check(findRow(mac, DetectionType::TILE) != nullptr, "tile: 0xFEED service UUID -> TILE");
    }
    // Samsung SmartTag: 0xFD5A as service data.
    {
        const uint8_t mac[6] = {0xD2, 0x21, 0x22, 0x23, 0x24, 0x25};
        std::vector<uint8_t> ad = { 0x02, 0x01, 0x06, 0x0B, 0x16, 0x5A, 0xFD,
                                    0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80 };
        check(bleAdvert(mac, 1, 3, ad, -55) == 1, "smarttag: report queued");
        advance(STEP_MS * 10);
        check(findRow(mac, DetectionType::SAMSUNG_TAG) != nullptr,
              "smarttag: 0xFD5A service data -> SAMSUNG_TAG");
    }
    // An ordinary advert (flags + a name): no row of any tracker type.
    {
        const uint8_t mac[6] = {0x5A, 0x31, 0x32, 0x33, 0x34, 0x35};
        const std::vector<uint8_t> ad = { 0x02, 0x01, 0x06, 0x05, 0x09, 'L', 'a', 'm', 'p' };
        bleAdvert(mac, 1, 0, ad, -50);
        advance(STEP_MS * 10);
        bool any = false;
        for (DetectionType t : {DetectionType::AIRTAG, DetectionType::TILE,
                                DetectionType::SAMSUNG_TAG, DetectionType::GOOGLE_TAG})
            any = any || findRow(mac, t);
        check(!any, "plain advert: not a tracker");
    }
}

// Squad advertising: transmit on through the firmware's own settings (the
// consent flag + the toggle the menu cycles), then Mesh::radioTick from
// loop() must start the advertiser with a SquachMesh manufacturer field.
// On the device under sudo this really goes on air (check with btmon).
void testSquadTx() {
    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    advance(2000);
    check(!adv->isAdvertising(), "squad tx: off by default (no consent, no toggle)");
    Settings::setMeshConsent(true);
    if (!Settings::meshTransmit()) Settings::cycleMeshTransmit();
    check(Settings::meshTransmit(), "squad tx: consent + toggle -> meshTransmit()");
    advance(3000);
    check(adv->isAdvertising(), "squad tx: advertiser started by the loop");
    const std::string& ad = adv->adBytes();
    check(ad.size() >= 4 && ad.size() <= 31 && (uint8_t)ad[0] == ad.size() - 1 &&
          (uint8_t)ad[1] == 0xFF && (uint8_t)ad[2] == 0xFF && (uint8_t)ad[3] == 0xFF,
          "squad tx: one mfg AD structure, company 0xFFFF, fits 31 bytes");
    check(adv->minUnits() == 2400, "squad tx: idle cadence 1500 ms = 2400 units");
    std::printf("[selftest] squad advert (%zu bytes) on air for 5 s\n", ad.size());
    advance(5000);
    Settings::cycleMeshTransmit();
    advance(2000);
    check(!adv->isAdvertising(), "squad tx: toggle off stops the advertiser");
}

// Features this board does not have yet (coming_soon.cpp, ota_cm0.cpp):
// their settings stay OFF when pressed, the loop keeps running with them
// pressed (EXT SCREEN on crashed pushFrame()), and the updater says
// "FEATURE COMING SOON" instead of the emulator's scripted fake install.
void testComingSoon() {
    check(!Settings::extScreen(), "ext screen: off at boot");
    // The row's own sequence (main.cpp, SettingsRow::EXT_SCREEN): the toggle,
    // then its "EXT SCREEN OFF" toast -- which must come out as ours.
    Settings::toggleExtScreen();
    check(!Settings::extScreen(), "ext screen: toggle leaves it off");
    Theme::showToast(Settings::extScreen() ? "EXT SCREEN ON" : "EXT SCREEN OFF", nullptr, Theme::CYAN);
    check(std::strcmp(cm0LastToastHead(), "FEATURE COMING SOON") == 0,
          "ext screen: the row's own toast becomes FEATURE COMING SOON");
    Theme::showToast("PRIVACY MODE ON", nullptr, Theme::CYAN);
    check(std::strcmp(cm0LastToastHead(), "PRIVACY MODE ON") == 0,
          "toasts: every other toast is left alone");
    Settings::toggleUpdateCheck();
    check(std::strcmp(cm0LastToastHead(), "FEATURE COMING SOON") == 0,
          "update check: the toggle shows FEATURE COMING SOON");
    Settings::toggleUpdateCheck();
    check(!Settings::updateCheck(), "update check: off at boot");
    Settings::toggleUpdateCheck();
    check(!Settings::updateCheck(), "update check: toggle leaves it off");
    advance(1000);
    check(!Settings::extScreen(), "ext screen: still off after a second of loop()");

    const char* v = OtaCore::runningVersion();
    std::printf("[selftest] running version: %s\n", v);
    check(v && v[0] == 'v' && std::strcmp(v, "v1.7.1") != 0,
          "updater: the real firmware tag, not the emulator's v1.7.1");
    check(OtaCore::otherVersion() == nullptr, "updater: no other slot to switch to");
    check(!OtaBle::available(), "updater: no Bluetooth update server");
    check(OtaWifi::savedCount() == 0, "updater: no made-up saved networks");
    OtaWifi::begin();
    check(OtaWifi::state() == OtaWifi::State::FAILED &&
          std::strstr(OtaWifi::failureText(), "FEATURE COMING SOON") != nullptr,
          "updater: starting an update says FEATURE COMING SOON");
    check(!OtaWifi::canTryAgain(), "updater: no TRY AGAIN (it would say the same)");
    OtaWifi::end();
}

}   // namespace

// Called after setup() and the boot splash; `step` = one harness step
// (pumps + loop()).
int runSelftest(void (*step)()) {
    s_step = step;
    std::printf("[selftest] detection path, virtual time\n");
    testAdParse();
    testDeauth();
    testTrackers();
    testSquadTx();
    testComingSoon();
    std::printf("\n%d checks, %d failures\n", s_checks, s_failures);
    return s_failures ? 1 : 0;
}
