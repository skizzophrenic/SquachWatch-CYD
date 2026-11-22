// SquachWatch-CYD — the trail: what a device's sightings add up to.
//
// Two things the alert card draws that the engine's rows did not keep: the
// last minute of a device's signal, and the places it was heard at. Both are
// written here, by one function the board's engine and the emulator's
// stand-in both call on every sighting, so the two cannot drift. The verdict
// (FOLLOWING, MET BEFORE, NEW) is read off the same fields.
//
// Header-only on purpose: it is arithmetic over a Detection, and the engine
// is swapped for sim/detection_sim.cpp in the emulator.
#pragma once
#include <stdint.h>
#include "state.h"
#include "places.h"

namespace Trail {

// A sighting at `nowMs`: samples the signal every ~4 s and notes the place.
inline void noteSighting(Detection& d, int8_t rssi, uint32_t nowMs) {
    const uint8_t at = (uint8_t)(nowMs >> 12);
    if (d.sparkCount == 0 || d.sparkAt != at) {
        d.spark[d.sparkHead] = rssi;
        d.sparkHead = (uint8_t)((d.sparkHead + 1) % Detection::SPARK_N);
        if (d.sparkCount < Detection::SPARK_N) d.sparkCount++;
        d.sparkAt = at;
    }
    const uint8_t  place  = Places::current();
    const uint16_t nowMin = (uint16_t)(nowMs / 60000u);
    if (!place) return;
    if (d.stops && d.stopPlace[d.stops - 1] == place) {
        d.stopLastMin[d.stops - 1] = nowMin;
        return;
    }
    if (d.stops == Detection::STOPS_N) {
        for (uint8_t i = 1; i < Detection::STOPS_N; i++) {
            d.stopPlace[i - 1]    = d.stopPlace[i];
            d.stopFirstMin[i - 1] = d.stopFirstMin[i];
            d.stopLastMin[i - 1]  = d.stopLastMin[i];
        }
        d.stops--;
    }
    d.stopPlace[d.stops]    = place;
    d.stopFirstMin[d.stops] = nowMin;
    d.stopLastMin[d.stops]  = nowMin;
    d.stops++;
}

// A tracker's owner state from its advert (OWNER_*; 0 = the advert did not
// say): kept with the minute it first read that way, so the card can say how
// long it has been apart from its owner.
inline void noteOwner(Detection& d, uint8_t owner, uint32_t nowMs) {
    if (!owner || d.owner == owner) return;
    d.owner    = owner;
    d.ownerMin = (uint16_t)(nowMs / 60000u);
}

// The signal reading `i` back from now (0 = newest), or 0 when there is none.
inline int8_t sparkAt(const Detection& d, uint8_t i) {
    if (i >= d.sparkCount) return 0;
    return d.spark[(uint8_t)((d.sparkHead + Detection::SPARK_N - 1 - i) % Detection::SPARK_N)];
}

// Minutes the device has been with you: first stop to last.
inline uint16_t spanMin(const Detection& d) {
    if (!d.stops) return 0;
    return (uint16_t)(d.stopLastMin[d.stops - 1] - d.stopFirstMin[0]);
}

// Metres between its first and last stop, when both places have a position.
inline uint16_t spanM(const Detection& d) {
    if (d.stops < 2) return 0;
    return Places::distanceM(d.stopPlace[0], d.stopPlace[d.stops - 1]);
}

// FOLLOWING: heard at this many stops, over at least this long, and at every
// place you stopped at in between -- a device that was at two of your five
// stops is a coincidence; one at all three is not.
static const uint8_t  FOLLOW_STOPS = 3;
static const uint16_t FOLLOW_MIN   = 15;

inline bool atEveryStop(const Detection& d) {
    if (d.stops < 2) return false;
    return Places::countBetweenMin(d.stopFirstMin[0], d.stopLastMin[d.stops - 1]) <= d.stops;
}

inline bool following(const Detection& d) {
    return d.stops >= FOLLOW_STOPS && spanMin(d) >= FOLLOW_MIN && atEveryStop(d);
}

// Closer, further or holding, from the last minute's readings: the newest
// against the mean of the three before it, with a 3 dB dead band.
inline int8_t trend(const Detection& d) {
    if (d.sparkCount < 4) return 0;
    const int now = sparkAt(d, 0);
    const int before = (sparkAt(d, 1) + sparkAt(d, 2) + sparkAt(d, 3)) / 3;
    if (now >= before + 3) return 1;
    if (now <= before - 3) return -1;
    return 0;
}

}  // namespace Trail
