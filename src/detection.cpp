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

// -------- DetectionEngine --------

bool DetectionEngine::init() {
    if (g_engine) return true;
    g_engine = this;

    // 1. SD card (best-effort)
    _sd.begin();

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
            // Beacon: SSID in IE; for now just record the BSSID (addr3)
            g_engine->postWiFi(frame + 16, pkt->rx_ctrl.rssi, pkt->rx_ctrl.channel);
        }
    });

    // 3. NimBLE scan
    NimBLEDevice::init("");
    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setActiveScan(true);
    scan->setInterval(100);
    scan->setWindow(99);
    scan->setDuplicateFilter(false);
    scan->start(0, [](NimBLEScanResults results) {
        if (!g_engine) return;
        for (int i = 0; i < results.getCount(); i++) {
            // Newer NimBLE-Arduino returns the advertised device by value
            // and the getters are non-const, so we copy it locally.
            NimBLEAdvertisedDevice d = results.getDevice(i);
            Detection det;
            memset(&det, 0, sizeof(det));
            const uint8_t* mac = d.getAddress().getNative();
            memcpy(det.mac, mac, 6);
            det.rssi   = d.getRSSI();
            det.channel= 0;
            det.firstSeen = det.lastSeen = millis();
            det.hits   = 1;
            det.active = true;
            const char* name = d.getName().c_str();
            if (name && name[0]) {
                strncpy(det.name, name, sizeof(det.name) - 1);
            }
            // Manufacturer data
            if (d.haveManufacturerData()) {
                std::string mfg = d.getManufacturerData();
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
            if (det.type == DetectionType::UNKNOWN && d.haveServiceUUID()) {
                for (int j = 0; j < d.getServiceUUIDCount(); j++) {
                    NimBLEUUID u = d.getServiceUUID(j);
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
            if (det.type == DetectionType::UNKNOWN) continue;
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
            } else {
                strncpy(det.vendor, "BLE", sizeof(det.vendor) - 1);
            }
            g_engine->postBle(det);
        }
        // keep scanning
        NimBLEDevice::getScan()->start(0, nullptr, false);
    }, false);

    // 4. BT Classic inquiry for skimmer names (best-effort, every 60 s)
    if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED) {
        // We don't try to *start* the controller here — NimBLE may have
        // taken it over. The v1.0 implementation is BLE-only for skimmers
        // (advertised name match). Documented in docs/DETECTIONS.md.
    }

    return true;
}

void DetectionEngine::loop() {
    processWiFiQ();
    expireStale();
    _sd.tick();
}

void DetectionEngine::clearLog() {
    _logCount = 0;
    _logHead  = 0;
    _latest   = nullptr;
    for (uint8_t i = 0; i < (uint8_t)DetectionType::COUNT; i++) {
        _typeCounts[i] = 0;
    }
}

void IRAM_ATTR DetectionEngine::postWiFi(const uint8_t* mac, int8_t rssi, uint8_t channel) {
    if (!mac) return;
    uint8_t next = (_wifiQHead + 1) % WIFI_Q_CAP;
    if (next == _wifiQTail) return;            // queue full, drop
    WiFiQEntry& e = (WiFiQEntry&)_wifiQ[_wifiQHead];
    memcpy((void*)e.mac, mac, 6);
    e.rssi    = rssi;
    e.channel = channel;
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
        // Check OUI
        DetectionType t = lookupOui(e.mac);
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
        // Vendor label from table
        for (uint16_t k = 0; k < kOuiCount; k++) {
            if (e.mac[0] == kOuiTable[k].b[0] &&
                e.mac[1] == kOuiTable[k].b[1] &&
                e.mac[2] == kOuiTable[k].b[2]) {
                strncpy(d.vendor, kOuiTable[k].name, sizeof(d.vendor) - 1);
                break;
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
