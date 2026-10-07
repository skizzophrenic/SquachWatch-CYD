// SquachWatch-CYD: wardriving's capture pieces, shared by the watch's flash
// store (wardrive.cpp) and the CYD GPS builds' SD writer (wardrive_sd.cpp).
// Internal: nothing outside those files and their tests includes it.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>

namespace Wardrive {

// ---- the queue --------------------------------------------------------------
// Two single-producer rings, one per radio task, drained by loop(). Each
// producer only writes its own head and each consumer only its own tail.
struct Pending {
    uint8_t kind, flags, mac[6];
    int8_t  rssi;
    uint8_t channel;
    uint16_t auth;
    uint8_t nameLen;
    char    name[32];
};
const uint8_t QN = 32;
struct Q {
    Pending item[QN];
    volatile uint8_t head = 0, tail = 0;
    bool push(const Pending& p) {
        const uint8_t h = head, nx = (uint8_t)((h + 1) % QN);
        if (nx == tail) return false;
        item[h] = p;
        __sync_synchronize();
        head = nx;
        return true;
    }
    bool pop(Pending& p) {
        const uint8_t t = tail;
        if (t == head) return false;
        __sync_synchronize();
        p = item[t];
        tail = (uint8_t)((t + 1) % QN);
        return true;
    }
};

// ---- not the same thing twice from the same place ------------------------------
// A network heard ten times a second while you stand still is one row, not
// six hundred a minute. A device is written again once it is five minutes
// since its last row, or from 50 m away, whichever comes first -- enough for
// WiGLE to place it, and enough to follow one that moves.
struct Seen { uint8_t mac[6]; uint8_t kind; uint8_t used; int32_t lat7, lon7; uint32_t epoch; };
const uint32_t AGAIN_S = 300;
const float    AGAIN_M = 50.0f;

inline float metres(int32_t la1, int32_t lo1, int32_t la2, int32_t lo2) {
    const float k = 0.0111319f;                       // metres per 10^-7 degree of latitude
    const float c = cosf((float)la1 * 1e-7f * 0.0174533f);
    const float dy = (float)(la2 - la1) * k, dx = (float)(lo2 - lo1) * k * c;
    return sqrtf(dx * dx + dy * dy);
}

// The CYD GPS builds' table: 256 entries in four blocks of 64 (1,280 bytes
// each). On the user's CYD with a card mounted the largest free block was
// 10,740 bytes, so no single wardrive allocation goes over about 1.5 KB.
// When all 256 are in use, the entry written longest ago is replaced.
struct SeenTable {
    static const uint8_t  BLOCKS = 4;
    static const uint16_t PER = 64;
    Seen* block[BLOCKS];
};
// True when this device is worth a row here and now, and then remembers it.
// wardrive_seen.cpp, CYD GPS builds only.
bool seenWorthWriting(SeenTable& t, uint8_t kind, const uint8_t* mac, int32_t lat7, int32_t lon7, uint32_t epoch);

}  // namespace Wardrive
