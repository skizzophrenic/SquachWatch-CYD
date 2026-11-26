// SquachWatch-CYD — include/ad_fields.h against NimBLE's own field walk.
//
// The advert handler reads a device's name and manufacturer data in place
// instead of through NimBLE's std::string getters. The detections depend on
// those bytes (the company ID picks the signature, the name matches Flock,
// Flipper, Axon...), so the in-place reader must return exactly what NimBLE
// would have. The oracle below is NimBLE-Arduino 2.x's
// NimBLEAdvertisedDevice::findAdvField() and getPayloadByType(), carried over
// line for line for the field types used here, then both are run over fixed
// cases and a large set of random (often malformed) payloads.
#include "test_util.h"
#include "ad_fields.h"
#include <cstring>
#include <string>
#include <vector>
#include <cstdlib>

namespace {

const uint8_t COMP_NAME = 0x09, INCOMP_NAME = 0x08, MFG = 0xFF;

// NimBLEAdvertisedDevice::findAdvField, for the types this file uses.
uint8_t nimFind(const std::vector<uint8_t>& m, uint8_t type, uint8_t index, size_t* data_loc) {
    size_t length = m.size();
    size_t data = 0;
    uint8_t count = 0;
    while (length > 2) {
        const uint8_t flen = m[data];
        const uint8_t ftype = m[data + 1];
        if (flen >= length) return count;
        if (ftype == type || (type == COMP_NAME && ftype == INCOMP_NAME)) {
            if (type == COMP_NAME && data_loc != nullptr && ftype == INCOMP_NAME) {
                *data_loc = data;
                index++;
            }
            count++;
            if (data_loc != nullptr && count > index) break;
        }
        length -= 1 + flen;
        data += 1 + flen;
    }
    if (data_loc != nullptr && count > index) *data_loc = data;
    return count;
}

std::string nimPayload(const std::vector<uint8_t>& m, uint8_t type, uint8_t index) {
    // NimBLE leaves data_loc unset when it stops at a broken length after
    // counting fewer fields than `index` asks for, and then reads it anyway.
    // The firmware only asks for blocks under the count, so that case is
    // left out here rather than copied.
    size_t loc = (size_t)-1;
    if (nimFind(m, type, index, &loc) > 0 && loc < m.size()) {
        const uint8_t flen = m[loc];
        if (flen > 1) return std::string((const char*)&m[loc + 2], flen - 1);
    }
    return "";
}

std::string mine(size_t n, const uint8_t* d) { return std::string((const char*)d, n); }

std::vector<uint8_t> ad(std::initializer_list<std::vector<uint8_t>> fields) {
    std::vector<uint8_t> out;
    for (const auto& f : fields) out.insert(out.end(), f.begin(), f.end());
    return out;
}

}  // namespace

