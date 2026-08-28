// SquachWatch-CYD — DetectionEngine implementation
#include "detection.h"
#include "signatures.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <NimBLEDevice.h>
#include <NimBLEAdvertisedDevice.h>
#include <NimBLEScan.h>
#include <esp_bt.h>
#include <esp_gap_bt_api.h>
#include <string.h>
#include <SD.h>

// -------- global engine instance (referenced by callbacks) --------
static DetectionEngine* g_engine = nullptr;

// -------- helpers --------

static void formatMac(char* dst, size_t dstSize, const uint8_t* mac) {
    snprintf(dst, dstSize, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void copyMac(char* dst12, const uint8_t* mac6) {
    snprintf(dst12, 12, "%02X%02X%02X%02X%02X",
             mac6[0], mac6[1], mac6[2], mac6[3], mac6[4], mac6[5]);
    // Trailing space, makes vendor field 12 chars wide
    dst12[10] = ' ';
    dst12[11] = 0;
}

// scan->start(0, cb, ...) starts an indefinite scan (0 = "forever" per
// NimBLEScan::start()'s own implementation) — cb is a *scan-complete*
// callback, which for a forever-scan never fires under normal
// operation. That's the real reason BLE detection (AirTag, Meta
// glasses, Raven, Tile, DroneID) never worked at all, confirmed
// against a real AirTag that other detectors picked up fine but this
// one never did — not just the earlier nullptr-restart bug (a real
// bug too, but not the actual root cause). The correct API for
// continuous scanning is setAdvertisedDeviceCallbacks(), which fires
// onResult() in real time for every advertisement seen, live, with no
// restart needed.
class BleScanCallbacks : public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice* adv) override {
        if (!g_engine) return;
        Detection det;
        memset(&det, 0, sizeof(det));
        const uint8_t* mac = adv->getAddress().getNative();
        memcpy(det.mac, mac, 6);
        det.rssi   = adv->getRSSI();
        det.channel= 0;
        det.firstSeen = det.lastSeen = millis();
        det.hits   = 1;
        det.active = true;
        const char* name = adv->getName().c_str();
        if (name && name[0]) {
            strncpy(det.name, name, sizeof(det.name) - 1);
        }
        // Manufacturer data
        if (adv->haveManufacturerData()) {
            std::string mfg = adv->getManufacturerData();
            if (mfg.size() >= 2) {
                uint16_t mfgId = (uint8_t)mfg[0] | ((uint8_t)mfg[1] << 8);
                det.type = lookupMfgId(mfgId);
                if (det.type == DetectionType::AIRTAG && mfg.size() >= 3) {
                    if (!isAirTagSubtype((const uint8_t*)mfg.data(), mfg.size())) {
                        det.type = DetectionType::UNKNOWN;
                    }
                }
            }
        }
        // Service UUIDs
        if (det.type == DetectionType::UNKNOWN && adv->haveServiceUUID()) {
            for (int j = 0; j < adv->getServiceUUIDCount(); j++) {
                NimBLEUUID u = adv->getServiceUUID(j);
                // 16-bit UUID match: avoid touching ble_uuid_t's
                // internals (the struct layout varies between
                // NimBLE-Arduino versions). The equals() method is
                // a stable API and compares the logical 16-bit value.
                if (u.bitSize() == 16) {
                    static const uint16_t kKnown16[] = {
                        0x1101,  // SPP — skimmer
                        0xFEED,  // Tile tracker
                        0xFD5F,  // Ray-Ban Meta glasses
                        0x3100, 0x3200, 0x3300, 0x3400, 0x3500,  // Raven
                        0xFFFA,  // OpenDroneID
                        0xFD5A,  // Samsung SmartTag
                        0xFEAA,  // Google Find My Device Network (Eddystone)
                    };
                    for (uint16_t k : kKnown16) {
                        if (u.equals(NimBLEUUID((uint16_t)k))) {
                            det.type = lookupUuid(k);
                            break;
                        }
                    }
                    if (det.type != DetectionType::UNKNOWN) break;
                }
            }
        }
        // Name fallback
        if (det.type == DetectionType::UNKNOWN && det.name[0]) {
            det.type = lookupBtName(det.name);
        }
        if (det.type == DetectionType::UNKNOWN) return;
        // Set vendor label based on the matched table entry.
        if (det.type == DetectionType::AIRTAG) {
            strncpy(det.vendor, "Apple", sizeof(det.vendor) - 1);
        } else if (det.type == DetectionType::DRONE) {
            strncpy(det.vendor, "DroneID", sizeof(det.vendor) - 1);
        } else if (det.type == DetectionType::META) {
            strncpy(det.vendor, "Meta", sizeof(det.vendor) - 1);
        } else if (det.type == DetectionType::RAVEN) {
            strncpy(det.vendor, "Raven", sizeof(det.vendor) - 1);
        } else if (det.type == DetectionType::FLOCK) {
            strncpy(det.vendor, "Flock-BLE", sizeof(det.vendor) - 1);
        } else if (det.type == DetectionType::SAMSUNG_TAG) {
            strncpy(det.vendor, "Samsung", sizeof(det.vendor) - 1);
        } else if (det.type == DetectionType::GOOGLE_TAG) {
            strncpy(det.vendor, "Google", sizeof(det.vendor) - 1);
        } else {
            strncpy(det.vendor, "BLE", sizeof(det.vendor) - 1);
        }
        g_engine->postBle(det);
    }
};
static BleScanCallbacks g_bleScanCallbacks;

