// SquachWatch-CYD — DetectionEngine implementation
#include "detection.h"
#include "signatures.h"
#include "settings.h"
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

// -------- manual raw scanner state (see startRawBleScan/startRawWifiScan) --------
// NONE = normal continuous signature-matched scanning (the default).
// Only one of these is ever active at a time -- see stopRawScan().
enum class RawScanMode : uint8_t { NONE, BLE, WIFI };
static RawScanMode g_rawMode        = RawScanMode::NONE;
static uint32_t    g_rawBleStartMs  = 0;
// How long a raw BLE sweep stays open before the UI is told it's
// "done" -- a deliberately longer, focused dwell than the continuous
// scan ever gives any one moment, which is the actual "more thorough"
// part; devices keep updating in _rawBle past this point too (nothing
// stops capturing), it's purely a UI cue for when to stop showing
// "SCANNING..." and reveal the list.
static const uint32_t RAW_BLE_SCAN_MS = 8000;

// -------- helpers --------

static void formatMac(char* dst, size_t dstSize, const uint8_t* mac) {
    snprintf(dst, dstSize, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
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
        const uint8_t* mac = adv->getAddress().getNative();
        // Checked regardless of raw-scan mode -- a watched/hunted
        // target still fires even if it's not a known signature and
        // even while the raw-scan screen happens to be open. The two
        // are independent slots (see detection.h), so both are always
        // checked -- either, both, or neither can match a given frame.
        int8_t rssi = (int8_t)adv->getRSSI();
        g_engine->checkWatchBle(mac, rssi);
        g_engine->checkHuntBle(mac, rssi);
        if (g_rawMode == RawScanMode::WIFI) return;   // radio's dedicated to the WiFi sweep right now
        if (g_rawMode == RawScanMode::BLE) {
            RawBleResult r;
            memset(&r, 0, sizeof(r));
            memcpy(r.mac, mac, 6);
            r.rssi = adv->getRSSI();
            const char* name = adv->getName().c_str();
            if (name && name[0]) strncpy(r.name, name, sizeof(r.name) - 1);
            g_engine->postRawBle(r);
            return;
        }
        Detection det;
        memset(&det, 0, sizeof(det));
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
                // Apple's company ID alone is every Apple device, so it
                // still has to be confirmed as a tag. That check now
                // runs against the RAW advert rather than this parsed
                // field -- see isAirTagPayload().
                if (det.type == DetectionType::AIRTAG) {
                    if (!isAirTagPayload(adv->getPayload(),
                                         (uint8_t)adv->getPayloadLength())) {
                        det.type = DetectionType::UNKNOWN;
                    }
                }
            }
        }
        // Raw-advert fallback. The block above only runs when NimBLE
        // parsed a manufacturer-data field and put Apple's company ID
        // first; the reference implementation this came from does not
        // depend on either, it just scans the bytes. An advert that
        // carries the Find My structure behind another AD structure, or
        // in a scan response, reaches the detector only through here.
        if (det.type == DetectionType::UNKNOWN &&
            isAirTagPayload(adv->getPayload(), (uint8_t)adv->getPayloadLength())) {
            det.type = DetectionType::AIRTAG;
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
                        0xFEEC,  // Tile tracker (second SIG-assigned UUID)
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
        } else if (det.type == DetectionType::TILE) {
            strncpy(det.vendor, "Tile", sizeof(det.vendor) - 1);
        } else {
            strncpy(det.vendor, "BLE", sizeof(det.vendor) - 1);
        }
        g_engine->postBle(det);
    }
};
static BleScanCallbacks g_bleScanCallbacks;

// -------- DetectionEngine --------

void DetectionEngine::saveLifetimeByType() {
    _prefs.putBytes("typetot", _lifetimeByType, sizeof(_lifetimeByType));
}

