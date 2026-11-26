// SquachWatch-CM0 -- the radio stub definitions.
//
// Phase 1: every radio call is a stateful no-op, so the firmware's real
// detection.cpp boots, the UI runs, and every screen that reads the
// radio's state reads consistent zeros: a wifi radio that is up and
// sniffs channel 1 but sees nothing, a BLE controller that is enabled
// with a scanner that is running but has not heard a single advert, an
// SD card that is not there.
//
// Phase 2/3 replace the bodies below (in this file or its successors,
// wifi_mon.cpp and a raw-HCI scanner) with the USB adapter's monitor vif
// and the CYW4343A2's HCI, and start delivering frames and adverts
// through the same callbacks the firmware already registered.
#include "esp_wifi.h"
#include "wifi_mon.h"
#include "ble_scan.h"
#include "esp_netif.h"
#include "esp_bt.h"
#include "WiFi.h"
#include "SD.h"
#include "NimBLEDevice.h"
#include "NimBLEScan.h"
#include "NimBLEAdvertising.h"
#include "NimBLEAdvertisedDevice.h"
#include "nimble/porting/nimble/include/nimble/nimble_port.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <vector>

// ------------------------------------------------------------------ esp_wifi
namespace {
    struct WifiState {
        wifi_mode_t  mode = WIFI_MODE_NULL;
        bool         started = false;
        bool         promisc = false;
        uint8_t      channel = 1;
        uint32_t     filterMask = 0;
        wifi_promiscuous_rx_cb_t rxCb = nullptr;
    };
    WifiState& wifiSt() {
        static WifiState s;
        return s;
    }
}   // namespace

esp_err_t esp_wifi_init(const wifi_init_config_t* config) {
    (void)config;
    return ESP_OK;
}
esp_err_t esp_wifi_deinit(void) {
    WifiState& s = wifiSt();
    s.started = false;
    s.promisc = false;
    s.mode = WIFI_MODE_NULL;
    return ESP_OK;
}
esp_err_t esp_wifi_start(void) { wifiSt().started = true; return ESP_OK; }
esp_err_t esp_wifi_stop(void) {
    WifiState& s = wifiSt();
    s.started = false;
    s.promisc = false;
    return ESP_OK;
}
esp_err_t esp_wifi_set_mode(wifi_mode_t mode) { wifiSt().mode = mode; return ESP_OK; }
esp_err_t esp_wifi_get_mode(wifi_mode_t* mode) {
    if (mode) *mode = wifiSt().mode;
    return ESP_OK;
}
esp_err_t esp_wifi_set_channel(uint8_t primary, wifi_second_chan_t) {
    if (!primary) return ESP_OK;
    // A 5 GHz channel is accepted only when the sniffer's wiphy lists it as
    // enabled under the current regulatory domain (the CM0's onboard
    // dual-band card is not the sniffer -- the probe looks at the USB
    // adapter's wiphy). The firmware's boot probe runs on this
    // accept/reject, per channel, exactly as on the C5 where the driver
    // applies the country's list: a 2.4-only adapter rejects all 28 (the 5G
    // hop path stays off); a dual-band one accepts what the country allows.
    //
    // Known gap: ESP_OK means "queued for the feed thread", not "tuned".
    // A set the driver later refuses is logged by wifi_mon and the radio
    // stays where it was; frames still carry their true radiotap channel.
    if (primary > 14 && !cm0::wifiMonChannelUsable(primary)) return ESP_FAIL;
    cm0::wifiMonSetChannel(primary);
    wifiSt().channel = primary;
    return ESP_OK;
}
esp_err_t esp_wifi_get_channel(uint8_t* primary, wifi_second_chan_t* secondary) {
    if (primary) *primary = wifiSt().channel;
    if (secondary) *secondary = WIFI_SECOND_CHAN_NONE;
    return ESP_OK;
}
esp_err_t esp_wifi_set_band_mode(wifi_band_mode_t) { return ESP_OK; }
esp_err_t esp_wifi_set_promiscuous(bool enable) {
    cm0::wifiMonSetPromisc(enable);
    wifiSt().promisc = enable;
    return ESP_OK;
}
esp_err_t esp_wifi_get_promiscuous(bool* enable) {
    if (enable) *enable = wifiSt().promisc;
    return ESP_OK;
}
esp_err_t esp_wifi_set_promiscuous_filter(const wifi_promiscuous_filter_t* filter) {
    if (filter) {
        wifiSt().filterMask = filter->filter_mask;
        cm0::wifiMonSetFilter(filter->filter_mask);
    }
    return ESP_OK;
}
esp_err_t esp_wifi_set_promiscuous_rx_cb(wifi_promiscuous_rx_cb_t cb) {
    wifiSt().rxCb = cb;
    cm0::wifiMonRegisterRx(cb);
    return ESP_OK;
}
esp_err_t esp_wifi_scan_start(const wifi_scan_config_t*, bool) { return ESP_OK; }
esp_err_t esp_wifi_scan_get_ap_num(uint16_t* num) {
    if (num) *num = 0;
    return ESP_OK;
}
esp_err_t esp_wifi_scan_get_ap_records(uint16_t* num, wifi_ap_record_t*) {
    if (num) *num = 0;
    return ESP_OK;
}
esp_err_t esp_wifi_clear_ap_list(void) { return ESP_OK; }
esp_err_t esp_wifi_get_max_tx_power(int8_t* tx_power) {
    if (tx_power) *tx_power = 20;
    return ESP_OK;
}
esp_err_t esp_wifi_get_country(wifi_country_t* country) {
    if (country) {
        country->cc[0] = 'U';
        country->cc[1] = 'S';
        country->cc[2] = '\0';
        country->schan = 1;
        country->nchan = 13;
    }
    return ESP_OK;
}

