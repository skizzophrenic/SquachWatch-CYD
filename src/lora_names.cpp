// SquachWatch-CYD — learned Meshtastic names. See include/lora_names.h.
#include "lora_names.h"
#include <string.h>

namespace Lora {
namespace Names {

namespace {

// Sized to the node table's own fields (lora_nodes.h: tag[12], name[24]), so
// a name read back is exactly what the row would have held.
struct Entry {
    uint32_t num;
    char     tag[12];
    char     name[24];
    uint32_t used;      // a counter, not a time: newest learned = biggest
};

Entry    s_e[CAP];
uint8_t  s_n = 0;
uint32_t s_clock = 0;
bool     s_dirty = false;

const uint8_t MAGIC = 0xA7, VERSION = 1;

void put(char* dst, size_t cap, const char* src) {
    if (!cap) return;
    size_t n = 0;
    while (src && n + 1 < cap && src[n]) { dst[n] = src[n]; n++; }
    dst[n] = '\0';
}

Entry* find(uint32_t num) {
    for (uint8_t i = 0; i < s_n; i++) if (s_e[i].num == num) return &s_e[i];
    return nullptr;
}

}  // namespace

bool learn(uint32_t num, const char* tag, const char* name) {
    const bool haveTag = tag && tag[0], haveName = name && name[0];
    if (!num || (!haveTag && !haveName)) return false;
    Entry* e = find(num);
    if (!e) {
        if (s_n < CAP) e = &s_e[s_n++];
        else {
            // Full: the one learned longest ago gives way.
            e = &s_e[0];
            for (uint8_t i = 1; i < CAP; i++) if (s_e[i].used < e->used) e = &s_e[i];
        }
        memset(e, 0, sizeof *e);
        e->num = num;
    }
    char t[sizeof e->tag], n[sizeof e->name];
    put(t, sizeof t, haveTag ? tag : e->tag);
    put(n, sizeof n, haveName ? name : e->name);
    const bool changed = strcmp(t, e->tag) != 0 || strcmp(n, e->name) != 0;
    memcpy(e->tag, t, sizeof t);
    memcpy(e->name, n, sizeof n);
    // Recency moves on every NodeInfo, changed or not, so a node that keeps
    // announcing itself is never the one evicted -- but only a change makes
    // the store worth writing to flash again.
    e->used = ++s_clock;
    if (changed) s_dirty = true;
    return changed;
}

bool lookup(uint32_t num, char* tag, size_t tagCap, char* name, size_t nameCap) {
    const Entry* e = find(num);
    if (!e) return false;
    if (tag)  put(tag, tagCap, e->tag);
    if (name) put(name, nameCap, e->name);
    return true;
}

uint8_t count() { return s_n; }
void clear() { s_n = 0; s_clock = 0; s_dirty = false; memset(s_e, 0, sizeof s_e); }
bool dirty() { return s_dirty; }
void markSaved() { s_dirty = false; }

// [MAGIC][VERSION][count] then count x (num LE32, tag[12], name[24]), oldest
// first so a load rebuilds the same recency order.
size_t bytesNeeded() { return 3 + (size_t)CAP * (4 + 12 + 24); }

size_t save(uint8_t* out, size_t cap) {
    const size_t need = 3 + (size_t)s_n * (4 + 12 + 24);
    if (!out || cap < need) return 0;
    uint8_t order[CAP];
    for (uint8_t i = 0; i < s_n; i++) order[i] = i;
    for (uint8_t i = 1; i < s_n; i++)                       // insertion sort by `used`
        for (uint8_t j = i; j > 0 && s_e[order[j - 1]].used > s_e[order[j]].used; j--) {
            const uint8_t t = order[j]; order[j] = order[j - 1]; order[j - 1] = t;
        }
    size_t o = 0;
    out[o++] = MAGIC; out[o++] = VERSION; out[o++] = s_n;
    for (uint8_t k = 0; k < s_n; k++) {
        const Entry& e = s_e[order[k]];
        for (int b = 0; b < 4; b++) out[o++] = (uint8_t)(e.num >> (8 * b));
        memcpy(out + o, e.tag, 12);  o += 12;
        memcpy(out + o, e.name, 24); o += 24;
    }
    return o;
}

bool load(const uint8_t* in, size_t len) {
    clear();
    if (!in || len < 3 || in[0] != MAGIC || in[1] != VERSION) return false;
    const uint8_t n = in[2];
    if (n > CAP || len < 3 + (size_t)n * (4 + 12 + 24)) return false;
    size_t o = 3;
    for (uint8_t k = 0; k < n; k++) {
        Entry& e = s_e[k];
        e.num = (uint32_t)in[o] | ((uint32_t)in[o + 1] << 8) | ((uint32_t)in[o + 2] << 16) | ((uint32_t)in[o + 3] << 24);
        o += 4;
        memcpy(e.tag, in + o, 12);  o += 12;
        memcpy(e.name, in + o, 24); o += 24;
        e.tag[11] = '\0'; e.name[23] = '\0';                // never trust a terminator from flash
        e.used = ++s_clock;
    }
    s_n = n;
    s_dirty = false;
    return true;
}

}
}