void DetectionEngine::resetLifetime() {
    _lifetimeTotal = 0;
    _prefs.putUInt("total", 0);
    for (uint8_t i = 0; i < (uint8_t)DetectionType::COUNT; i++) {
        _typeCounts[i]     = 0;
        _lifetimeByType[i] = 0;
    }
    saveLifetimeByType();
}

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
    // A short read means the key predates this field or the type list has
    // grown since it was written; take what is there and leave the rest at
    // zero rather than discarding counts someone has spent months earning.
    {
        size_t have = _prefs.getBytesLength("typetot");
        if (have > sizeof(_lifetimeByType)) have = sizeof(_lifetimeByType);
        if (have >= sizeof(uint32_t)) _prefs.getBytes("typetot", _lifetimeByType, have);
    }

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
            // Capability info is the last 2 bytes of the 12-byte fixed
            // parameter block following the 24-byte header, so offset
            // 34. Bit 4 (0x10) is Privacy: set means this BSS requires
            // encryption. That one bit is what separates a mesh node
            // from an evil twin -- see noteApBeacon().
            bool enc = (sigLen > 35) && ((frame[34] & 0x10) != 0);
            g_engine->postWiFi(frame + 16, pkt->rx_ctrl.rssi, pkt->rx_ctrl.channel, ssid, enc);
        } else if (type == 0 && subtype == 12) {
            // Deauthentication: addr2 (transmitter -- the attacker, or
            // a spoofed AP address) at the same offset probe requests
            // use above. A single frame here is completely normal
            // WiFi traffic (a phone disconnecting, an AP restarting);
            // postDeauth()/processDeauthQ() is what actually decides
            // whether a BURST of them is happening.
            g_engine->postDeauth(frame + 10, pkt->rx_ctrl.rssi, pkt->rx_ctrl.channel);
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
    // Without this, NimBLE keeps its OWN permanent record of every
    // distinct BLE address it has ever seen since scan start (a
    // separate, unbounded structure from our own bounded 200-entry
    // _log) -- one new NimBLEAdvertisedDevice heap allocation per
    // never-before-seen address, held forever because this scan runs
    // indefinitely (duration 0) and never fires a scan-complete
    // callback to clear it. Confirmed on real hardware: a commute
    // exposes a steady stream of genuinely distinct devices (phones,
    // headsets, cars, AirTags), and that leak crashed the device after
    // enough of them. maxResults=0 is documented in NimBLEScan.cpp as
    // "none (callbacks only)" — it deletes each device immediately
    // after onResult() returns instead of retaining it, which is
    // exactly this project's usage (everything comes through the
    // per-advertisement callback; the retained-results list and the
    // scan-complete callback below are never read).
    scan->setMaxResults(0);
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
    if (g_rawMode != RawScanMode::NONE) {
        // A raw scan owns the radio right now -- channel hopping here
        // would fight WiFi.scanNetworks()'s own hopping during a WIFI
        // sweep, and the WiFi promiscuous queue is empty anyway (it's
        // disabled for the duration of either raw mode, see
        // startRawBleScan/startRawWifiScan).
        _sd.tick();
        return;
    }
    hopChannel();
    processWiFiQ();
    processDeauthQ();
    expireStale();
    decayChannelActivity();
    _sd.tick();
}

void DetectionEngine::decayChannelActivity() {
    uint32_t now = millis();
    for (uint8_t ch = 1; ch <= 13; ch++) {
        if (_channelActivity[ch] > 0 && now - _channelLastMs[ch] > 200) {
            _channelActivity[ch] = (_channelActivity[ch] > 4) ? _channelActivity[ch] - 4 : 0;
            _channelLastMs[ch] = now;
        }
    }
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
                                         const char* ssid, bool encrypted) {
    if (!mac) return;
    // Group-addressed (broadcast/multicast) destinations can never be a
    // real device: bit 0 of byte 0 is the I/G bit, and every OUI in
    // kOuiTable is a globally-administered unicast prefix with it
    // clear, so these could only ever fall through lookupOui() as
    // UNKNOWN. Rejected here rather than in processWiFiQ() because the
    // queue only holds 7 entries and is drained once per rendered
    // frame -- a slot spent on ff:ff:ff:ff:ff:ff or 01:00:5e:... is a
    // slot a real beacon can't have.
    //
    // This costs the RF-spectrum background's channel-activity level
    // nothing: the data-frame path that produces these is the same one
    // that also posts addr2 (the transmitter, always unicast) with an
    // identical rssi/channel, so every frame still lands in that
    // running maximum exactly once.
    if (mac[0] & 0x01) return;
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
    e.encrypted = encrypted;
    _wifiQHead = next;
}

