// SquachWatch-CYD — the raw BLE scan's phone labels (rawBleKindOf in
// include/detection.h): what an unnamed device's advert says it is.
#include "test_util.h"
#include "detection.h"
#include <cstring>

#define KIND(arr) rawBleKindOf(arr, sizeof arr)

int main() {
    suite("Apple Continuity");
    const uint8_t nearby[]  = { 0x02, 0x01, 0x1A, 0x0A, 0xFF, 0x4C, 0x00, 0x10, 0x05, 0x03, 0x1C, 0x12, 0x34, 0x56 };
    const uint8_t handoff[] = { 0x0F, 0xFF, 0x4C, 0x00, 0x0C, 0x0E, 0x00, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13 };
    const uint8_t airpods[] = { 0x0B, 0xFF, 0x4C, 0x00, 0x07, 0x06, 0x01, 0x0E, 0x20, 0x55, 0xAA, 0x01 };
    const uint8_t findmy[]  = { 0x05, 0xFF, 0x4C, 0x00, 0x12, 0x00 };
    // Find My first, then Nearby Info in the same advert: the phone wins.
    const uint8_t both[]    = { 0x0A, 0xFF, 0x4C, 0x00, 0x12, 0x00, 0x10, 0x03, 0x01, 0x02, 0x03 };
    ck("Nearby Info is an iPhone", KIND(nearby) == RAW_KIND_IPHONE);
    ck("Handoff is an iPhone", KIND(handoff) == RAW_KIND_IPHONE);
    ck("proximity pairing is AirPods", KIND(airpods) == RAW_KIND_AIRPODS);
    ck("Find My alone is just Apple", KIND(findmy) == RAW_KIND_APPLE);
    ck("the phone outranks Find My in one advert", KIND(both) == RAW_KIND_IPHONE);

    suite("Android and Samsung");
    const uint8_t uuidList[] = { 0x02, 0x01, 0x06, 0x05, 0x03, 0x0F, 0x18, 0xF3, 0xFE };
    const uint8_t svcData[]  = { 0x05, 0x16, 0xF3, 0xFE, 0x01, 0x02 };
    const uint8_t otherSvc[] = { 0x05, 0x16, 0x2C, 0xFE, 0xF3, 0xFE };   // Fast Pair, 0xFEF3 only in its data
    const uint8_t galaxy[]   = { 0x07, 0xFF, 0x75, 0x00, 0x42, 0x09, 0x01, 0x02 };
    ck("0xFEF3 in a UUID list is Android", KIND(uuidList) == RAW_KIND_ANDROID);
    ck("0xFEF3 service data is Android", KIND(svcData) == RAW_KIND_ANDROID);
    ck("service data's own UUID only, not its payload", KIND(otherSvc) == RAW_KIND_NONE);
    ck("Samsung's company ID", KIND(galaxy) == RAW_KIND_SAMSUNG);

    suite("Nothing to go on, and broken adverts");
    const uint8_t flags[]    = { 0x02, 0x01, 0x06 };
    const uint8_t cut[]      = { 0x0A, 0xFF, 0x4C, 0x00, 0x10 };          // says 10 bytes, has 4
    const uint8_t badTlv[]   = { 0x06, 0xFF, 0x4C, 0x00, 0x05, 0x7F, 0x10 }; // a TLV running off the end
    const uint8_t zeroLen[]  = { 0x00, 0xFF, 0x4C, 0x00, 0x10 };
    ck("flags only: nothing", KIND(flags) == RAW_KIND_NONE);
    ck("a field longer than the advert stops the walk", KIND(cut) == RAW_KIND_NONE);
    ck("a Continuity TLV off the end is not read past", KIND(badTlv) == RAW_KIND_APPLE);
    ck("a zero length stops the walk", KIND(zeroLen) == RAW_KIND_NONE);
    ck("empty", rawBleKindOf(nullptr, 0) == RAW_KIND_NONE);

    suite("Labels");
    ck("iPhone label", strcmp(rawBleKindLabel(RAW_KIND_IPHONE), "(iPhone?)") == 0);
    ck("none is (unnamed)", strcmp(rawBleKindLabel(RAW_KIND_NONE), "(unnamed)") == 0);
    ck("every label is bracketed, so Privacy Mode leaves it be", rawBleKindLabel(RAW_KIND_ANDROID)[0] == '('
       && rawBleKindLabel(RAW_KIND_SAMSUNG)[0] == '(' && rawBleKindLabel(RAW_KIND_AIRPODS)[0] == '('
       && rawBleKindLabel(RAW_KIND_APPLE)[0] == '(');
    return report();
}