// -------------------------------------------------------------- esp_netif
static esp_netif_t s_staNetif;   // one station netif; attach succeeds

esp_netif_t* esp_netif_get_handle_from_ifkey(const char*) {
    return &s_staNetif;
}
esp_err_t esp_netif_attach_wifi_station(esp_netif_t*) {
    return ESP_OK;
}

// ----------------------------------------------------------------- esp_bt
int esp_bt_controller_get_status(void) {
    // The BLE half of this app is the app's own scanner (Phase 3); the
    // firmware must see an enabled controller or it stops using the radio.
    return ESP_BT_CONTROLLER_STATUS_ENABLED;
}

// ------------------------------------------------------------------ WiFi
WiFiClass WiFi;

// The scan: wifi_mon runs the sweep on its feed thread; the firmware's
// getters read a snapshot copied here (UI thread) once it is done, so a
// pointer from BSSID(i) stays valid until the next scan or scanDelete().
namespace {
    std::vector<cm0::WifiScanAp> s_scanSnap;
    bool s_scanSnapValid = false;
    bool s_scanStubDone  = false;   // no sniffer: "finished, nothing found"
    const cm0::WifiScanAp* scanAp(uint8_t i) {
        return (s_scanSnapValid && i < s_scanSnap.size()) ? &s_scanSnap[i] : nullptr;
    }
}

int WiFiClass::scanNetworks(bool async, bool showHiddenAPs, bool passive, uint32_t maxMsPerChan) {
    (void)showHiddenAPs;   // hidden networks are never listed (no probe to reveal them)
    (void)passive;         // always passive: the sniffer does not transmit
    s_scanSnap.clear();
    s_scanSnapValid = false;
    s_scanStubDone = false;
    const uint16_t dwell = (uint16_t)(maxMsPerChan ? (maxMsPerChan > 1000 ? 1000 : maxMsPerChan) : 150);
    if (!cm0::wifiMonScanStart(dwell)) { s_scanStubDone = true; return 0; }
    if (async) return -1;   // WIFI_SCAN_RUNNING
    // Nothing in this build calls the blocking form; it would stall loop().
    int st;
    while ((st = cm0::wifiMonScanState()) == -1) { struct timespec ts = {0, 20000000}; nanosleep(&ts, nullptr); }
    return scanComplete();
}