int main() {
    suite("Names, NimBLE's way");
    {
        auto p = ad({ {0x02, 0x01, 0x06}, {0x05, 0x09, 'A', 'B', 'C', 'D'} });
        const uint8_t* d; size_t n = AdFields::name(p.data(), p.size(), &d);
        ck("a complete name", mine(n, d) == "ABCD");
    }
    {
        auto p = ad({ {0x03, 0x08, 'S', '1'}, {0x04, 0x09, 'F', 'U', 'L'} });
        const uint8_t* d; size_t n = AdFields::name(p.data(), p.size(), &d);
        ck("complete wins over a short one before it", mine(n, d) == "FUL");
    }
    {
        auto p = ad({ {0x03, 0x08, 'S', '1'}, {0x03, 0x08, 'S', '2'} });
        const uint8_t* d; size_t n = AdFields::name(p.data(), p.size(), &d);
        ck("two short names: the last", mine(n, d) == "S2" && nimPayload(p, COMP_NAME, 0) == "S2");
    }
    {
        auto p = ad({ {0x02, 0x01, 0x06} });
        const uint8_t* d; size_t n = AdFields::name(p.data(), p.size(), &d);
        ck("no name", n == 0 && d != nullptr);
    }
    {
        char out[8];
        auto p = ad({ {0x0B, 0x09, 'F', 'l', 'i', 'p', 'p', 'e', 'r', ' ', 'Z', 'Z'} });
        AdFields::copyName(p.data(), p.size(), out, sizeof out);
        ck("copyName truncates to the buffer", strcmp(out, "Flipper") == 0);
        auto q = ad({ {0x05, 0x09, 'A', 0, 'B', 'C'} });
        AdFields::copyName(q.data(), q.size(), out, sizeof out);
        ck("and stops at a NUL, as strncpy did", strcmp(out, "A") == 0);
    }

    suite("Manufacturer data");
    {
        auto p = ad({ {0x02, 0x01, 0x06}, {0x05, 0xFF, 0x4C, 0x00, 0x12, 0x19}, {0x04, 0xFF, 0x75, 0x00, 0x01} });
        const uint8_t* d;
        size_t n0 = AdFields::find(p.data(), p.size(), MFG, 0, &d);
        ck("the first block", mine(n0, d) == nimPayload(p, MFG, 0) && n0 == 4);
        size_t n1 = AdFields::find(p.data(), p.size(), MFG, 1, &d);
        ck("the second block", mine(n1, d) == nimPayload(p, MFG, 1) && n1 == 3);
        ck("two counted", AdFields::count(p.data(), p.size(), MFG) == nimFind(p, MFG, 0, nullptr));
        size_t n2 = AdFields::find(p.data(), p.size(), MFG, 2, &d);
        ck("no third", n2 == 0 && nimPayload(p, MFG, 2).empty());
    }
    {
        auto p = ad({ {0x02, 0x01, 0x06}, {0x1E, 0xFF, 0x4C, 0x00} });   // length runs off the end
        const uint8_t* d;
        ck("a length past the end stops the walk", AdFields::find(p.data(), p.size(), MFG, 0, &d) == 0 &&
                                                    nimPayload(p, MFG, 0).empty());
    }

    suite("Random payloads, against NimBLE's walk");
    {
        srand(12345);
        int bad = 0;
        const int RUNS = 200000;
        for (int r = 0; r < RUNS; r++) {
            std::vector<uint8_t> p;
            const int n = rand() % 63;
            // Mostly well-formed fields with the interesting types, sometimes
            // plain noise, so malformed lengths and zero-length fields happen.
            if (rand() % 4 == 0) {
                for (int i = 0; i < n; i++) p.push_back((uint8_t)rand());
            } else {
                static const uint8_t TYPES[] = { 0x01, 0x08, 0x09, 0xFF, 0x16, 0x03 };
                while ((int)p.size() < n) {
                    const uint8_t len = (uint8_t)(rand() % 9);
                    p.push_back(rand() % 10 == 0 ? (uint8_t)(len + rand() % 40) : len);
                    p.push_back(TYPES[rand() % 6]);
                    for (int i = 1; i < len; i++) p.push_back((uint8_t)('a' + rand() % 26));
                }
            }
            const uint8_t* d;
            const size_t nn = AdFields::name(p.data(), p.size(), &d);
            if (mine(nn, d) != nimPayload(p, COMP_NAME, 0)) bad++;
            if (AdFields::count(p.data(), p.size(), MFG) != nimFind(p, MFG, 0, nullptr)) bad++;
            const uint8_t mc = nimFind(p, MFG, 0, nullptr);
            for (uint8_t i = 0; i < mc && i < 3; i++) {
                const size_t mn = AdFields::find(p.data(), p.size(), MFG, i, &d);
                if (mine(mn, d) != nimPayload(p, MFG, i)) bad++;
            }
        }
        ck("200,000 payloads agree on name, count and blocks", bad == 0);
    }
    return report();
}