// -------- DetectionEngine --------

bool DetectionEngine::init() {
    if (g_engine) return true;
    g_engine = this;

    // 1. SD card (best-effort)
    _sd.begin();

    // Lifetime detection count survives reboots — Squachy references it
    // for milestone quips. Live _typeCounts above deliberately don't
    // persist (they decay when a detection goes stale), so this is
    // tracked separately.
    _prefs.begin("squachwatch", false);
    _lifetimeTotal = _prefs.getUInt("total", 0);

    // 2. WiFi promiscuous mode for OUI/SSID detection
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(50);
    esp_wifi_set_promiscuous(true);
    wifi_promiscuous_filter_t filter;
    filter.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA;
    esp_wifi_set_promiscuous_filter(&filter);
    esp_wifi_set_promiscuous_rx_cb([](void* buf, wifi_promiscuous_pkt_type_t) {
        if (!g_engine) return;
        const wifi_promiscuous_pkt_t* pkt = (const wifi_promiscuous_pkt_t*)buf;
        if (pkt->rx_ctrl.sig_len < 24) return;
        // 802.11 frame header: bytes 0..23 contain frame control, duration,
        // addr1 (DA, offset 4), addr2 (SA, offset 10), addr3 (BSSID, offset 16)
        const uint8_t* frame = pkt->payload;
        uint8_t fc0 = frame[0];
        uint8_t fc1 = frame[1];
        uint8_t type  = (fc0 & 0x0C) >> 2;
        uint8_t subtype = (fc0 & 0xF0) >> 4;
        // Management frame probe request: type=0, subtype=4
        if (type == 0 && subtype == 4) {
            // addr2 (transmitter) is at offset 10
            g_engine->postWiFi(frame + 10, pkt->rx_ctrl.rssi, pkt->rx_ctrl.channel);
        } else if (type == 2) {
            // Data frame: addr1 (DA) and addr2 (SA) both interesting
            g_engine->postWiFi(frame + 4,  pkt->rx_ctrl.rssi, pkt->rx_ctrl.channel);
            g_engine->postWiFi(frame + 10, pkt->rx_ctrl.rssi, pkt->rx_ctrl.channel);
        } else if (type == 0 && subtype == 8) {
            // Beacon: fixed params (timestamp+interval+capability) run
            // 12 bytes after the 24-byte header, then the SSID is the
            // first information element — tag 0x00, 1-byte length,
            // then up to 32 bytes of SSID (unescaped, not
            // null-terminated in the frame itself).
            char ssid[33] = {0};
            uint32_t sigLen = pkt->rx_ctrl.sig_len;
            if (sigLen > 37) {
                const uint8_t* ie = frame + 36;
                if (ie[0] == 0x00) {
                    uint8_t ssidLen = ie[1];
                    if (ssidLen > 32) ssidLen = 32;
                    if (36 + 2 + ssidLen <= sigLen) {
                        memcpy(ssid, ie + 2, ssidLen);
                        ssid[ssidLen] = 0;
                    }
                }
            }
            g_engine->postWiFi(frame + 16, pkt->rx_ctrl.rssi, pkt->rx_ctrl.channel, ssid);
        }
    });

    // 3. NimBLE scan — onResult() fires live per-advertisement via
    // g_bleScanCallbacks (see above), not via a scan-complete callback
    // that would never fire on an indefinite (duration 0) scan.
    NimBLEDevice::init("");
    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setActiveScan(true);
    scan->setInterval(100);
    scan->setWindow(99);
    scan->setDuplicateFilter(false);
    // wantDuplicates=true: we want onResult() called on every sighting
    // of a device, not just the first, so lastSeen/RSSI keep updating
    // (postBle()/expireStale() rely on that for the still-active log).
    scan->setAdvertisedDeviceCallbacks(&g_bleScanCallbacks, true);
    scan->start(0, nullptr, false);

    // 4. BT Classic inquiry for skimmer names (best-effort, every 60 s)
    if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED) {
        // We don't try to *start* the controller here — NimBLE may have
        // taken it over. The v1.0 implementation is BLE-only for skimmers
        // (advertised name match). Documented in docs/DETECTIONS.md.
    }

    return true;
}