int8_t WiFiClass::scanComplete() {
    if (s_scanStubDone) return 0;
    if (s_scanSnapValid) return (int8_t)s_scanSnap.size();
    const int st = cm0::wifiMonScanState();
    if (st < 0) return (int8_t)st;   // -1 running, -2 not started
    s_scanSnap.resize((size_t)st);
    s_scanSnap.resize(cm0::wifiMonScanResults(s_scanSnap.data(), s_scanSnap.size()));
    s_scanSnapValid = true;
    std::fprintf(stderr, "[wifi_scan] passive sweep done: %u network(s)\n",
                 (unsigned)s_scanSnap.size());
    return (int8_t)s_scanSnap.size();
}

void WiFiClass::scanDelete() {
    cm0::wifiMonScanClear();
    s_scanSnap.clear();
    s_scanSnapValid = false;
    s_scanStubDone = false;
}

String WiFiClass::SSID(uint8_t i) { const auto* a = scanAp(i); return a ? String(a->ssid) : String(); }
int WiFiClass::RSSI(uint8_t i) { const auto* a = scanAp(i); return a ? a->rssi : 0; }
uint8_t WiFiClass::channel(uint8_t i) { const auto* a = scanAp(i); return a ? a->channel : 0; }
WIFICIPHER WiFiClass::encryptionType(uint8_t i) {
    const auto* a = scanAp(i);
    // The beacon's Privacy bit says open or not; which cipher is not needed
    // by any caller (the screen only asks "== OPEN").
    return (a && a->privacy) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
}
const uint8_t* WiFiClass::BSSID(uint8_t i) { const auto* a = scanAp(i); return a ? a->bssid : nullptr; }

// -------------------------------------------------------------------- SD
// SD: sd_card.cpp (the card folder).

// --------------------------------------------------------------- NimBLE
namespace {
    NimBLEScan      s_scan;
    NimBLEAdvertising s_adv;
    bool            s_bleInit = false;
}   // namespace

bool NimBLEDevice::init(const std::string& deviceName) {
    (void)deviceName;
    s_bleInit = true;
    return true;
}
bool NimBLEDevice::isInitialized() {
    return s_bleInit;
}
NimBLEScan* NimBLEDevice::getScan() {
    return &s_scan;
}
NimBLEAdvertising* NimBLEDevice::getAdvertising() {
    return &s_adv;
}
NimBLEAddress NimBLEDevice::getAddress() {
    // The controller's public address (HCI Read_BD_ADDR), which is also
    // what our adverts carry (public own-address type) -- the mesh nonce
    // must match what peers see. Read once; zero with no controller.
    static NimBLEAddress s_addr;
    static bool s_have = false;
    if (!s_have) {
        uint8_t a[6];
        if (cm0::bleReadBdAddr(a)) {
            s_addr = NimBLEAddress(a);
            s_have = true;
            std::fprintf(stderr, "[ble] own address %02X:%02X:%02X:%02X:%02X:%02X\n",
                         a[5], a[4], a[3], a[2], a[1], a[0]);
        }
    }
    return s_addr;
}

bool NimBLEAdvertising::start() {
    if (_ad.empty()) return false;
    const int r = cm0::bleAdvStart((const uint8_t*)_ad.data(), (uint8_t)_ad.size(),
                                   _min ? _min : 0x0800, _max ? _max : 0x0800);
    // 0 = no controller: "advertising" into the void, as the scan "runs"
    // with nothing to hear -- the firmware's bookkeeping stays consistent.
    _advertising = r >= 0;
    return _advertising;
}
bool NimBLEAdvertising::stop() {
    cm0::bleAdvStop();
    _advertising = false;
    return true;
}

bool NimBLEScan::start(uint32_t duration, bool continuous, bool firstScan) {
    (void)duration; (void)continuous; (void)firstScan;   // always "forever" here
    // The scan "runs" either way, so the firmware's bookkeeping stays
    // self-consistent; with no controller (or no CAP_NET_RAW) it simply
    // hears nothing, as in Phase 1 (ble_scan logs why, once).
    (void)cm0::bleScanStart(_active, _interval, _window, _dupFilter);
    _scanning = true;
    return true;
}
bool NimBLEScan::stop() {
    cm0::bleScanStop();
    _scanning = false;
    return true;
}

