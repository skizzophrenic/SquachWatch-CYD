// The HACKER signatures.
//
// Two of these constants are ones the wider ecosystem gets wrong, which is
// the reason this file exists rather than trusting the table:
//
//   * ESP32 Marauder's source comments Flipper's BLE company ID as 0x0FBA.
//     0x0FBA belongs to Cosonic Intelligent Technologies, who make
//     headsets. Flipper Devices is 0x0E29 in the Bluetooth SIG list.
//   * Wall-of-Flippers checks MAC prefixes 80:E1:26 and 80:E1:27. Neither
//     appears anywhere in the IEEE registry. Only 0C:FA:22 is registered
//     to Flipper Devices Inc.
//
// Both are the same failure the Sonos entry was: a plausible constant
// copied between detectors until it looks like a fact. Pinned here so a
// future "fix" that imports them from another project turns this red.
#include "signatures.h"
#include "test_util.h"
#include <cstring>

static DetectionType oui(uint8_t a, uint8_t b, uint8_t c, Confidence* conf) {
    const uint8_t mac[6] = { a, b, c, 0x11, 0x22, 0x33 };
    return lookupOui(mac, conf);
}

int main() {
    Confidence conf = Confidence::LOW_CONF;

    suite("Flipper Zero");

    ck("its own IEEE block is HACKER", oui(0x0C, 0xFA, 0x22, &conf) == DetectionType::HACKER);
    ck("...and graded HIGH", conf == Confidence::HIGH_CONF);

    // Not merely absent: these must never be added, because they are not
    // Flipper's and nothing in the registry says they are anybody's.
    ck("80:E1:26 matches nothing", oui(0x80, 0xE1, 0x26, &conf) == DetectionType::UNKNOWN);
    ck("80:E1:27 matches nothing", oui(0x80, 0xE1, 0x27, &conf) == DetectionType::UNKNOWN);

    ck("SIG company ID 0x0E29 is HACKER", lookupMfgId(0x0E29) == DetectionType::HACKER);
    ck("0x0FBA (Cosonic) is not", lookupMfgId(0x0FBA) == DetectionType::UNKNOWN);

    // Three UUIDs, one per case colour. All three have to be present: ship
    // two of them and one colour of Flipper is invisible.
    ck("service UUID 0x3081 is HACKER", lookupUuid(0x3081) == DetectionType::HACKER);
    ck("service UUID 0x3082 is HACKER", lookupUuid(0x3082) == DetectionType::HACKER);
    ck("service UUID 0x3083 is HACKER", lookupUuid(0x3083) == DetectionType::HACKER);
    // 0x3080 and 0x3084 are not Flipper's. Widening the check to a range
    // (the tempting simplification) would swallow both.
    ck("0x3080 is not", lookupUuid(0x3080) == DetectionType::UNKNOWN);
    ck("0x3084 is not", lookupUuid(0x3084) == DetectionType::UNKNOWN);

    // Raven sits at 0x3100..0x3500. The two blocks are close enough that a
    // careless range check would collide, and a gunshot detector reported
    // as pentest hardware is not a small mistake.
    ck("Raven's 0x3100 is still RAVEN", lookupUuid(0x3100) == DetectionType::RAVEN);

    ck("an advertised name beginning Flipper matches",
       lookupBtName("Flipper Ozzyx") == DetectionType::HACKER);

    suite("Hak5, which has no IEEE registration at all");

    // Both of Hak5's habitual addresses are locally administered: bit 1 of
    // the first octet is set, so by construction they identify no vendor
    // and anyone can set them. They are a hint, not evidence.
    ck("02:C0:CA is HACKER", oui(0x02, 0xC0, 0xCA, &conf) == DetectionType::HACKER);
    ck("...and graded LOW", conf == Confidence::LOW_CONF);
    ck("02:13:37 is HACKER", oui(0x02, 0x13, 0x37, &conf) == DetectionType::HACKER);
    ck("...and graded LOW", conf == Confidence::LOW_CONF);

    suite("SSID prefixes");

    ck("Pineapple_1337 is HACKER", lookupSsid("Pineapple_1337") == DetectionType::HACKER);
    ck("case-insensitive, as the table promises",
       lookupSsid("PINEAPPLE_ABCD") == DetectionType::HACKER);
    ck("pwned is HACKER", lookupSsid("pwned") == DetectionType::HACKER);
    ck("so is a fork's pwned-1234", lookupSsid("pwned-1234") == DetectionType::HACKER);
    // Prefix matching, so this is the boundary worth stating: a network
    // merely containing the word is not a match.
    ck("but not a network that merely contains it",
       lookupSsid("MyNetwork-pwned") == DetectionType::UNKNOWN);
    ck("vendor label fits Detection::vendor",
       strlen(ssidVendorName("Pineapple_1337")) <= 11);

    suite("The type's own grade is the conservative one");

    // HACKER deliberately bundles exact signatures with guessable strings.
    // The rule at the top of confidenceFor is that such a type reports the
    // LOWER grade; the exact paths raise it at their own match sites.
    ck("confidenceFor(HACKER) is MED, not HIGH",
       confidenceFor(DetectionType::HACKER) == Confidence::MED_CONF);

    return report();
}
