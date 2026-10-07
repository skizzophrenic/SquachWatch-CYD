// SquachWatch-CYD — the CYD GPS builds' repeat table: which devices already
// have a recent row from about here. See wardrive_capture.h.
//
// 256 entries, searched in full, since a CYD hears far fewer devices a minute
// than it has entries. When all are in use, the entry written longest ago
// makes room. Every other board compiles this file empty.
#if defined(CYD_GPS)
#include "wardrive_capture.h"

namespace Wardrive {

bool seenWorthWriting(SeenTable& t, uint8_t kind, const uint8_t* mac, int32_t lat7, int32_t lon7, uint32_t epoch) {
    Seen* free = nullptr;
    Seen* oldest = nullptr;
    for (uint8_t b = 0; b < SeenTable::BLOCKS; b++) {
        Seen* blk = t.block[b];
        if (!blk) continue;
        for (uint16_t i = 0; i < SeenTable::PER; i++) {
            Seen& s = blk[i];
            if (!s.used) { if (!free) free = &s; continue; }
            if (s.kind == kind && !memcmp(s.mac, mac, 6)) {
                if (epoch - s.epoch < AGAIN_S && metres(s.lat7, s.lon7, lat7, lon7) < AGAIN_M) return false;
                s.lat7 = lat7; s.lon7 = lon7; s.epoch = epoch;
                return true;
            }
            if (!oldest || s.epoch < oldest->epoch) oldest = &s;
        }
    }
    Seen* s = free ? free : oldest;
    if (!s) return true;          // no table at all: nothing to compare against
    memcpy(s->mac, mac, 6); s->kind = kind; s->used = 1;
    s->lat7 = lat7; s->lon7 = lon7; s->epoch = epoch;
    return true;
}

}  // namespace Wardrive
#endif