// The adverts, on the UI thread (the port's threading invariant: firmware
// callbacks never run on a feed thread). NimBLE's delivery order:
// onDiscovered for every advert; onResult at once for a passive scan or an
// advert nobody may ask (not scannable); in an active scan a scannable
// advert waits for its SCAN_RSP, and onResult gets the advert with the
// response's AD structures appended. A device that never answers is not
// re-reported here -- the firmware already handles it at first sight in
// onDiscovered for exactly that case (detection.cpp, BleScanCallbacks).
namespace {
    struct PendingAdv { cm0::BleAdvRec rec; uint32_t at; };
    std::vector<PendingAdv> s_pending;   // active scan, awaiting SCAN_RSP
    const size_t PENDING_CAP = 64;
}

namespace cm0 {
size_t blePump(size_t maxReports) {
    std::vector<BleAdvRec> recs;
    if (!bleScanDrain(recs, maxReports)) return 0;
    NimBLEScanCallbacks* cbs = s_scan.callbacks();
    if (!cbs || !s_scan.isScanning()) return 0;
    const bool active = s_scan.isActiveScan();
    const uint32_t now = (uint32_t)time(nullptr);
    for (const BleAdvRec& r : recs) {
        if (r.evtType == 4) {   // SCAN_RSP: merge into its waiting advert
            for (size_t i = 0; i < s_pending.size(); i++) {
                const BleAdvRec& a = s_pending[i].rec;
                if (memcmp(a.addr, r.addr, 6) != 0 || a.addrType != r.addrType) continue;
                uint8_t merged[62];
                memcpy(merged, a.data, a.len);
                memcpy(merged + a.len, r.data, r.len);
                NimBLEAdvertisedDevice dev(merged, (size_t)a.len + r.len, r.rssi, a.addr, a.evtType);
                cbs->onResult(&dev);
                s_pending.erase(s_pending.begin() + (long)i);
                break;
            }
            continue;
        }
        NimBLEAdvertisedDevice dev(r.data, r.len, r.rssi, r.addr, r.evtType);
        cbs->onDiscovered(&dev);
        const bool scannable = r.evtType == 0 || r.evtType == 2;
        if (!active || !scannable) { cbs->onResult(&dev); continue; }
        bool found = false;
        for (PendingAdv& p : s_pending)
            if (memcmp(p.rec.addr, r.addr, 6) == 0 && p.rec.addrType == r.addrType) {
                p.rec = r; p.at = now; found = true; break;
            }
        if (!found) {
            if (s_pending.size() >= PENDING_CAP) s_pending.erase(s_pending.begin());   // oldest
            s_pending.push_back({r, now});
        }
    }
    // Forget waits older than a few seconds (NimBLE's reply timeout is
    // ~180 ms; nothing is re-reported, see above).
    s_pending.erase(std::remove_if(s_pending.begin(), s_pending.end(),
                                   [now](const PendingAdv& p) { return now - p.at > 3; }),
                    s_pending.end());
    return recs.size();
}
}   // namespace cm0

// ------------------------------------------------- NimBLEAdvertisedDevice
//
// The scanner's door. Phase 3's raw-HCI code parses LE Advertising
// Reports into this constructor; the parse is written now so the scanner
// only has to hand over bytes. Standard AD structure walk:
//   len type data...  (type 0x09 name, 0xFF manufacturer, 0x03/0x06/0x10
//   service UUIDs; complete- and short-form both).
NimBLEAdvertisedDevice::NimBLEAdvertisedDevice(const uint8_t* advData, size_t len, int rssi,
                                               const uint8_t* addr, uint8_t advType)
    : _rssi((int16_t)rssi), _advType(advType) {
    _payload.assign(advData, advData + len);
    if (addr) _addr = NimBLEAddress(addr);

    // Core Supplement Part A §1: each structure is {Length, Type, Data}
    // where Length counts the Type byte, so the data is Length - 1 bytes
    // and the next structure starts Length + 1 on. (This walk used to
    // treat Length as the data size: everything after the first structure
    // was misread, and a 31-byte single structure -- a squad peer's
    // advert, an AirTag's -- was dropped whole. Found by --selftest.)
    size_t off = 0;
    while (off + 2 <= len) {
        const uint8_t fLen = advData[off];
        if (fLen == 0) break;                       // early terminator / padding
        if (off + 1 + fLen > len) break;            // truncated: keep what we have
        const uint8_t fType = advData[off + 1];
        const uint8_t* d = advData + off + 2;
        const size_t dLen = (size_t)fLen - 1;

        if (fType == 0x09 || (fType == 0x08 && _name.empty())) {
            // Complete local name; the shortened one only if no complete.
            _name.assign(reinterpret_cast<const char*>(d), dLen);
        } else if (fType == 0xFF && dLen >= 2) {
            // As NimBLE: the whole field, company ID (LE) first -- the
            // firmware reads md[0..1] as the company (mesh, wardrive).
            _mfgData.emplace_back(reinterpret_cast<const char*>(d), dLen);
        } else if (fType == 0x02 || fType == 0x03) {
            // Incomplete / complete list of 16-bit UUIDs, little-endian.
            for (size_t i = 0; i + 1 < dLen; i += 2)
                _uuids.emplace_back((uint16_t)((uint16_t)d[i] | ((uint16_t)d[i + 1] << 8)));
        } else if (fType == 0x06 || fType == 0x07) {
            // Incomplete / complete list of 128-bit UUIDs, each little-
            // endian on air; kept MSB first (the string form's order).
            for (size_t i = 0; i + 15 < dLen; i += 16) {
                uint8_t be[16];
                for (int k = 0; k < 16; k++) be[k] = d[i + 15 - k];
                _uuids.emplace_back(be, 16);
            }
        }
        off += 1 + fLen;
    }
}