void IRAM_ATTR DetectionEngine::postDeauth(const uint8_t* mac, int8_t rssi, uint8_t channel) {
    if (!mac) return;
    uint8_t next = (_deauthQHead + 1) % DEAUTH_Q_CAP;
    if (next == _deauthQTail) return;           // queue full, drop
    DeauthQEntry& e = (DeauthQEntry&)_deauthQ[_deauthQHead];
    memcpy((void*)e.mac, mac, 6);
    e.rssi    = rssi;
    e.channel = channel;
    _deauthQHead = next;
}

void DetectionEngine::processDeauthQ() {
    while (_deauthQTail != _deauthQHead) {
        DeauthQEntry e;
        {
            noInterrupts();
            e = (const DeauthQEntry&)_deauthQ[_deauthQTail];
            _deauthQTail = (_deauthQTail + 1) % DEAUTH_Q_CAP;
            interrupts();
        }

        uint32_t now = millis();
        // Rolling window: resets after a gap longer than the window
        // itself rather than a fixed calendar-aligned interval, so an
        // isolated frame (normal traffic) never counts toward a burst
        // that happened long before or after it.
        if (now - _deauthWinStart > DEAUTH_WINDOW_MS) {
            _deauthWinStart = now;
            _deauthWinCount = 0;
        }
        _deauthWinCount++;

        // Window/cooldown bookkeeping above still runs even while
        // disabled, so re-enabling doesn't instantly fire off a stale
        // accumulated count -- only the actual recording is gated.
        if (_deauthWinCount >= DEAUTH_THRESHOLD && (now - _deauthLastFireMs) > DEAUTH_COOLDOWN_MS &&
            Settings::typeEnabled(DetectionType::DEAUTH)) {
            _deauthLastFireMs = now;
            Detection d;
            memset(&d, 0, sizeof(d));
            memcpy(d.mac, e.mac, 6);
            d.rssi    = e.rssi;
            d.channel = e.channel;
            d.type    = DetectionType::DEAUTH;
            strncpy(d.vendor, "Deauth", sizeof(d.vendor) - 1);
            d.firstSeen = d.lastSeen = now;
            // hits doubles as "how many frames triggered this" here,
            // rather than a repeat-sighting count like every other
            // type uses it for -- there's no single persistent device
            // identity behind a flood the way there is for a tracker
            // or camera.
            d.hits   = _deauthWinCount;
            d.active = true;
            pushLog(d);
        }
    }
}

void DetectionEngine::postBle(Detection d) {
    // Disabled types (Settings > DETECTION FILTER) are dropped here,
    // before the dedupe/merge below -- that merge branch re-activates
    // and re-alerts on an already-logged device without ever reaching
    // pushLog(), so gating pushLog() alone would miss it. An entry
    // already in the log for a type disabled after the fact isn't
    // touched or removed; it just stops updating and ages out through
    // the normal expireStale() path like any other device that goes
    // out of range.
    if (!Settings::typeEnabled(d.type)) return;
    // Try to dedupe / merge with existing log entry by MAC
    for (uint8_t i = 0; i < _logCount; i++) {
        uint8_t slot = (_logHead + LOG_CAP - 1 - i) % LOG_CAP;
        if (memcmp(_log[slot].mac, d.mac, 6) == 0 &&
            _log[slot].type == d.type) {
            // Real bug, not a no-op: this used to be
            // `_typeCounts[...] = _typeCounts[...]`, which does
            // nothing. If the device had already gone stale (see
            // expireStale — active=false, counter decremented) and
            // is now seen again, it landed right here on every repeat
            // sighting: hits/rssi/lastSeen updated, but active never
            // flipped back on and the counter never re-incremented —
            // so a device that comes and goes (exactly what an AirTag
            // does) would only ever get counted once, on its very
            // first sighting, and then silently stop being detected
            // for good.
            bool reactivating = !_log[slot].active;
            // hits counts distinct sightings (comes-and-goes, gated by
            // expireStale's active flag), not raw advertisement
            // packets -- a BLE beacon like an AirTag advertises every
            // 1-2s, so incrementing on every packet made this climb
            // into the thousands within an hour of it just sitting
            // nearby instead of meaning anything. rssi/lastSeen still
            // update on every packet regardless, since those drive
            // "is it still actually here" freshness, not the count.
            _log[slot].rssi = d.rssi;
            _log[slot].lastSeen = millis();
            if (reactivating) {
                _log[slot].hits++;
                _log[slot].active = true;
                _log[slot].firstSeen = millis();   // fresh sighting for alert purposes
                _typeCounts[(uint8_t)d.type]++;
                _latest = &_log[slot];
                _latestChangeMs = millis();
            }
            return;
        }
    }
    pushLog(d);
}

