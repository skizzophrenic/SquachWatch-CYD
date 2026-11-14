// SquachEmit's shot lists against this firmware's signature tables.
//
// SquachEmit (a sibling repository) is the bench tool that fires what this
// firmware detects. Its tables were hand copies, and by 2026-10-14 they were
// a month stale: sixteen Flock addresses this detector had dropped, a Google
// tag frame it no longer accepts, an AirTag frame it no longer recognises,
// and nothing at all for Axon's Bluetooth, ShotSpotter or drones by maker --
// and every one of those buttons still said it had fired.
//
// Its lists are generated from src/signatures.cpp now (SquachEmit's
// tools/gen_shots.py), and this test holds the two together both ways:
//
//   * every shot it can fire is detected, as the kind its button claims;
//   * every OUI, UUID, company ID, SSID prefix and name in the tables here
//     is fired by some shot.
//
// So a signature added or dropped here without re-running the generator
// turns this red. The adverts are matched in the order detection.cpp's BLE
// handler uses, through the same functions.
//
// Built only when SquachEmit sits beside this checkout (see the Makefile);
// CI has just this repository, and skips it.
#include "signatures.h"
#include "emit_data.h"
#include "test_util.h"
#include <cstdio>
#include <cstring>
#include <set>
#include <string>

static const DetectionType kWant[(uint8_t)EmitCategory::COUNT] = {
    DetectionType::FLOCK, DetectionType::AXON, DetectionType::META, DetectionType::SKIMMER,
    DetectionType::RAVEN, DetectionType::AIRTAG, DetectionType::DRONE, DetectionType::ALPR,
    DetectionType::CAMERA, DetectionType::SAMSUNG_TAG, DetectionType::GOOGLE_TAG, DetectionType::TILE,
    DetectionType::RING, DetectionType::IBEACON, DetectionType::EVILTWIN, DetectionType::HACKER,
    DetectionType::UNKNOWN,   // SQUACHMESH: a peer, never a detection
};

// The Flock accessory service, as detection.cpp spells it, little-endian.
static const uint8_t kFlockGatt[16] = { 0x6F, 0x2E, 0x17, 0xDF, 0x14, 0x18, 0xE5, 0x9F,
                                        0xA8, 0x46, 0x32, 0x95, 0x38, 0xBB, 0xCC, 0xE8 };

// What the BLE handler would make of this advert, in its order: company ID
// (Apple's needing the Find My frame), the raw AirTag frame, the Axon tag,
// Find Hub, a known UUID as service data, the UUID lists, the name.
static DetectionType classifyAdvert(const uint8_t* ad, uint8_t len) {
    DetectionType t = DetectionType::UNKNOWN;
    const uint8_t* mfg = nullptr; uint8_t mfgLen = 0;
    char name[32] = {0};
    for (uint8_t i = 0; i + 1 < len; ) {
        const uint8_t n = ad[i], type = ad[i + 1];
        if (!n || i + 1 + n > len) break;
        const uint8_t* d = ad + i + 2; const uint8_t dn = (uint8_t)(n - 1);
        if (type == 0xFF && !mfg) { mfg = d; mfgLen = dn; }
        if ((type == 0x08 || type == 0x09) && dn < sizeof(name)) { memcpy(name, d, dn); name[dn] = 0; }
        i = (uint8_t)(i + 1 + n);
    }
    if (mfg && mfgLen >= 2) {
        t = lookupMfgId((uint16_t)(mfg[0] | (mfg[1] << 8)));
        if (t == DetectionType::AIRTAG && !isAirTagPayload(ad, len))
            t = isIBeacon(mfg, mfgLen) ? DetectionType::IBEACON : DetectionType::UNKNOWN;
    }
    if (t == DetectionType::UNKNOWN && isAirTagPayload(ad, len)) t = DetectionType::AIRTAG;
    if (t == DetectionType::UNKNOWN && isAxonBwcPayload(ad, len)) t = DetectionType::AXON;
    if (t == DetectionType::UNKNOWN && isFmdnPayload(ad, len)) t = DetectionType::GOOGLE_TAG;
    if (t == DetectionType::UNKNOWN) { const uint16_t u = serviceDataUuid(ad, len); if (u) t = lookupUuid(u); }
    for (uint8_t i = 0; t == DetectionType::UNKNOWN && i + 1 < len; ) {
        const uint8_t n = ad[i], type = ad[i + 1];
        if (!n || i + 1 + n > len) break;
        const uint8_t* d = ad + i + 2; const uint8_t dn = (uint8_t)(n - 1);
        if (type == 0x02 || type == 0x03)
            for (uint8_t k = 0; k + 1 < dn && t == DetectionType::UNKNOWN; k += 2) {
                const uint16_t u = (uint16_t)(d[k] | (d[k + 1] << 8));
                if (u != 0xFEAA) t = lookupUuid(u);     // Find Hub is its frame, above
            }
        if ((type == 0x06 || type == 0x07) && dn >= 16 && memcmp(d, kFlockGatt, 16) == 0) t = DetectionType::FLOCK;
        i = (uint8_t)(i + 1 + n);
    }
    if (t == DetectionType::UNKNOWN && name[0]) t = lookupBtName(name);
    return t;
}

