// SquachWatch-CM0 -- the NimBLEUUID stub.
//
// The firmware compares service UUIDs through bitSize() and equals() and
// constructs candidates from 16-bit values and 128-bit string form
// (detection.cpp, the signature tables). The stub is exact: the value
// model is the same, only the source of the UUIDs (a real scanner) is
// what Phase 3 brings.
#pragma once
#include <cstdint>
#include <cstddef>
#include <string>

class NimBLEUUID {
public:
    NimBLEUUID() : _size(0) {}
    explicit NimBLEUUID(uint16_t u16) : _u16(u16), _size(2) {}
    explicit NimBLEUUID(const uint8_t* data, size_t len);
    explicit NimBLEUUID(const std::string& s);   // 4- or 32-hex-digit form, dashes allowed

    int  bitSize() const { return _size == 2 ? 16 : (_size == 16 ? 128 : 0); }
    bool equals(const NimBLEUUID& other) const;
    std::string toString() const;

private:
    void parseString(const std::string& s);
    uint16_t _u16 = 0;
    uint8_t  _u128[16] = { 0 };
    size_t   _size = 0;   // 2 bytes (16-bit), 16 bytes (128-bit), or 0 (empty)
};
