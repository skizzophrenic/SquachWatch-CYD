// SquachWatch-CM0 -- the NimBLEAddress stub.
//
// The firmware copies the address out through getBase()->val (the
// mesh's own-MAC nonce and the raw-scan bookkeeping), so the base
// struct is the classic ble_addr_t shape: a type byte and the six
// address bytes.
#pragma once
#include <cstdint>
#include <cstddef>

struct ble_addr_t {
    uint8_t type;
    uint8_t val[6];
};

class NimBLEAddress {
public:
    NimBLEAddress() {}
    explicit NimBLEAddress(const uint8_t* bytes) { if (bytes) for (int i = 0; i < 6; i++) _base.val[i] = bytes[i]; }

    ble_addr_t* getBase() { return &_base; }
    const ble_addr_t* getBase() const { return &_base; }

private:
    ble_addr_t _base = { 0, { 0, 0, 0, 0, 0, 0 } };
};
