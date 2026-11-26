// SquachWatch-CM0 -- the NimBLEDevice stub.
//
// The four calls the firmware makes: init (with the device name, which
// the watch passes empty -- it advertises manufacturer data, not a name),
// the isInitialized() read, and the singleton scan/advertising handles.
// getAddress() answers the zero address until Phase 3 reads the real one
// from the controller -- the mesh nonce carries it, so a wrong value
// would show up as squad messages that never round-trip, which is the
// test Phase 3 exists to pass.
#pragma once
#include "NimBLEAddress.h"
#include "NimBLEUUID.h"
#include "NimBLEScan.h"
#include "NimBLEAdvertising.h"

class NimBLEDevice {
public:
    static bool init(const std::string& deviceName);
    static bool isInitialized();
    static NimBLEScan* getScan();
    static NimBLEAdvertising* getAdvertising();
    static NimBLEAddress getAddress();
    static bool setPowerLevel(int8_t power) { (void)power; return false; }
};
