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

    // 24:0A:C4 is Espressif: every dev board, smart plug, hobby project --
    // and every SquachWatch. It used to match FLOCK at LOW; the projects that
    // test at real Flock sites stopped trusting Espressif blocks, and Flock's
    // newer cameras run a Qualcomm radio. Taken out 2026-10-14, so a
    // SquachWatch is never somebody else's Flock camera.
    ck("a generic Espressif block is no longer FLOCK",
       lookup(0x24, 0x0A, 0xC4, &conf) != DetectionType::FLOCK);
    ck("...nor is the one this board's own C3/S3 chips use",
       lookup(0x3C, 0x71, 0xBF, &conf) != DetectionType::FLOCK);

    // The new tables, spot-checked (2026-10-14).
    ck("ShotSpotter's own block is a gunshot sensor",
       lookup(0xD4, 0x11, 0xD6, &conf) == DetectionType::RAVEN);
    ck("WatchGuard VIDEO is a body cam", lookup(0x00, 0x1D, 0x96, &conf) == DetectionType::AXON);
    ck("...and WatchGuard Technologies, the firewall maker, is not",
       lookup(0x00, 0x01, 0x21, &conf) == DetectionType::UNKNOWN);
    ck("Skydio is a drone", lookup(0x38, 0x1D, 0x14, &conf) == DetectionType::DRONE);
    ck("the HC-05 maker's real block is a skimmer", lookup(0x98, 0xD3, 0x31, &conf) == DetectionType::SKIMMER);

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

    suite("An ESP32 is not a camera");
    // BC:DD:C2 sat in the camera block as "Arlo" and is registered to
    // Espressif. Every ESP32 in that block -- including this one -- read as
    // a covert camera, and no real Arlo matched, because none of Arlo's own
    // blocks were in the table. DETECTIONS.md says in as many words that
    // flagging Espressif OUIs as cameras is NOT built; this is the row that
    // was doing it anyway, under a brand name. Pinned so it cannot return.
    ck("Espressif's BC:DD:C2 is not a camera",
       lookup(0xBC, 0xDD, 0xC2, &conf) == DetectionType::UNKNOWN);
    ck("Arlo Technology's own FC:9C:98 is a camera",
       lookup(0xFC, 0x9C, 0x98, &conf) == DetectionType::CAMERA);
    ck("...and graded HIGH", conf == Confidence::HIGH_CONF);
    ck("Arlo's 48:62:64 is a camera", lookup(0x48, 0x62, 0x64, &conf) == DetectionType::CAMERA);
    ck("Arlo's A4:11:62 is a camera", lookup(0xA4, 0x11, 0x62, &conf) == DetectionType::CAMERA);

    // The general property, not just the one row: this board's own radio must
    // never match anything. A detector that reports itself is worse than one
    // that misses, because the reading looks exactly like a real find.
    {
        static const uint8_t espressif[][3] = {
            { 0x24, 0x0A, 0xC4 }, { 0x3C, 0x71, 0xBF }, { 0xBC, 0xDD, 0xC2 },
            { 0x30, 0xAE, 0xA4 }, { 0x7C, 0x9E, 0xBD }, { 0xA4, 0xCF, 0x12 },
            { 0xC4, 0x4F, 0x33 }, { 0xCC, 0x50, 0xE3 }, { 0xD8, 0xA0, 0x1D },
            { 0xE0, 0x98, 0x06 }, { 0xEC, 0xFA, 0xBC }, { 0xF4, 0xCF, 0xA2 },
        };
        int matched = 0;
        for (unsigned i = 0; i < sizeof espressif / sizeof espressif[0]; i++)
            if (lookup(espressif[i][0], espressif[i][1], espressif[i][2], &conf)
                != DetectionType::UNKNOWN) matched++;
        char m[80];
        snprintf(m, sizeof m, "no Espressif block matches any signature (%d did)", matched);
        ck(m, matched == 0);
    }

    suite("Flock-You's additions (issue #15)");
    ck("a Liteon block from the field list is FLOCK", lookup(0x74, 0x4C, 0xA1, &conf) == DetectionType::FLOCK);
    ck("...graded LOW", conf == Confidence::LOW_CONF);
    ck("Atheros's firmware-default block is FLOCK", lookup(0x00, 0x03, 0x7F, &conf) == DetectionType::FLOCK);
    ck("...graded LOW", conf == Confidence::LOW_CONF);
    ck("Samsung's 48:27:EA is left out", lookup(0x48, 0x27, 0xEA, &conf) == DetectionType::UNKNOWN);
    ck("a bare Flock network is FLOCK", lookupSsid("Flock") == DetectionType::FLOCK);
    ck("...and not every network that starts with it", lookupSsid("Flockhart Family") == DetectionType::UNKNOWN);
    ck("a ten-digit serial name is FLOCK", lookupBtName("4022019033") == DetectionType::FLOCK);
    ck("eleven digits is not", !isBareSerialName("40220190331"));
    ck("nine digits is not", !isBareSerialName("402201903"));
    ck("digits with a letter is not", !isBareSerialName("40220A9033"));

    suite("The 2026-10-14 signatures");
    ck("a Flock hotspot's network is FLOCK", lookupSsid("Flock Camera net.") == DetectionType::FLOCK);
    ck("FS- and a hex serial is FLOCK", lookupBtName("FS-BEC46A") == DetectionType::FLOCK);
    ck("...but not FS- and a word", lookupBtName("FS-Living Room") == DetectionType::UNKNOWN);
    ck("...nor FS- and two digits", !isFlockFsName("FS-12"));
    ck("a BodyWorn remote is a body cam", lookupBtName("BodyWorn Remote") == DetectionType::AXON);
    ck("Axon's company ID is AXON", lookupMfgId(0x034D) == DetectionType::AXON);
    ck("Axon's SIG UUIDs are AXON", lookupUuid(0xFE6B) == DetectionType::AXON && lookupUuid(0xFC81) == DetectionType::AXON);
    ck("Meta's 0x058E (the Quest's) is gone", lookupMfgId(0x058E) == DetectionType::UNKNOWN);
    {
        // AD 0x21 (128-bit service data), the tag as the field capture has it:
        // reversed, so it reads ...ECIVEDCWB in byte order.
        const uint8_t bwc[] = { 0x02, 0x01, 0x06, 0x12, 0x21,
                                'E','C','I','V','E','D','C','W','B','S','U','N','A','J','X','A', 0x00 };
        ck("Axon's BWCDEVICE tag, reversed, is found", isAxonBwcPayload(bwc, sizeof bwc));
        const uint8_t fwd[] = { 0x0A, 0x09, 'B','W','C','D','E','V','I','C','E' };
        ck("...and forwards", isAxonBwcPayload(fwd, sizeof fwd));
        const uint8_t no[] = { 0x08, 0x09, 'B','W','C','D','E','V','I' };
        ck("...but not a part of it", !isAxonBwcPayload(no, sizeof no));
    }
    {
        // Find Hub: flags, then service data AA FE 40 and an EID.
        const uint8_t fmdn[] = { 0x02, 0x01, 0x06, 0x06, 0x16, 0xAA, 0xFE, 0x40, 0x11, 0x22 };
        ck("a Find Hub tag (frame 0x40) is one", isFmdnPayload(fmdn, sizeof fmdn));
        const uint8_t utp[] = { 0x05, 0x16, 0xAA, 0xFE, 0x41, 0x11 };
        ck("...and in unwanted-tracking mode (0x41)", isFmdnPayload(utp, sizeof utp));
        const uint8_t url[] = { 0x02, 0x01, 0x06, 0x03, 0x03, 0xAA, 0xFE, 0x06, 0x16, 0xAA, 0xFE, 0x10, 0x00, 0x01 };
        ck("an Eddystone URL beacon on the same UUID is not", !isFmdnPayload(url, sizeof url));
    }
    {
        const uint8_t tile[] = { 0x02, 0x01, 0x06, 0x05, 0x16, 0xED, 0xFE, 0x02, 0x00 };
        ck("Tile's UUID as service data is found", serviceDataUuid(tile, sizeof tile) == 0xFEED);
        const uint8_t dult[] = { 0x04, 0x16, 0xB2, 0xFC, 0x01 };
        ck("the DULT tracker standard is a TRACKER", lookupUuid(serviceDataUuid(dult, sizeof dult)) == DetectionType::GOOGLE_TAG);
        const uint8_t other[] = { 0x04, 0x16, 0x34, 0x12, 0x01 };
        ck("an unknown service data UUID is nothing", serviceDataUuid(other, sizeof other) == 0);
        const uint8_t bad[] = { 0x09, 0x16, 0xED, 0xFE };   // a length that runs off the end
        ck("a malformed advert is read safely", serviceDataUuid(bad, sizeof bad) == 0);
    }

    return report();
}