void DetectionEngine::loop() {
    hopChannel();
    processWiFiQ();
    expireStale();
    _sd.tick();
}

void DetectionEngine::hopChannel() {
    // Dwell ~300ms per channel, cycling 1-13 — without this the
    // promiscuous sniffer stays parked on whatever channel the radio
    // defaulted to and only ever sees traffic on that one channel,
    // missing anything (real hardware included, not just test rigs)
    // transmitting elsewhere in the band.
    uint32_t now = millis();
    if (now - _lastHopMs < 300) return;
    _lastHopMs = now;
    _wifiChannel = (_wifiChannel % 13) + 1;
    esp_wifi_set_channel(_wifiChannel, WIFI_SECOND_CHAN_NONE);
}

void DetectionEngine::clearLog() {
    _logCount = 0;
    _logHead  = 0;
    _latest   = nullptr;
    for (uint8_t i = 0; i < (uint8_t)DetectionType::COUNT; i++) {
        _typeCounts[i] = 0;
    }
}

void IRAM_ATTR DetectionEngine::postWiFi(const uint8_t* mac, int8_t rssi, uint8_t channel,
                                         const char* ssid) {
    if (!mac) return;
    uint8_t next = (_wifiQHead + 1) % WIFI_Q_CAP;
    if (next == _wifiQTail) return;            // queue full, drop
    WiFiQEntry& e = (WiFiQEntry&)_wifiQ[_wifiQHead];
    memcpy((void*)e.mac, mac, 6);
    e.rssi    = rssi;
    e.channel = channel;
    if (ssid && ssid[0]) {
        strncpy((char*)e.ssid, ssid, sizeof(e.ssid) - 1);
        e.ssid[sizeof(e.ssid) - 1] = 0;
    } else {
        e.ssid[0] = 0;
    }
    _wifiQHead = next;
}

void DetectionEngine::postBle(Detection d) {
    // Try to dedupe / merge with existing log entry by MAC
    for (uint8_t i = 0; i < _logCount; i++) {
        uint8_t slot = (_logHead + LOG_CAP - 1 - i) % LOG_CAP;
        if (memcmp(_log[slot].mac, d.mac, 6) == 0 &&
            _log[slot].type == d.type) {
            _log[slot].hits++;
            _log[slot].rssi = d.rssi;
            _log[slot].lastSeen = millis();
            _typeCounts[(uint8_t)d.type] = _typeCounts[(uint8_t)d.type];
            return;
        }
    }
    pushLog(d);
}

void DetectionEngine::postBtClassic(Detection d) {
    pushLog(d);
}

