// SquachWatch-CYD PC emulator — minimal PNG writer.
// Writes "stored" (uncompressed) DEFLATE blocks rather than linking
// zlib -- one less thing that has to be present on whatever machine
// builds this. Files are larger than a real PNG encoder would produce
// (no LZ77), which is a total non-issue for a dev tool writing a
// handful of debug screenshots.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace PngWriter {

inline uint32_t crc32(const uint8_t* data, size_t len) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t n = 0; n < 256; n++) {
            uint32_t c = n;
            for (int k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[n] = c;
        }
        init = true;
    }
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) c = table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

inline uint32_t adler32(const uint8_t* data, size_t len) {
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < len; i++) { a = (a + data[i]) % 65521; b = (b + a) % 65521; }
    return (b << 16) | a;
}

inline void put32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back((uint8_t)(v >> 24)); out.push_back((uint8_t)(v >> 16));
    out.push_back((uint8_t)(v >> 8));  out.push_back((uint8_t)v);
}

inline void chunk(std::vector<uint8_t>& out, const char tag[4],
                  const uint8_t* data, size_t len) {
    put32(out, (uint32_t)len);
    size_t tagPos = out.size();
    out.insert(out.end(), tag, tag + 4);
    out.insert(out.end(), data, data + len);
    put32(out, crc32(&out[tagPos], len + 4));
}

// rgb888: width*height*3 bytes, row-major, no padding.
inline bool write(const char* path, int width, int height, const uint8_t* rgb888) {
    // Raw scanlines: one filter-type byte (0 = None) + width*3 pixel
    // bytes, per row.
    std::vector<uint8_t> raw;
    raw.reserve((size_t)(width * 3 + 1) * height);
    for (int y = 0; y < height; y++) {
        raw.push_back(0);
        raw.insert(raw.end(), rgb888 + (size_t)y * width * 3, rgb888 + (size_t)(y + 1) * width * 3);
    }

    // zlib stream: 2-byte header + one or more stored DEFLATE blocks
    // (each capped at 65535 bytes, the format's block-length limit) +
    // 4-byte Adler-32 trailer.
    std::vector<uint8_t> z;
    z.push_back(0x78); z.push_back(0x01);
    size_t off = 0;
    while (off < raw.size() || raw.empty()) {
        size_t n = raw.size() - off;
        if (n > 65535) n = 65535;
        bool last = (off + n >= raw.size());
        z.push_back(last ? 1 : 0);
        z.push_back((uint8_t)(n & 0xFF)); z.push_back((uint8_t)((n >> 8) & 0xFF));
        uint16_t nn = (uint16_t)~n;
        z.push_back((uint8_t)(nn & 0xFF)); z.push_back((uint8_t)((nn >> 8) & 0xFF));
        z.insert(z.end(), raw.begin() + off, raw.begin() + off + n);
        off += n;
        if (raw.empty()) break;
    }
    put32(z, adler32(raw.data(), raw.size()));

    std::vector<uint8_t> out;
    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    out.insert(out.end(), sig, sig + 8);

    uint8_t ihdr[13];
    ihdr[0] = (uint8_t)(width >> 24); ihdr[1] = (uint8_t)(width >> 16);
    ihdr[2] = (uint8_t)(width >> 8);  ihdr[3] = (uint8_t)width;
    ihdr[4] = (uint8_t)(height >> 24); ihdr[5] = (uint8_t)(height >> 16);
    ihdr[6] = (uint8_t)(height >> 8);  ihdr[7] = (uint8_t)height;
    ihdr[8] = 8;    // bit depth
    ihdr[9] = 2;    // color type 2 = truecolor (RGB)
    ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    chunk(out, "IHDR", ihdr, sizeof(ihdr));
    chunk(out, "IDAT", z.data(), z.size());
    chunk(out, "IEND", nullptr, 0);

    FILE* f = fopen(path, "wb");
    if (!f) return false;
    fwrite(out.data(), 1, out.size(), f);
    fclose(f);
    return true;
}

}  // namespace PngWriter