void DetectionEngine::postBtClassic(Detection d) {
    if (!Settings::typeEnabled(d.type)) return;
    pushLog(d);
}

void DetectionEngine::postRawBle(RawBleResult r) {
    // Looked up by MAC and updated in place -- this is "everything
    // currently visible", not a chronological log, so a device seen
    // again just refreshes its existing row instead of duplicating it.
    for (uint8_t i = 0; i < _rawBleCount; i++) {
        if (memcmp(_rawBle[i].mac, r.mac, 6) == 0) {
            _rawBle[i].rssi = r.rssi;
            if (r.name[0]) strncpy(_rawBle[i].name, r.name, sizeof(_rawBle[i].name) - 1);
            return;
        }
    }
    if (_rawBleCount < RAW_BLE_CAP) {
        _rawBle[_rawBleCount++] = r;
    }
    // else: full -- ignore further new devices until the next
    // startRawBleScan() resets the list. RAW_BLE_CAP entries is plenty
    // for a single focused sweep and keeps this bounded regardless of
    // how many devices happen to be nearby.
}

void DetectionEngine::startRawBleScan() {
    if (g_rawMode == RawScanMode::WIFI) WiFi.scanDelete();
    _rawBleCount = 0;
    // The continuous NimBLE scan (started once, forever, in init())
    // keeps running -- onResult() just routes into postRawBle() above
    // instead of the signature matcher while g_rawMode == BLE. WiFi's
    // promiscuous capture is switched off so the radio is focused on
    // BLE for the duration, per the "pause the continuous scan and
    // focus on what we're scanning for" design.
    esp_wifi_set_promiscuous(false);
    g_rawMode = RawScanMode::BLE;
    g_rawBleStartMs = millis();
}

bool DetectionEngine::rawBleScanDone() const {
    return g_rawMode == RawScanMode::BLE && (millis() - g_rawBleStartMs) >= RAW_BLE_SCAN_MS;
}

const RawBleResult* DetectionEngine::rawBleAt(uint8_t idx) const {
    if (idx >= _rawBleCount) return nullptr;
    return &_rawBle[idx];
}

void DetectionEngine::startRawWifiScan() {
    // Gate the BLE callback off first (it'd otherwise still be live
    // during the scan) before touching the radio.
    g_rawMode = RawScanMode::WIFI;
    esp_wifi_set_promiscuous(false);
    WiFi.scanNetworks(true /* async */);
}

bool DetectionEngine::rawWifiScanDone() const {
    return g_rawMode == RawScanMode::WIFI && WiFi.scanComplete() >= 0;
}

uint8_t DetectionEngine::rawWifiCount() const {
    if (!rawWifiScanDone()) return 0;
    int n = WiFi.scanComplete();
    return n > 0 ? (uint8_t)n : 0;
}

