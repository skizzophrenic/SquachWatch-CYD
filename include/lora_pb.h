// SquachWatch-CYD — just enough protobuf to read Meshtastic.
//
// A field walker, not a code generator: the decoder asks for the next field
// and switches on its number. Nothing is allocated and nothing is trusted;
// a length that runs past the buffer ends the walk. Header-only so the host
// tests and the firmware share the same dozen lines.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace Pb {

enum Wire : uint8_t { VARINT = 0, FIXED64 = 1, BYTES = 2, FIXED32 = 5 };

struct Field {
    uint32_t       num;
    uint8_t        wire;
    uint64_t       varint;   // Wire::VARINT
    uint32_t       fixed32;  // Wire::FIXED32, little-endian
    uint64_t       fixed64;
    const uint8_t* bytes;    // Wire::BYTES
    size_t         len;
};

struct Reader {
    const uint8_t* p;
    const uint8_t* end;
    Reader(const uint8_t* data, size_t n) : p(data), end(data + n) {}

    bool varint(uint64_t& v) {
        v = 0;
        for (int shift = 0; shift < 64 && p < end; shift += 7) {
            const uint8_t b = *p++;
            v |= (uint64_t)(b & 0x7F) << shift;
            if (!(b & 0x80)) return true;
        }
        return false;
    }

    // The next field, or false at the end or on a malformed one.
    bool next(Field& f) {
        if (p >= end) return false;
        uint64_t tag;
        if (!varint(tag)) return false;
        f.num  = (uint32_t)(tag >> 3);
        f.wire = (uint8_t)(tag & 7);
        f.bytes = nullptr; f.len = 0; f.varint = 0; f.fixed32 = 0; f.fixed64 = 0;
        switch (f.wire) {
            case VARINT:  return varint(f.varint);
            case FIXED64:
                if (end - p < 8) return false;
                for (int i = 7; i >= 0; i--) f.fixed64 = (f.fixed64 << 8) | p[i];
                p += 8; return true;
            case FIXED32:
                if (end - p < 4) return false;
                f.fixed32 = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
                p += 4; return true;
            case BYTES: {
                uint64_t n;
                if (!varint(n) || n > (uint64_t)(end - p)) return false;
                f.bytes = p; f.len = (size_t)n; p += n; return true;
            }
            default: return false;   // groups and anything newer
        }
    }
};

// Copies a string field, NUL-terminated and cut to fit.
inline void str(const Field& f, char* out, size_t cap) {
    if (!cap) return;
    size_t n = f.len < cap - 1 ? f.len : cap - 1;
    memcpy(out, f.bytes, n);
    out[n] = '\0';
}

// A float carried as fixed32.
inline float f32(const Field& f) {
    float v; uint32_t u = f.fixed32; memcpy(&v, &u, 4); return v;
}

// A packed repeated field of fixed32s: how many, and each.
inline uint8_t packedFixed32(const Field& f, uint32_t* out, uint8_t cap) {
    uint8_t n = 0;
    for (size_t i = 0; i + 4 <= f.len && n < cap; i += 4)
        out[n++] = (uint32_t)f.bytes[i] | ((uint32_t)f.bytes[i+1] << 8) | ((uint32_t)f.bytes[i+2] << 16) | ((uint32_t)f.bytes[i+3] << 24);
    return n;
}
// A packed repeated field of varints (int32s), sign-extended.
inline uint8_t packedVarint(const Field& f, int32_t* out, uint8_t cap) {
    Reader r(f.bytes, f.len);
    uint8_t n = 0;
    uint64_t v;
    while (n < cap && r.p < r.end && r.varint(v)) out[n++] = (int32_t)v;
    return n;
}

}
