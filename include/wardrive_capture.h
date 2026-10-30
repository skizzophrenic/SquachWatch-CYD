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
#if defined(CYD_GPS)
    // Eligible location captured at callback time, never substituted at drain.
    // epoch == 0 means no eligible snapshot; those observations are not queued.
    uint32_t observedMs, epoch;
    int32_t lat7, lon7;
    int16_t altM;
    uint8_t accM;
#endif
};
// Drained on every pass of loop(), about 38 a second, so a CYD with its
// heap measured at 5.6 KB largest block keeps 8 of each; the watch keeps 32.
#if defined(CYD_GPS)
const uint8_t QN = 8;
#else
const uint8_t QN = 32;
#endif
struct Q {
    Pending item[QN];
    volatile uint8_t head = 0, tail = 0;
    bool push(const Pending& p) {
        const uint8_t h = __atomic_load_n(&head, __ATOMIC_RELAXED), nx = (uint8_t)((h + 1) % QN);
        if (nx == __atomic_load_n(&tail, __ATOMIC_ACQUIRE)) return false;
        item[h] = p;
        __atomic_store_n(&head, nx, __ATOMIC_RELEASE);
        return true;
    }
    bool pop(Pending& p) {
        const uint8_t t = __atomic_load_n(&tail, __ATOMIC_RELAXED);
        if (t == __atomic_load_n(&head, __ATOMIC_ACQUIRE)) return false;
        p = item[t];
        __atomic_store_n(&tail, (uint8_t)((t + 1) % QN), __ATOMIC_RELEASE);
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

// Repeat suppression is a bounded cache, not a limit on captured devices.
// The Elecrow profile leaves more RAM for scanning: its SD/GPS buffers share
// a small classic-ESP32 heap with the radios. A full cache replaces its oldest
// entry, so a smaller cache may write more repeat rows in a busy location.
struct SeenTable {
#if defined(CYD_GPS_ELECROW)
    static const uint8_t  BLOCKS = 1;
    static const uint16_t PER = 32;
#else
    static const uint8_t  BLOCKS = 2;
    static const uint16_t PER = 64;
#endif
    Seen* block[BLOCKS];
};
// True when this device is worth a row here and now, and then remembers it.
// wardrive_seen.cpp, CYD GPS builds only.
bool seenWorthWriting(SeenTable& t, uint8_t kind, const uint8_t* mac, int32_t lat7, int32_t lon7, uint32_t epoch);

}  // namespace Wardrive
