// SquachWatch-CYD — an advert's name and manufacturer data, read in place.
//
// NimBLE's getName() and getManufacturerData() each return a fresh
// std::string, which mallocs for anything over 15 bytes (an AirTag's block,
// an iBeacon, a squad frame, most names). On the Bluetooth host task, at a
// crowded room's ~200 adverts a second, that was hundreds of small malloc/free
// pairs a second on the task where a failed allocation aborts. These walk the
// payload (NimBLEAdvertisedDevice::getPayload(), advert and scan response
// together) and hand back a pointer into it instead.
//
// They follow NimBLEAdvertisedDevice::findAdvField() exactly, so what is read
// is what NimBLE would have returned:
//   - the walk stops at a field whose length byte reaches past the payload
//     (field length >= bytes left), and while fewer than three bytes remain;
//   - a field of length 0 still has its type read and is counted;
//   - the name is the first COMPLETE name (0x09); failing that, the LAST
//     shortened name (0x08);
//   - a field holding nothing (length byte 1) reads as empty.
// test/adfields_test.cpp holds them to those rules.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace AdFields {

// The data of the index-th field of this type (0-based), or 0 bytes. *out is
// always left pointing at something readable, never null.
inline size_t find(const uint8_t* p, size_t n, uint8_t type, uint8_t index, const uint8_t** out) {
    static const uint8_t EMPTY[1] = { 0 };
    *out = EMPTY;
    if (!p) return 0;
    size_t rem = n, pos = 0;
    uint8_t count = 0;
    while (rem > 2) {
        const uint8_t len = p[pos];
        if (len >= rem) return 0;
        if (p[pos + 1] == type) {
            if (count == index) {
                if (len > 1) { *out = p + pos + 2; return (size_t)len - 1; }
                return 0;
            }
            count++;
        }
        rem -= 1 + (size_t)len;
        pos += 1 + (size_t)len;
    }
    return 0;
}

inline uint8_t count(const uint8_t* p, size_t n, uint8_t type) {
    if (!p) return 0;
    size_t rem = n, pos = 0;
    uint8_t c = 0;
    while (rem > 2) {
        const uint8_t len = p[pos];
        if (len >= rem) break;
        if (p[pos + 1] == type) c++;
        rem -= 1 + (size_t)len;
        pos += 1 + (size_t)len;
    }
    return c;
}

// The device's name, NimBLE's way: complete first, else the last short one.
inline size_t name(const uint8_t* p, size_t n, const uint8_t** out) {
    static const uint8_t EMPTY[1] = { 0 };
    *out = EMPTY;
    if (!p) return 0;
    size_t rem = n, pos = 0;
    size_t shortAt = 0, shortLen = 0;
    bool haveShort = false;
    while (rem > 2) {
        const uint8_t len = p[pos];
        if (len >= rem) break;
        const uint8_t type = p[pos + 1];
        if (type == 0x09) {
            if (len > 1) { *out = p + pos + 2; return (size_t)len - 1; }
            return 0;
        }
        if (type == 0x08) { haveShort = true; shortAt = pos; shortLen = len; }
        rem -= 1 + (size_t)len;
        pos += 1 + (size_t)len;
    }
    if (haveShort && shortLen > 1) { *out = p + shortAt + 2; return shortLen - 1; }
    return 0;
}

// Copied into a C string the size of dst, stopping at a NUL inside the name
// the way strncpy() from the std::string's c_str() did.
inline void copyName(const uint8_t* p, size_t n, char* dst, size_t dstSize) {
    if (!dst || !dstSize) return;
    const uint8_t* d;
    size_t k = name(p, n, &d);
    if (k > dstSize - 1) k = dstSize - 1;
    size_t i = 0;
    for (; i < k && d[i]; i++) dst[i] = (char)d[i];
    dst[i] = 0;
}

}  // namespace AdFields
