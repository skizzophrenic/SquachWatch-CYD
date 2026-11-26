// The ignore list's NVS persistence.
//
// Reported bug: with exactly one device on the list, deleting it worked --
// the row disappeared, count() read zero -- and then the device was back
// after a reboot. The cause is that Preferences::putBytes returns early on
// a zero-length value without touching NVS, so writing the now-empty list
// was a silent no-op and the previous blob, still holding that one device,
// survived. With two or more devices the write is non-empty and everything
// behaves, which is why it went unnoticed.
//
// Nothing in the in-memory state can show this: count() is correct either
// way. The assertion has to be about what is on disk, so these tests reach
// past IgnoreList to the Preferences namespace underneath it and ask how
// many bytes the next boot would find.
#include "ignore_list.h"
#include "test_util.h"
#include <Arduino.h>
#include <Preferences.h>
#include <cstdlib>
#include <filesystem>

static const char* NS  = "ignore";
static const char* KEY = "dev";

// What begin() would read on the next boot.
static size_t bytesOnDisk() {
    Preferences p;
    p.begin(NS, false);
    return p.getBytesLength(KEY);
}

static const uint8_t A[6] = { 0xB4, 0x1E, 0x52, 0x01, 0x02, 0x03 };
static const uint8_t B[6] = { 0xAC, 0x9F, 0xC3, 0x0A, 0x0B, 0x0C };

int main() {
    // The shim only persists when it has somewhere to persist to.
    const char* dir = "ignore_test_nvs";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    setenv("SQUACHSIM_NVS", dir, 1);

    suite("Removing one of several leaves the rest on disk");

    ck("adding the first device works", IgnoreList::add(A, DetectionType::FLOCK));
    ck("adding the second works",       IgnoreList::add(B, DetectionType::RING));
    ck("count is 2", IgnoreList::count() == 2);
    ck("two records on disk", bytesOnDisk() == 14);

    ck("removing the first reports success", IgnoreList::remove(A));
    ck("count is 1", IgnoreList::count() == 1);
    ck("the removed device is gone", !IgnoreList::contains(A));
    ck("the other one is still there", IgnoreList::contains(B));
    // The survivor backfills index 0, so this also covers the swap-with-last.
    ck("one record on disk", bytesOnDisk() == 7);
    ck("and it is the right one", IgnoreList::typeAt(0) == DetectionType::RING);

    suite("Removing the last one empties the stored list too");

    ck("removing the last reports success", IgnoreList::remove(B));
    ck("count is 0", IgnoreList::count() == 0);
    ck("nothing in memory", !IgnoreList::contains(B));
    // The bug: this used to be 7. The list looked empty until the reboot
    // read the stale blob back and the device returned.
    ck("nothing on disk either -- it must not come back on reboot",
       bytesOnDisk() == 0);

    suite("clear() empties an already-populated list");

    ck("re-add", IgnoreList::add(A, DetectionType::FLOCK));
    ck("on disk", bytesOnDisk() == 7);
    IgnoreList::clear();
    ck("count is 0 after clear", IgnoreList::count() == 0);
    ck("nothing on disk after clear", bytesOnDisk() == 0);

    suite("An address ignored backwards (before v1.25.0) still matches");
    {
        const uint8_t fwd[6] = { 0x5E, 0xC5, 0xC1, 0xCF, 0x45, 0xC4 };
        const uint8_t old[6] = { 0xC4, 0x45, 0xCF, 0xC1, 0xC5, 0x5E };
        ck("ignored the old way round", IgnoreList::add(old, DetectionType::AIRTAG));
        ck("heard the new way round, still ignored", IgnoreList::contains(fwd));
        ck("not added twice either way", !IgnoreList::add(fwd, DetectionType::AIRTAG));
        ck("UN-IGNORE the new way round takes it off", IgnoreList::remove(fwd) && !IgnoreList::contains(old));
    }

    suite("Timed snoozes run out when they should");
    {
        SimClock::virtualTime = true;
        SimClock::nowMs = 1000;
        const uint8_t C[6] = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66 };
        const uint8_t D[6] = { 0x66, 0x55, 0x44, 0x33, 0x22, 0x10 };
        ck("10 min snooze taken", IgnoreList::snooze(C, IgnoreList::SnoozeDuration::MIN_10));
        ck("4 hour snooze taken", IgnoreList::snooze(D, IgnoreList::SnoozeDuration::HOUR_4));
        ck("both quiet now", IgnoreList::silenced(C) && IgnoreList::silenced(D));
        SimClock::nowMs += 11UL * 60 * 1000;
        ck("10 min one is back after 11 min", !IgnoreList::snoozed(C));
        ck("4 hour one still quiet after 11 min", IgnoreList::snoozed(D));
        SimClock::nowMs += 3UL * 60 * 60 * 1000;
        ck("4 hour one still quiet after 3 h 11 min", IgnoreList::snoozed(D));
        SimClock::nowMs += 50UL * 60 * 1000;
        ck("4 hour one back after 4 h 1 min", !IgnoreList::snoozed(D));
        ck("a timed snooze writes nothing to disk", []{
            Preferences p; p.begin(NS, false); return p.getBytesLength("snz") == 0; }());
        SimClock::virtualTime = false;
    }

    std::filesystem::remove_all(dir);
    return report();
}
