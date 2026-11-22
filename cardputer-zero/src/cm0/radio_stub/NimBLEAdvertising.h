// SquachWatch-CM0 -- the NimBLEAdvertising stub.
//
// The surface detection.cpp's mesh transmit path uses: the advertisement
// data object (manufacturer data is where the squad frames ride), the
// discoverable/connectable modes (both NON -- the watch advertises, it
// does not hand out a GATT), the interval pair, and start/stop with the
// isAdvertising() read-back the firmware's bookkeeping trusts.
//
// The BLE_GAP_*_MODE names come from the classic esp_gap_bt_api.h the
// firmware used to include; only the two _NON values are referenced.
#pragma once
#include <cstdint>
#include <string>

#define BLE_GAP_DISC_MODE_NON     0
#define BLE_GAP_DISC_MODE_LIMITED 1
#define BLE_GAP_DISC_MODE_GENERAL 2

#define BLE_GAP_CONN_MODE_NON     0
#define BLE_GAP_CONN_MODE_DIRECT  1

class NimBLEAdvertisementData {
public:
    NimBLEAdvertisementData() {}
    // As NimBLE: the field is the company ID (LE) then the data; it must
    // fit one legacy advert with its 2-byte AD header (<= 29 bytes).
    bool setManufacturerData(const std::string& data) {
        if (data.size() + 2 > 31) return false;
        _mfg = data;
        return true;
    }
    bool setServiceData(const std::string& data) { (void)data; return false; }
    bool setName(const std::string& name) { (void)name; return false; }
    bool isComplete() const { return true; }
    // The AD bytes as sent: one manufacturer-specific structure (the only
    // thing the firmware puts in an advert), or nothing.
    std::string payload() const {
        if (_mfg.empty()) return std::string();
        std::string p;
        p.push_back((char)(_mfg.size() + 1));
        p.push_back((char)0xFF);
        p += _mfg;
        return p;
    }

private:
    std::string _mfg;
};

// Real advertising since 2026-11-19: start() sends the data over HCI
// (ble_scan.cpp, legacy ADV_NONCONN_IND from the public address -- what
// NON/NON modes mean). The data is copied at setAdvertisementData(): the
// firmware passes a NimBLEAdvertisementData on its own stack.
class NimBLEAdvertising {
public:
    bool setAdvertisementData(const NimBLEAdvertisementData& data) { _ad = data.payload(); return true; }
    bool setScanResponseData(const NimBLEAdvertisementData& data) { (void)data; return true; }
    bool setDiscoverableMode(int mode) { _discMode = mode; return true; }
    bool setConnectableMode(int mode) { _connMode = mode; return true; }
    // NimBLE units: 0.625 ms (the firmware converts from ms itself).
    bool setMinInterval(uint16_t units) { _min = units; return true; }
    bool setMaxInterval(uint16_t units) { _max = units; return true; }
    bool enableScanResponse(bool enabled) { _scanResp = enabled; return true; }
    bool start();   // radio_stub.cpp
    bool stop();
    bool isAdvertising() const { return _advertising; }
    // The AD bytes of the last setAdvertisementData() (selftest).
    const std::string& adBytes() const { return _ad; }
    uint16_t minUnits() const { return _min; }

private:
    bool _advertising = false;
    bool _scanResp = false;
    int _discMode = BLE_GAP_DISC_MODE_NON;
    int _connMode = BLE_GAP_CONN_MODE_NON;
    uint16_t _min = 0, _max = 0;
    std::string _ad;
};