int main() {
    std::set<std::string> fired;   // "oui:..", "uuid:..", "mfg:..", "ssid:..", "name:.."
    char key[48];

    suite("Every shot SquachEmit can fire is detected as its button says");
    for (uint8_t c = 0; c < (uint8_t)EmitCategory::COUNT; c++) {
        const ShotList& L = kShots[c];
        ck((std::string(kCategoryLabel[c]) + " has shots").c_str(), L.n > 0);
        for (uint16_t i = 0; i < L.n; i++) {
            const Shot& s = L.s[i];
            DetectionType got = kWant[c];
            if (s.kind == ShotKind::PROBE) {
                const uint8_t mac[6] = { s.oui[0], s.oui[1], s.oui[2], 0x12, 0x34, 0x56 };
                got = lookupOui(mac);
                snprintf(key, sizeof key, "oui:%02X%02X%02X", s.oui[0], s.oui[1], s.oui[2]); fired.insert(key);
            } else if (s.kind == ShotKind::HOTSPOT) {
                char ssid[40];
                snprintf(ssid, sizeof ssid, s.hex ? "%sBEEF" : "%s", s.ssid);
                got = lookupSsid(ssid);
                fired.insert(std::string("ssid:") + ssid);
            } else if (s.kind == ShotKind::ADVERT) {
                got = classifyAdvert(s.ad, s.adLen);
                for (uint8_t k = 0; k + 1 < s.adLen; ) {
                    const uint8_t n = s.ad[k], type = s.ad[k + 1];
                    if (!n) break;
                    const uint8_t* d = s.ad + k + 2;
                    if ((type == 0x02 || type == 0x03 || type == 0x16) && n >= 3) {
                        snprintf(key, sizeof key, "uuid:%04X", d[0] | (d[1] << 8)); fired.insert(key);
                    }
                    if (type == 0xFF && n >= 3) { snprintf(key, sizeof key, "mfg:%04X", d[0] | (d[1] << 8)); fired.insert(key); }
                    if (type == 0x09) fired.insert("name:" + std::string((const char*)d, n - 1));
                    k = (uint8_t)(k + 1 + n);
                }
            } else {
                continue;   // built in emit.cpp: Remote ID, iBeacon, mesh, Pwnagotchi, twin
            }
            char what[96];
            snprintf(what, sizeof what, "%s: %s", kCategoryLabel[c], s.label);
            ck(what, got == kWant[c]);
            if (got != kWant[c]) printf("      detected as %d, the button is %d\n", (int)got, (int)kWant[c]);
        }
    }

    suite("Every signature in the tables here has a shot");
    for (uint16_t i = 0; i < kOuiCount; i++) {
        snprintf(key, sizeof key, "oui:%02X%02X%02X", kOuiTable[i].b[0], kOuiTable[i].b[1], kOuiTable[i].b[2]);
        if (!fired.count(key)) ck((std::string("OUI ") + kOuiTable[i].name + " " + key).c_str(), false);
    }
    for (uint16_t i = 0; i < kUuidCount; i++) {
        if (kUuidTable[i].uuid == 0xFFFA) continue;   // Remote ID, built in emit.cpp
        snprintf(key, sizeof key, "uuid:%04X", kUuidTable[i].uuid);
        if (!fired.count(key)) ck((std::string("UUID ") + kUuidTable[i].name + " " + key).c_str(), false);
    }
    for (uint16_t i = 0; i < kMfgIdCount; i++) {
        snprintf(key, sizeof key, "mfg:%04X", kMfgIdTable[i].mfgId);
        if (!fired.count(key)) ck((std::string("company ") + kMfgIdTable[i].name + " " + key).c_str(), false);
    }
    for (uint16_t i = 0; i < kSsidCount; i++) {
        bool any = false;
        for (const std::string& f : fired)
            if (f.compare(0, 5, "ssid:") == 0 && strncasecmp(f.c_str() + 5, kSsidPrefixes[i].prefix, strlen(kSsidPrefixes[i].prefix)) == 0) any = true;
        if (!any) ck((std::string("SSID prefix ") + kSsidPrefixes[i].prefix).c_str(), false);
    }
    for (uint16_t i = 0; i < kBtClassicCount; i++)
        if (!fired.count(std::string("name:") + kBtClassicNames[i].name))
            ck((std::string("name ") + kBtClassicNames[i].name).c_str(), false);
    ck("all covered (any gap is listed above)", true);

    return report();
}
