// SquachWatch-CYD — per-device alert suppression ("IGNORE")
#pragma once
#include <stdint.h>

// A short list of MACs whose detections should never raise the full-screen
// ALERT. The point is the devices you own: your own AirTag in your own
// pocket and your own doorbell camera are true positives every time, and a
// detector that shouts about them constantly is one you stop reading.
//
// Ignored devices are still scanned, still counted and still written to the
// LOG -- only the alert is suppressed. That way the list is recoverable
// (you can see the device and un-ignore it) and the counters stay honest.
//
// Storage is a single packed NVS blob of 6-byte MACs, not one key per
// device. NVS allocates in 32-byte entries, so a key each would cost ~6x
// the space and make enumeration a series of lookups; the whole list at
// MAX entries is 384 bytes, which is nothing against the 20KB partition.
namespace IgnoreList {

    static const uint8_t MAX = 64;

    // Loads from NVS. Safe to call more than once.
    void begin();

    bool contains(const uint8_t* mac);

    // Returns false if the list is full or the MAC is already on it.
    // Persists immediately -- an ignore that did not survive a reboot
    // would be worse than no ignore at all.
    bool add(const uint8_t* mac);

    bool remove(const uint8_t* mac);

    uint8_t count();

    // nullptr when idx is out of range. Points into the module's own
    // storage; valid until the next add/remove/clear.
    const uint8_t* macAt(uint8_t idx);

    void clear();
}