const char* DetectionEngine::rawWifiSsid(uint8_t idx) const {
    // WiFi.SSID() returns a temporary String -- copy into a static
    // buffer rather than returning a pointer into it (same pattern as
    // macFmt() below).
    static char buf[33];
    buf[0] = 0;
    if (idx < rawWifiCount()) {
        String s = WiFi.SSID(idx);
        strncpy(buf, s.c_str(), sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = 0;
        if (buf[0] == 0) strncpy(buf, "(hidden)", sizeof(buf) - 1);
    }
    return buf;
}

int8_t DetectionEngine::rawWifiRssi(uint8_t idx) const {
    return idx < rawWifiCount() ? (int8_t)WiFi.RSSI(idx) : 0;
}

uint8_t DetectionEngine::rawWifiChannel(uint8_t idx) const {
    return idx < rawWifiCount() ? (uint8_t)WiFi.channel(idx) : 0;
}

bool DetectionEngine::rawWifiOpen(uint8_t idx) const {
    return idx < rawWifiCount() && WiFi.encryptionType(idx) == WIFI_AUTH_OPEN;
}

const uint8_t* DetectionEngine::rawWifiBssid(uint8_t idx) const {
    if (!rawWifiScanDone() || idx >= rawWifiCount()) return nullptr;
    return WiFi.BSSID(idx);
}

void DetectionEngine::stopRawScan() {
    if (g_rawMode == RawScanMode::WIFI) WiFi.scanDelete();
    g_rawMode = RawScanMode::NONE;
    esp_wifi_set_promiscuous(true);
}

void DetectionEngine::watchBle(const uint8_t* mac, const char* name) {
    _watchKind = WatchKind::BLE;
    memcpy(_watchMac, mac, 6);
    strncpy(_watchLabel, (name && name[0]) ? name : "Unnamed device", sizeof(_watchLabel) - 1);
    _watchLabel[sizeof(_watchLabel) - 1] = 0;
    _watchLastHitMs = 0;
    _watchHitFlag   = false;
    _watchRssiHead = _watchRssiCount = 0;
    _watchRssiLastMs = 0;
}

void DetectionEngine::watchWifi(const uint8_t* bssid, const char* ssid) {
    _watchKind = WatchKind::WIFI;
    memcpy(_watchMac, bssid, 6);
    strncpy(_watchLabel, (ssid && ssid[0]) ? ssid : "(hidden)", sizeof(_watchLabel) - 1);
    _watchLabel[sizeof(_watchLabel) - 1] = 0;
    _watchLastHitMs = 0;
    _watchHitFlag   = false;
    _watchRssiHead = _watchRssiCount = 0;
    _watchRssiLastMs = 0;
}

void DetectionEngine::clearWatch() {
    _watchKind    = WatchKind::NONE;
    _watchHitFlag = false;
    _watchRssiHead = _watchRssiCount = 0;
}

bool DetectionEngine::watchHitPending() {
    if (_watchHitFlag) {
        _watchHitFlag = false;
        return true;
    }
    return false;
}

void DetectionEngine::checkWatchBle(const uint8_t* mac, int8_t rssi) {
    if (_watchKind != WatchKind::BLE) return;
    if (memcmp(mac, _watchMac, 6) != 0) return;
    recordWatchRssi(rssi);
    uint32_t now = millis();
    if (now - _watchLastHitMs < WATCH_COOLDOWN_MS) return;
    _watchLastHitMs = now;
    _watchHitFlag   = true;
}

void DetectionEngine::checkWatchWifi(const uint8_t* mac, int8_t rssi) {
    if (_watchKind != WatchKind::WIFI) return;
    if (memcmp(mac, _watchMac, 6) != 0) return;
    recordWatchRssi(rssi);
    uint32_t now = millis();
    if (now - _watchLastHitMs < WATCH_COOLDOWN_MS) return;
    _watchLastHitMs = now;
    _watchHitFlag   = true;
}

// Throttled independently of WATCH_COOLDOWN_MS above -- that gate is
// about not re-popping the full-screen alert every advertisement,
// this is about building up a dense-enough trend to actually plot.
void DetectionEngine::recordWatchRssi(int8_t rssi) {
    uint32_t now = millis();
    if (now - _watchRssiLastMs < WATCH_RSSI_SAMPLE_MS && _watchRssiCount > 0) return;
    _watchRssiLastMs = now;
    _watchRssiHist[_watchRssiHead] = rssi;
    _watchRssiHead = (_watchRssiHead + 1) % WATCH_RSSI_CAP;
    if (_watchRssiCount < WATCH_RSSI_CAP) _watchRssiCount++;
}

int8_t DetectionEngine::watchRssiAt(uint8_t idx) const {
    if (idx >= _watchRssiCount) return 0;
    // Oldest-first: when the buffer hasn't wrapped yet, oldest is slot
    // 0; once it has, oldest is whatever _watchRssiHead is about to
    // overwrite next.
    uint8_t start = (_watchRssiCount < WATCH_RSSI_CAP) ? 0 : _watchRssiHead;
    uint8_t slot = (start + idx) % WATCH_RSSI_CAP;
    return _watchRssiHist[slot];
}

void DetectionEngine::huntBle(const uint8_t* mac, const char* name) {
    _huntKind = WatchKind::BLE;
    memcpy(_huntMac, mac, 6);
    strncpy(_huntLabel, (name && name[0]) ? name : "Unnamed device", sizeof(_huntLabel) - 1);
    _huntLabel[sizeof(_huntLabel) - 1] = 0;
    _huntRssiHead = _huntRssiCount = 0;
    _huntRssiLastMs = 0;
}

void DetectionEngine::huntWifi(const uint8_t* bssid, const char* ssid) {
    _huntKind = WatchKind::WIFI;
    memcpy(_huntMac, bssid, 6);
    strncpy(_huntLabel, (ssid && ssid[0]) ? ssid : "(hidden)", sizeof(_huntLabel) - 1);
    _huntLabel[sizeof(_huntLabel) - 1] = 0;
    _huntRssiHead = _huntRssiCount = 0;
    _huntRssiLastMs = 0;
}

void DetectionEngine::clearHunt() {
    _huntKind = WatchKind::NONE;
    _huntRssiHead = _huntRssiCount = 0;
}

void DetectionEngine::checkHuntBle(const uint8_t* mac, int8_t rssi) {
    if (_huntKind != WatchKind::BLE) return;
    if (memcmp(mac, _huntMac, 6) != 0) return;
    recordHuntRssi(rssi);
}

void DetectionEngine::checkHuntWifi(const uint8_t* mac, int8_t rssi) {
    if (_huntKind != WatchKind::WIFI) return;
    if (memcmp(mac, _huntMac, 6) != 0) return;
    recordHuntRssi(rssi);
}

void DetectionEngine::recordHuntRssi(int8_t rssi) {
    uint32_t now = millis();
    if (now - _huntRssiLastMs < WATCH_RSSI_SAMPLE_MS && _huntRssiCount > 0) return;
    _huntRssiLastMs = now;
    _huntRssiHist[_huntRssiHead] = rssi;
    _huntRssiHead = (_huntRssiHead + 1) % WATCH_RSSI_CAP;
    if (_huntRssiCount < WATCH_RSSI_CAP) _huntRssiCount++;
}

int8_t DetectionEngine::huntRssiAt(uint8_t idx) const {
    if (idx >= _huntRssiCount) return 0;
    uint8_t start = (_huntRssiCount < WATCH_RSSI_CAP) ? 0 : _huntRssiHead;
    uint8_t slot = (start + idx) % WATCH_RSSI_CAP;
    return _huntRssiHist[slot];
}

// Same vendor, ignoring the locally-administered bit. Multi-SSID and
// mesh APs routinely derive per-radio BSSIDs by setting that bit on
// their base MAC (34:12:98 becomes 36:12:98) -- byte 0 differs by 2 and
// a plain memcmp calls that a different manufacturer, which is how the
// first version of this managed to flag an entire mesh network.
static inline bool sameVendor(const uint8_t* a, const uint8_t* b) {
    return ((a[0] & ~0x02) == (b[0] & ~0x02)) && a[1] == b[1] && a[2] == b[2];
}

// See the AP table's comment in detection.h for why the encryption
// mismatch is the actual test and what it trades away.
//
// One inherent limitation worth naming: whichever BSSID is seen first
// becomes the baseline. If a rogue is already up when you arrive, it
// gets recorded as legitimate and the real AP is what trips the alert.
// The pair is still surfaced either way -- the device is telling you
// two boxes claim one name and disagree about security, which is the
// finding; it can't tell you which of them is lying.
bool DetectionEngine::noteApBeacon(const uint8_t* bssid, const char* ssid, bool encrypted) {
    for (uint8_t i = 0; i < _apCount; i++) {
        if (strncmp(_aps[i].ssid, ssid, sizeof(_aps[i].ssid) - 1) != 0) continue;
        // Same SSID, same BSSID -- just this AP beaconing again. Refresh
        // the posture so a legitimate security change re-baselines
        // rather than alerting forever after.
        if (memcmp(_aps[i].bssid, bssid, 6) == 0) {
            _aps[i].encrypted = encrypted;
            return false;
        }
        // Same hardware vendor -- mesh node, or the other band of the
        // same box. Never flagged, whatever else it says.
        if (sameVendor(_aps[i].bssid, bssid)) return false;
        // Different vendor, but both agree on security. Can't tell a
        // rogue from a mixed-vendor network here, so stay quiet.
        if (_aps[i].encrypted == encrypted) return false;
        // Different vendor AND disagreeing about encryption: one of
        // these two is not what it claims to be.
        return true;
    }
    // First sighting of this SSID: record it as the baseline. Round-
    // robin eviction once full, so a busy area can't grow this without
    // bound.
    ApEntry& slot = (_apCount < AP_CAP) ? _aps[_apCount++] : _aps[_apNext];
    if (_apCount >= AP_CAP) _apNext = (uint8_t)((_apNext + 1) % AP_CAP);
    strncpy(slot.ssid, ssid, sizeof(slot.ssid) - 1);
    slot.ssid[sizeof(slot.ssid) - 1] = 0;
    memcpy(slot.bssid, bssid, 6);
    slot.encrypted = encrypted;
    return false;
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
        // Checked for every dequeued frame, regardless of what (if
        // anything) it ends up matching below -- a watched AP's own
        // MAC shows up here as addr2 (probe/data) or addr3/BSSID
        // (beacon), same offsets postWiFi() was already called with.
        checkWatchWifi(e.mac, e.rssi);
        checkHuntWifi(e.mac, e.rssi);
        // Every captured frame feeds the spectrum-waterfall's channel
        // activity level, whether or not it ends up matching anything
        // below — this is meant to reflect real ambient RF traffic,
        // not just known-vendor hits.
        if (e.channel >= 1 && e.channel <= 13) {
            int level = ((int)e.rssi + 90) * 100 / 60;
            if (level < 0) level = 0;
            if (level > 100) level = 100;
            if ((uint8_t)level > _channelActivity[e.channel]) _channelActivity[e.channel] = (uint8_t)level;
            _channelLastMs[e.channel] = millis();
        }
        // Evil-twin check runs ahead of the signature lookups and wins
        // over them. "This SSID is beaconing from a second, different-
        // vendor BSSID" is a statement about the *network*, not about
        // whichever radio chip happens to be in this particular box --
        // and a rogue AP built on commodity hardware would otherwise be
        // logged as whatever its OUI matched, or dropped as UNKNOWN,
        // burying the thing actually worth saying. Only beacons carry
        // an SSID (see the promiscuous callback), so this is naturally
        // limited to them.
        DetectionType t = DetectionType::UNKNOWN;
        bool matchedBySsid = false;
        bool evilTwin = e.ssid[0] && noteApBeacon(e.mac, e.ssid, e.encrypted);
        if (evilTwin) {
            t = DetectionType::EVILTWIN;
        } else {
            // Check OUI first (per DESIGN.md §6.2 precedence); fall back
            // to the SSID prefix (e.g. an Axon/Flock unit in pairing
            // mode, broadcasting from a WiFi module OUI we don't
            // otherwise know) if the OUI itself didn't match anything.
            t = lookupOui(e.mac);
            if (t == DetectionType::UNKNOWN && e.ssid[0]) {
                t = lookupSsid(e.ssid);
                matchedBySsid = (t != DetectionType::UNKNOWN);
            }
        }
        if (t == DetectionType::UNKNOWN) continue;
        // Disabled types (Settings > DETECTION FILTER) dropped here too
        // -- same reasoning as postBle()'s guard: the dedupe/merge loop
        // just below can re-activate and re-count an already-logged
        // device without ever reaching pushLog().
        if (!Settings::typeEnabled(t)) continue;
        // Try to dedupe / merge. Same reactivation fix as postBle()'s
        // merge branch — a device that went stale and comes back needs
        // active flipped back on and the counter bumped again, or it
        // silently stops being counted after its first sighting.
        bool merged = false;
        for (uint8_t i = 0; i < _logCount; i++) {
            uint8_t slot = (_logHead + LOG_CAP - 1 - i) % LOG_CAP;
            if (memcmp(_log[slot].mac, e.mac, 6) == 0 &&
                _log[slot].type == t) {
                bool reactivating = !_log[slot].active;
                _log[slot].hits++;
                _log[slot].rssi = e.rssi;
                _log[slot].lastSeen = millis();
                _log[slot].channel = e.channel;
                if (reactivating) {
                    _log[slot].active = true;
                    _log[slot].firstSeen = millis();
                    _typeCounts[(uint8_t)t]++;
                    _latest = &_log[slot];
                    _latestChangeMs = millis();
                }
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
        // matched, otherwise from the OUI table. An evil twin gets
        // neither -- what matters is which network is being
        // impersonated, so the SSID goes in as the name.
        if (t == DetectionType::EVILTWIN) {
            strncpy(d.vendor, "EvilTwin", sizeof(d.vendor) - 1);
            strncpy(d.name, e.ssid, sizeof(d.name) - 1);
        } else if (matchedBySsid) {
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
    if ((uint8_t)d.type < (uint8_t)DetectionType::COUNT) {
        _lifetimeByType[(uint8_t)d.type]++;
        saveLifetimeByType();
    }
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
