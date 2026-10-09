// SquachWatch-CYD — multi-target HUNT roster behavior.
#include "test_util.h"
#include "detection.h"
#include <Arduino.h>
#include <cstring>

static DetectionEngine eng;

static void macFor(uint8_t n, uint8_t out[6]) {
    out[0] = 0x02;
    out[1] = 0x44;
    out[2] = 0x55;
    out[3] = 0x66;
    out[4] = 0x77;
    out[5] = n;
}

int main() {
    SimClock::virtualTime = true;
    SimClock::nowMs = 100000;
    eng.init();

    suite("Bounded roster");
    ck("starts empty", eng.huntTargetCount() == 0);
    for (uint8_t i = 0; i < DetectionEngine::HUNT_TARGET_CAP; i++) {
        uint8_t mac[6]; macFor(i, mac);
        char label[16]; snprintf(label, sizeof label, "target-%u", (unsigned)i);
        ck("target adds", eng.toggleHuntBle(mac, label) == DetectionEngine::HuntToggle::ADDED);
    }
    ck("holds eight", eng.huntTargetCount() == DetectionEngine::HUNT_TARGET_CAP);
    {
        uint8_t ninth[6]; macFor(99, ninth);
        ck("ninth is refused", eng.toggleHuntBle(ninth, "ninth") == DetectionEngine::HuntToggle::FULL);
        ck("full roster stays intact", eng.huntTargetCount() == DetectionEngine::HUNT_TARGET_CAP);
    }

    suite("Toggle and refill");
    {
        uint8_t mac[6]; macFor(3, mac);
        ck("selected target is reported", eng.isHunted(mac, true));
        ck("toggling it removes it", eng.toggleHuntBle(mac, "target-3") == DetectionEngine::HuntToggle::REMOVED);
        ck("count drops", eng.huntTargetCount() == DetectionEngine::HUNT_TARGET_CAP - 1);
        ck("removed target is gone", !eng.isHunted(mac, true));

        uint8_t ninth[6]; macFor(99, ninth);
        ck("freed slot can refill", eng.toggleHuntWifi(ninth, "lab-ap") == DetectionEngine::HuntToggle::ADDED);
        ck("back at eight", eng.huntTargetCount() == DetectionEngine::HUNT_TARGET_CAP);
        ck("BLE/WiFi namespaces stay distinct", eng.isHunted(ninth, false) && !eng.isHunted(ninth, true));
    }

    suite("Active gauge is only a view of the roster");
    DetectionEngine::HuntTargetInfo first{};
    ck("first row exists", eng.huntTargetInfo(0, first));
    ck("row can become active", eng.activateHuntTarget(0));
    ck("active kind matches row", eng.huntKind() == first.kind);
    ck("active label matches row", strcmp(eng.huntLabel(), first.label) == 0);
    eng.deactivateHunt();
    ck("BACK can leave the gauge", eng.huntKind() == DetectionEngine::WatchKind::NONE);
    ck("BACK keeps all selected targets", eng.huntTargetCount() == DetectionEngine::HUNT_TARGET_CAP);

    suite("Each target keeps its own signal");
    DetectionEngine::HuntTargetInfo a{}, b{};
    ck("two rows available", eng.huntTargetInfo(0, a) && eng.huntTargetInfo(1, b));
    eng.checkHuntBle(a.mac, -72);
    eng.checkHuntBle(a.mac, -61);
    eng.checkHuntBle(b.mac, -88);
    DetectionEngine::HuntTargetInfo a2{}, b2{};
    eng.huntTargetInfo(0, a2);
    eng.huntTargetInfo(1, b2);
    ck("first target has two samples", a2.samples == 2);
    ck("first target keeps current and previous RSSI", a2.rssi == -61 && a2.previousRssi == -72);
    ck("second target keeps its own sample", b2.samples == 1 && b2.rssi == -88);

    suite("STOP removes only the active target");
    const uint8_t before = eng.huntTargetCount();
    ck("activate first", eng.activateHuntTarget(0));
    eng.removeActiveHunt();
    ck("one target removed", eng.huntTargetCount() == before - 1);
    ck("gauge deactivated", eng.huntKind() == DetectionEngine::WatchKind::NONE);

    suite("Clear means clear all");
    eng.clearHunt();
    ck("roster empty", eng.huntTargetCount() == 0);
    ck("nothing active", eng.huntKind() == DetectionEngine::WatchKind::NONE);

    return report();
}
