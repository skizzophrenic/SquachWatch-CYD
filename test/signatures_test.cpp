// The signature tables themselves.
//
// These are hand-maintained lists of hardware addresses, and an audit
// against the IEEE registry found the table saying things that were not
// true: one prefix attributed to Vigilant belonged to Sonos, another to
// "Sierra" belonged to a company called SPECTRA - TEK, one labelled
// Hikvision belonged to Amazon, and a Verkada block had been added twice.
// None of that fails to compile and none of it fails at runtime -- it just
// makes the device confidently wrong.
//
// This cannot re-check the registry (CI has no business fetching 40,000
// rows on every push), so it checks the properties that hold regardless
// of what the registry says, plus a few anchors from the audit.
#include "signatures.h"
#include "test_util.h"
#include <cstring>

static DetectionType lookup(uint8_t a, uint8_t b, uint8_t c, Confidence* conf) {
    const uint8_t mac[6] = { a, b, c, 0x11, 0x22, 0x33 };
    return lookupOui(mac, conf);
}

int main() {
    suite("Every OUI row is well formed");

    int locallyAdministered = 0, gradedAboveLow = 0;
    for (uint16_t i = 0; i < kOuiCount; i++) {
        // Bit 1 of the first octet means locally administered: a randomised
        // or hand-assigned address, which by definition identifies no
        // vendor. Such a row may exist as a hint, but it can never be
        // strong evidence.
        if (kOuiTable[i].b[0] & 0x02) {
            locallyAdministered++;
            if (kOuiTable[i].conf != Confidence::LOW_CONF) gradedAboveLow++;
        }
    }
    char msg[96];
    snprintf(msg, sizeof(msg), "%d locally-administered rows, none graded above LOW",
             locallyAdministered);
    ck(msg, gradedAboveLow == 0);

    int dupes = 0;
    for (uint16_t i = 0; i < kOuiCount; i++)
        for (uint16_t j = (uint16_t)(i + 1); j < kOuiCount; j++)
            if (memcmp(kOuiTable[i].b, kOuiTable[j].b, 3) == 0) dupes++;
    // A duplicate is not merely untidy: lookupOui returns on the first hit,
    // so the second row is dead code that reads as a maintained signature.
    ck("no duplicate prefixes", dupes == 0);

    int emptyLabel = 0, unknownType = 0;
    for (uint16_t i = 0; i < kOuiCount; i++) {
        if (!kOuiTable[i].name || !kOuiTable[i].name[0]) emptyLabel++;
        if (kOuiTable[i].type == DetectionType::UNKNOWN) unknownType++;
        // vendor[12] in Detection, so 11 characters plus a terminator.
        if (kOuiTable[i].name && strlen(kOuiTable[i].name) > 11) emptyLabel++;
    }
    ck("every row has a label that fits Detection::vendor", emptyLabel == 0);
    ck("no row matches to UNKNOWN", unknownType == 0);

    suite("lookupOui hands back the row's own grade");

    Confidence conf = Confidence::LOW_CONF;

    // B4:1E:52 is registered to Flock Safety themselves -- the only prefix
    // in the whole FLOCK block that is.
    ck("Flock Safety's own block is FLOCK",
       lookup(0xB4, 0x1E, 0x52, &conf) == DetectionType::FLOCK);
    ck("...and graded HIGH", conf == Confidence::HIGH_CONF);

    // 24:0A:C4 is Espressif. Flock build on ESP32, so it is real evidence --
    // shared with every dev board, smart plug and hobby project on earth,
    // which is exactly what LOW is for.
    ck("a generic Espressif block still matches FLOCK",
       lookup(0x24, 0x0A, 0xC4, &conf) == DetectionType::FLOCK);
    ck("...but graded LOW", conf == Confidence::LOW_CONF);

    // Ring LLC's own registration versus Amazon's umbrella block, which
    // also covers Echo, Fire TV and Kindle.
    ck("Ring's own block is RING", lookup(0xAC, 0x9F, 0xC3, &conf) == DetectionType::RING);
    ck("...and graded HIGH", conf == Confidence::HIGH_CONF);
    ck("Amazon's umbrella block is RING", lookup(0xFC, 0x65, 0xDE, &conf) == DetectionType::RING);
    ck("...and graded MED", conf == Confidence::MED_CONF);

    suite("A miss leaves the caller's value alone");
    // The WiFi path seeds conf from the type before asking, so lookupOui
    // must not stamp on it when nothing matches.
    conf = Confidence::MED_CONF;
    ck("no match returns UNKNOWN", lookup(0xDE, 0xAD, 0xBE, &conf) == DetectionType::UNKNOWN);
    ck("...and does not touch conf", conf == Confidence::MED_CONF);
    ck("null mac is safe", lookupOui(nullptr, &conf) == DetectionType::UNKNOWN);
    ck("the one-argument form still compiles and works",
       lookupOui((const uint8_t[]){ 0xB4, 0x1E, 0x52, 0, 0, 0 }) == DetectionType::FLOCK);

    suite("Sonos is not a licence plate reader");
    // 00:0E:58 sat in the ALPR block as "Vigilant" for eleven releases. It
    // is registered to Sonos, Inc. and was removed; this is here so it
    // cannot come back by copy-paste from an older detector.
    ck("00:0E:58 no longer matches anything",
       lookup(0x00, 0x0E, 0x58, &conf) == DetectionType::UNKNOWN);

    return report();
}