// ---------------------------------------------------------- NimBLEUUID
namespace {
    uint8_t hexVal(char c) {
        if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
        if (c >= 'a' && c <= 'f') return (uint8_t)(c - 'a' + 10);
        if (c >= 'A' && c <= 'F') return (uint8_t)(c - 'A' + 10);
        return 0xFF;
    }
}   // namespace

NimBLEUUID::NimBLEUUID(const uint8_t* data, size_t len) : _size(0) {
    if (len == 2) {
        _u16 = (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
        _size = 2;
    } else if (len == 16) {
        for (size_t i = 0; i < 16; i++) _u128[i] = data[i];
        _size = 16;
    }
}

NimBLEUUID::NimBLEUUID(const std::string& s) : _size(0) {
    parseString(s);
}

void NimBLEUUID::parseString(const std::string& s) {
    // Dashes and braces (UUID string form) are skipped; 4 hex digits are a
    // 16-bit value, 32 a 128-bit one, anything else an empty UUID.
    std::string hex;
    for (char c : s)
        if (isxdigit((unsigned char)c)) hex += (char)tolower((unsigned char)c);
    if (hex.size() == 4) {
        _u16 = (uint16_t)strtol(hex.c_str(), nullptr, 16);
        _size = 2;
    } else if (hex.size() == 32) {
        for (int i = 0; i < 16; i++) {
            char two[3] = { hex[2 * i], hex[2 * i + 1], 0 };
            _u128[i] = hexVal(two[0]) * 16 + hexVal(two[1]);
        }
        _size = 16;
    }
}

bool NimBLEUUID::equals(const NimBLEUUID& other) const {
    if (_size != other._size) return false;
    if (_size == 2) return _u16 == other._u16;
    if (_size == 16) return memcmp(_u128, other._u128, 16) == 0;
    return true;
}

std::string NimBLEUUID::toString() const {
    char buf[37];
    if (_size == 2) {
        snprintf(buf, sizeof buf, "%04x", _u16);
    } else if (_size == 16) {
        for (int i = 0; i < 16; i++) snprintf(buf + 2 * i, 3, "%02x", _u128[i]);
    } else {
        buf[0] = 0;
    }
    return std::string(buf);
}

// -------------------------------------------------------- nimble_port
void ble_npl_event_init(struct ble_npl_event* ev, ble_npl_event_fn_t fn, void* arg) {
    ev->fn = fn;
    ev->arg = arg;
    ev->link.next = nullptr;
}
void ble_npl_eventq_put(void* eventq, struct ble_npl_event* ev) {
    (void)eventq;
    (void)ev;
    // Phase 1: no host task, no delivery. Nothing is posted, so nothing
    // is lost; Phase 3's scanner owns its own queue and delivers for real.
}
void* nimble_port_get_dflt_eventq(void) {
    static int s_defaultQueue;
    return &s_defaultQueue;
}
