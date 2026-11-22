// SquachWatch-CM0 -- the NimBLEAdvertisedDevice stub.
//
// The full read surface detection.cpp's handle(adv) walks: RSSI, name,
// address, advert type and the legacy/scannable pair, manufacturer data
// (count + per-index + whole), service UUIDs (count + per-index), and the
// raw payload. Phase 1 delivers nothing through it (the stub scanner never
// starts); Phase 3's raw-HCI scanner parses the AD structures in the
// constructor defined in radio_stub.cpp and hands one of these to
// engine.postBle().
#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include "NimBLEAddress.h"
#include "NimBLEUUID.h"

// The advert types the firmware switches on, from the classic NimBLE
// HCI header.
#define BLE_HCI_ADV_TYPE_ADV_IND             0x00
#define BLE_HCI_ADV_TYPE_ADV_DIRECT_IND_HD   0x01
#define BLE_HCI_ADV_TYPE_ADV_SCAN_IND        0x02
#define BLE_HCI_ADV_TYPE_ADV_NONCONN_IND     0x03

class NimBLEAdvertisedDevice {
public:
    NimBLEAdvertisedDevice() {}
    // The scanner's door: raw advertising data plus the address fields of
    // the HCI LE Advertising Report. Defined in radio_stub.cpp.
    NimBLEAdvertisedDevice(const uint8_t* advData, size_t len, int rssi, const uint8_t* addr, uint8_t advType);

    int16_t getRSSI() const { return _rssi; }
    bool haveName() const { return !_name.empty(); }
    std::string getName() const { return _name; }
    // By reference, as NimBLE-Arduino 2.x: detection.cpp keeps
    // `getAddress().getBase()->val` as a pointer for the whole callback.
    // By value that pointed into a dead temporary -- every BLE detection
    // got a garbage address (found by --selftest, 2026-11-19).
    const NimBLEAddress& getAddress() const { return _addr; }
    uint8_t getAdvType() const { return _advType; }
    bool isLegacyAdvertisement() const { return _advType < 8; }
    bool isScannable() const { return _advType == BLE_HCI_ADV_TYPE_ADV_IND || _advType == BLE_HCI_ADV_TYPE_ADV_SCAN_IND; }

    bool haveManufacturerData() const { return !_mfgData.empty(); }
    int  getManufacturerDataCount() const { return (int)_mfgData.size(); }
    std::string getManufacturerData() const { return _mfgData.empty() ? std::string() : _mfgData[0]; }
    std::string getManufacturerData(int index) const { return (size_t)index < _mfgData.size() ? _mfgData[index] : std::string(); }

    bool haveServiceUUID() const { return !_uuids.empty(); }
    int  getServiceUUIDCount() const { return (int)_uuids.size(); }
    NimBLEUUID getServiceUUID(int index) const { return (size_t)index < _uuids.size() ? _uuids[index] : NimBLEUUID(); }

    const std::vector<uint8_t>& getPayload() const { return _payload; }

private:
    int16_t _rssi = -127;
    std::string _name;
    NimBLEAddress _addr;
    uint8_t _advType = 0;
    std::vector<std::string> _mfgData;
    std::vector<NimBLEUUID> _uuids;
    std::vector<uint8_t> _payload;
};
