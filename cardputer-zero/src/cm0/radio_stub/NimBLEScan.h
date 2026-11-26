// SquachWatch-CM0 -- the NimBLEScan stub.
//
// The read surface plus the configuration calls detection.cpp's init and
// its scan-flush path make: the passive/active switch, interval, window,
// duplicate filter, response timeout, max results, the callbacks object,
// and start/stop. The stub records state and reports "scanning" once
// start() is called, so the firmware's bookkeeping (isScanning(), the
// flush's stop/start dance) is self-consistent. No advert is ever
// delivered until Phase 3's scanner runs.
//
// NimBLEScanCallbacks carries the two virtuals the firmware's
// BleScanCallbacks overrides: onDiscovered (every advert, first sight)
// and onResult (a device after its reply wait).
#pragma once
#include <cstdint>

class NimBLEAdvertisedDevice;

class NimBLEScanCallbacks {
public:
    virtual ~NimBLEScanCallbacks() {}
    virtual void onResult(const NimBLEAdvertisedDevice* adv) { (void)adv; }
    virtual void onDiscovered(const NimBLEAdvertisedDevice* adv) { (void)adv; }
};

class NimBLEScan {
public:
    void setActiveScan(bool active) { _active = active; }
    bool isActiveScan() const { return _active; }
    void setInterval(uint16_t ms) { _interval = ms; }
    void setWindow(uint16_t ms) { _window = ms; }
    void setMinInterval(uint16_t ms) { _minInterval = ms; }
    void setMaxInterval(uint16_t ms) { _maxInterval = ms; }
    void setDuplicateFilter(bool enabled) { _dupFilter = enabled; }
    void setScanResponseTimeout(uint32_t ms) { _respTimeout = ms; }
    void setMaxResults(int max) { _maxResults = max; }
    void setScanCallbacks(NimBLEScanCallbacks* cbs, bool clearOld) { (void)clearOld; _cbs = cbs; }
    // Phase 3: start/stop drive the onboard controller (ble_scan.cpp); the
    // adverts come back through cm0::blePump() into the callbacks below.
    bool start(uint32_t duration, bool continuous, bool firstScan);
    bool stop();
    bool isScanning() const { return _scanning; }
    NimBLEScanCallbacks* callbacks() const { return _cbs; }

private:
    bool _scanning = false;
    bool _active = true;
    bool _dupFilter = true;
    uint16_t _interval = 1125, _window = 1125, _minInterval = 0, _maxInterval = 0;
    uint32_t _respTimeout = 180;
    int _maxResults = 0;
    NimBLEScanCallbacks* _cbs = nullptr;
};
