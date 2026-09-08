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

    std::filesystem::remove_all(dir);
    return report();
}
