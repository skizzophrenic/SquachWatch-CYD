// SquachWatch-CYD — one MORE INFO page per DEVICE, not per type.
//
// A DetectionType is a bucket: HACKER holds a Flipper Zero, a Pwnagotchi, a
// WiFi Pineapple and an ESP deauther, CAMERA holds eleven brands, FLOCK holds
// the one registered Flock block and a pile of chips Flock merely might use.
// "Wireless testing hardware" is true of all four HACKER devices and useful
// about none of them, so every device the firmware can name in its log gets
// its own page here, and the type's paragraph (detection_info.cpp) is only the
// fallback for anything this table does not know.
//
// A device is recognised by what the detector already wrote into the log
// entry: the vendor label, or -- for the matches that arrive on a Bluetooth
// NAME, like an HC-05 skimmer or a Flock battery -- the advertised name.
// Pure data and string matching, so the host test can prove every signature
// row reaches a page and every page can be summoned in the emulator.
#pragma once
#include "state.h"

namespace DeviceInfo {

struct Device {
    DetectionType type;
    const char*   title;     // the MORE INFO heading; 13 characters at most
    const char*   vendors;   // "A|B": vendor labels that mean this device, exact; or nullptr
    const char*   names;     // "A|B": advertised-name prefixes, any case; or nullptr
    const char*   text;      // fits the panel in portrait: seven lines of 34
};

extern const Device  kDevices[];
extern const uint8_t kDeviceCount;

// The page for this detection, or nullptr for one this table does not know --
// the caller then falls back to the type's own paragraph. A name rule is
// checked before a vendor rule, so a device that is only identifiable by its
// name wins over the generic label its type gets.
const Device* find(DetectionType t, const char* vendor, const char* name);

}
