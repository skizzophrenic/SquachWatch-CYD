// SquachWatch-CYD — Privacy Mode screen lens (include/privacy.h).
//
// Types and makers stay whole; only a device/network's own name is cut to
// three characters. The LOG confirm panel used to feed vendorText through
// Privacy::name (#92: RING -> RIN***), so this pins the contract callers
// must respect: pass names into Privacy::name, never type/vendor tokens.
#include "test_util.h"
#include "privacy.h"
#include "settings.h"
#include "theme.h"
#include "clock.h"
#include <cstdlib>
#include <cstring>

namespace Theme { void applyPalette(uint8_t) {} }
namespace Clock {
uint8_t     zoneCount()        { return 1; }
uint8_t     zoneStep(uint8_t, int) { return 0; }
const char* zoneName(uint8_t)  { return "UTC"; }
void        applyZone(uint8_t) {}
}

int main() {
    setenv("SQUACHSIM_NVS", "out", 1);
    remove("out/settings.nvs");
    Settings::load();

    suite("Off: every string passes through");
    char buf[40];
    ck("name unchanged", Privacy::name("Gary's iPhone", buf, sizeof buf) == (const char*)"Gary's iPhone"
       || strcmp(Privacy::name("Gary's iPhone", buf, sizeof buf), "Gary's iPhone") == 0);
    // When off, name() returns `in` itself.
    const char* ring = "RING";
    ck("RING unchanged when off", Privacy::name(ring, buf, sizeof buf) == ring);

    suite("On: names masked, placeholders and empty spared");
    if (!Settings::privacyMode()) Settings::togglePrivacyMode();
    ck("privacy is on", Settings::privacyMode());

    const char* masked = Privacy::name("Gary's iPhone", buf, sizeof buf);
    ck("device name keeps three chars + stars", strcmp(masked, "Gar***") == 0);

    const char* unnamed = "Unnamed device";
    ck("Unnamed device spared", Privacy::name(unnamed, buf, sizeof buf) == unnamed);
    const char* hidden = "(hidden)";
    ck("(hidden) spared", Privacy::name(hidden, buf, sizeof buf) == hidden);

    suite("On: Privacy::name still truncates type tokens (callers must not pass them)");
    // Documents why confirmLabelOnScreen gates on s_confirmLabelIsName.
    const char* cut = Privacy::name("RING", buf, sizeof buf);
    ck("RING becomes RIN*** if wrongly passed in", strcmp(cut, "RIN***") == 0);
    cut = Privacy::name("APPLE", buf, sizeof buf);
    ck("APPLE becomes APP*** if wrongly passed in", strcmp(cut, "APP***") == 0);
    cut = Privacy::name("FLOCK-LITEO", buf, sizeof buf);
    ck("FLOCK-LITEO becomes FLO*** if wrongly passed in", strcmp(cut, "FLO***") == 0);

    return report();
}