void DetectionEngine::processWiFiQ() {
    while (_wifiQTail != _wifiQHead) {
        WiFiQEntry e;
        {
            // copy out under volatile guard
            noInterrupts();
            e = (const WiFiQEntry&)_wifiQ[_wifiQTail];
            _wifiQTail = (_wifiQTail + 1) % WIFI_Q_CAP;
            interrupts();
        }
        // Check OUI first (per DESIGN.md §6.2 precedence); fall back to
        // the SSID prefix (e.g. an Axon/Flock unit in pairing mode,
        // broadcasting from a WiFi module OUI we don't otherwise know)
        // if the OUI itself didn't match anything.
        DetectionType t = lookupOui(e.mac);
        bool matchedBySsid = false;
        if (t == DetectionType::UNKNOWN && e.ssid[0]) {
            t = lookupSsid(e.ssid);
            matchedBySsid = (t != DetectionType::UNKNOWN);
        }
        if (t == DetectionType::UNKNOWN) continue;
        // Try to dedupe / merge
        bool merged = false;
        for (uint8_t i = 0; i < _logCount; i++) {
            uint8_t slot = (_logHead + LOG_CAP - 1 - i) % LOG_CAP;
            if (memcmp(_log[slot].mac, e.mac, 6) == 0 &&
                _log[slot].type == t) {
                _log[slot].hits++;
                _log[slot].rssi = e.rssi;
                _log[slot].lastSeen = millis();
                _log[slot].channel = e.channel;
                merged = true;
                break;
            }
        }
        if (merged) continue;
        Detection d;
        memset(&d, 0, sizeof(d));
        memcpy(d.mac, e.mac, 6);
        d.rssi    = e.rssi;
        d.channel = e.channel;
        d.type    = t;
        // Vendor label: from the SSID-prefix table if that's what
        // matched, otherwise from the OUI table.
        if (matchedBySsid) {
            const char* name = ssidVendorName(e.ssid);
            if (name) strncpy(d.vendor, name, sizeof(d.vendor) - 1);
        } else {
            for (uint16_t k = 0; k < kOuiCount; k++) {
                if (e.mac[0] == kOuiTable[k].b[0] &&
                    e.mac[1] == kOuiTable[k].b[1] &&
                    e.mac[2] == kOuiTable[k].b[2]) {
                    strncpy(d.vendor, kOuiTable[k].name, sizeof(d.vendor) - 1);
                    break;
                }
            }
        }
        d.firstSeen = d.lastSeen = millis();
        d.hits   = 1;
        d.active = true;
        pushLog(d);
    }
}

void DetectionEngine::pushLog(const Detection& d) {
    _log[_logHead] = d;
    _logHead = (_logHead + 1) % LOG_CAP;
    if (_logCount < LOG_CAP) _logCount++;
    _latest = &_log[(_logHead + LOG_CAP - 1) % LOG_CAP];
    _latestChangeMs = millis();
    _typeCounts[(uint8_t)d.type]++;
    _lifetimeTotal++;
    _prefs.putUInt("total", _lifetimeTotal);
    _sd.logEvent(d);
}

void DetectionEngine::expireStale() {
    uint32_t now = millis();
    for (uint8_t i = 0; i < _logCount; i++) {
        uint8_t slot = (_logHead + LOG_CAP - 1 - i) % LOG_CAP;
        if (_log[slot].active && (now - _log[slot].lastSeen) > STALE_MS) {
            _log[slot].active = false;
            if (_typeCounts[(uint8_t)_log[slot].type] > 0) {
                _typeCounts[(uint8_t)_log[slot].type]--;
            }
        }
    }
}

const Detection* DetectionEngine::logAt(uint8_t idx) const {
    if (idx >= _logCount) return nullptr;
    uint8_t slot = (_logHead + LOG_CAP - 1 - idx) % LOG_CAP;
    return &_log[slot];
}

// Module-level helpers used by main / UI
static char g_macBuf[20];
const char* macFmt(const uint8_t* mac) {
    formatMac(g_macBuf, sizeof(g_macBuf), mac);
    return g_macBuf;
}
