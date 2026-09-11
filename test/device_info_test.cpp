// SquachWatch-CYD — every device has its own MORE INFO page, every page can
// be summoned in the emulator, and every page fits the panel.
//
// The failure this exists for already happened once: the web emulator's
// detection list was typed out by hand, stopped at EVILTWIN, and so nobody
// could look at a Flipper Zero's alert without a Flipper Zero. A table that
// has to agree with three others is only kept in step by a test.
#include "test_util.h"
#include "device_info.h"
#include "signatures.h"
#include "sim_detections.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

using DeviceInfo::Device;

static bool hasPages(DetectionType t) {
    for (uint8_t i = 0; i < DeviceInfo::kDeviceCount; i++)
        if (DeviceInfo::kDevices[i].type == t) return true;
    return false;
}

// A signature row of a type with device pages must land on one.
static bool reaches(DetectionType t, const char* vendor, const char* name, const char* what) {
    if (!hasPages(t)) return true;
    if (DeviceInfo::find(t, vendor, name)) return true;
    printf("    no page for %s: %s vendor=\"%s\" name=\"%s\"\n",
           what, detectionTypeName(t), vendor, name);
    return false;
}

// What the BLE path writes as the vendor for a service-UUID or company-ID
// match -- the rule in detection.cpp's vendor block, restated.
static const char* bleVendor(DetectionType t, const char* rowLabel) {
    switch (t) {
        case DetectionType::META:    return rowLabel;
        case DetectionType::SKIMMER: return rowLabel;
        case DetectionType::FLOCK:   return "Flock-BLE";
        case DetectionType::HACKER:  return "Flipper";
        default:                     return rowLabel;
    }
}

// Greedy word wrap at `cols` characters: how many lines the panel needs.
static int wrapLines(const char* s, int cols) {
    int lines = 0, col = 0;
    while (*s) {
        while (*s == ' ') s++;
        const char* w = s;
        while (*s && *s != ' ') s++;
        const int n = (int)(s - w);
        if (!n) break;
        if (col == 0)                 { col = n; lines++; }
        else if (col + 1 + n <= cols) { col += 1 + n; }
        else                          { col = n; lines++; }
    }
    return lines;
}

// Bangers draws A-Z, 0-9, space, '!', and the hand-added hyphen and
// apostrophe. Anything else is skipped without a gap.
static bool bangersOk(const char* s) {
    for (; *s; s++)
        if (!(isupper((unsigned char)*s) || isdigit((unsigned char)*s) ||
              *s == ' ' || *s == '!' || *s == '-' || *s == '\'')) return false;
    return true;
}

int main() {
    suite("Every signature a multi-device type can log has its own page");
    {
        bool ok = true;
        for (uint16_t i = 0; i < kOuiCount; i++)
            ok &= reaches(kOuiTable[i].type, kOuiTable[i].name, "", "OUI row");
        for (uint16_t i = 0; i < kSsidCount; i++)
            ok &= reaches(kSsidPrefixes[i].type, kSsidPrefixes[i].name, "", "SSID row");
        for (uint16_t i = 0; i < kUuidCount; i++)
            ok &= reaches(kUuidTable[i].type, bleVendor(kUuidTable[i].type, kUuidTable[i].name),
                          "", "UUID row");
        for (uint16_t i = 0; i < kMfgIdCount; i++)
            ok &= reaches(kMfgIdTable[i].type, bleVendor(kMfgIdTable[i].type, kMfgIdTable[i].name),
                          "", "company-ID row");
        for (uint16_t i = 0; i < kBtClassicCount; i++)
            ok &= reaches(kBtClassicNames[i].type, bleVendor(kBtClassicNames[i].type, "BLE"),
                          kBtClassicNames[i].name, "Bluetooth name");
        ck("every row reaches a page", ok);
        ck("a pwnagotchi, labelled in processWiFiQ(), has its page",
           DeviceInfo::find(DetectionType::HACKER, "Pwnagotchi", "rikki") != nullptr);
        const Device* f = DeviceInfo::find(DetectionType::HACKER, "Flipper", "Flipper Ozzyx");
        ck("a Flipper Zero gets the Flipper Zero page", f && !strcmp(f->title, "FLIPPER ZERO"));
        ck("a type without device pages falls back to its own paragraph",
           DeviceInfo::find(DetectionType::AIRTAG, "Apple", "AirTag") == nullptr);
        const Device* b = DeviceInfo::find(DetectionType::FLOCK, "Flock-BLE", "FS Ext Battery");
        ck("a name wins over the generic label its type gets",
           b && !strcmp(b->title, "FLOCK POWER"));
        ck("a device name matches whatever its case",
           DeviceInfo::find(DetectionType::SKIMMER, "BLE", "hc-05") != nullptr);
    }

    suite("Every page can be summoned in the emulator");
    {
        bool ok = true;
        for (uint8_t i = 0; i < DeviceInfo::kDeviceCount; i++) {
            const Device& dv = DeviceInfo::kDevices[i];
            bool found = false;
            for (size_t p = 0; p < kSimProfileCount && !found; p++)
                found = DeviceInfo::find(kSimProfiles[p].type, kSimProfiles[p].vendor,
                                         kSimProfiles[p].name) == &dv;
            if (!found) { printf("    no emulator profile for %s\n", dv.title); ok = false; }
        }
        ck("every device page has a profile that reaches it", ok);
        bool types = true;
        for (uint8_t t = 1; t < (uint8_t)DetectionType::COUNT; t++)
            if (!simProfileFor((DetectionType)t)) {
                printf("    no emulator profile for %s\n", detectionTypeName((DetectionType)t));
                types = false;
            }
        ck("and every type has at least one", types);
        bool fits = true;
        for (size_t p = 0; p < kSimProfileCount; p++)
            if (strlen(kSimProfiles[p].vendor) >= sizeof(Detection::vendor) ||
                strlen(kSimProfiles[p].name) >= sizeof(Detection::name)) {
                printf("    profile %s overflows a Detection field\n", kSimProfiles[p].label);
                fits = false;
            }
        ck("every profile fits the Detection it builds", fits);
    }

    suite("Every page fits the MORE INFO panel");
    {
        bool text = true, title = true;
        for (uint8_t i = 0; i < DeviceInfo::kDeviceCount; i++) {
            const Device& dv = DeviceInfo::kDevices[i];
            // Portrait: a 208 px text column is 34 characters, seven lines.
            const int n = wrapLines(dv.text, 34);
            if (n > 7) { printf("    %s runs to %d lines in portrait\n", dv.title, n); text = false; }
            if (strlen(dv.title) > 13 || !bangersOk(dv.title)) {
                printf("    title \"%s\" is too long or has a character Bangers skips\n", dv.title);
                title = false;
            }
        }
        ck("seven lines of 34 or fewer", text);
        ck("titles of 13 characters Bangers can draw", title);
    }

    return report();
}
