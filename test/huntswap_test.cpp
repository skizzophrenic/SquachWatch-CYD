// SquachWatch-CYD — a hunted phone that changed its Bluetooth ID
// (DetectionEngine::checkHuntSwap in include/detection.h): when the board
// offers the new address, and when it must not.
#include "test_util.h"
#include "detection.h"
#include <cstring>
#include <Arduino.h>

static const uint8_t IPHONE[]  = { 0x02, 0x01, 0x1A, 0x0A, 0xFF, 0x4C, 0x00, 0x10, 0x05, 0x03, 0x1C, 0x12, 0x34, 0x56 };
static const uint8_t ANDROID[] = { 0x05, 0x16, 0xF3, 0xFE, 0x01, 0x02 };
static const uint8_t T[6]  = { 0x10, 0, 0, 0, 0, 1 };   // the hunted phone
static const uint8_t N[6]  = { 0x20, 0, 0, 0, 0, 2 };   // its new address
static const uint8_t O[6]  = { 0x30, 0, 0, 0, 0, 3 };   // another iPhone, there all along

int main() {
    static DetectionEngine e;
    SimClock::virtualTime = true;          // millis() = SimClock::nowMs: a board that has been up a while
    SimClock::nowMs = 100000;
    const uint32_t t0 = SimClock::nowMs;

    suite("The swap is offered");
    e.huntBle(T, "(iPhone?)", RAW_KIND_IPHONE);
    e.checkHuntSwap(O, -60, IPHONE, sizeof IPHONE, t0 - 30000);  // the other phone, heard early
    e.checkHuntBle(T, -62);                                        // stamps the target (millis)
    const uint32_t seen = millis();
    e.checkHuntSwap(N, -63, IPHONE, sizeof IPHONE, seen + 2000);   // new address starts...
    ck("not while the target is still talking", !e.huntSwapOffered());
    e.checkHuntSwap(N, -63, IPHONE, sizeof IPHONE, seen + 11000);  // ...target quiet 11 s
    ck("offered once the target has been quiet 10 s", e.huntSwapOffered());
    ck("with the new address's reading", e.huntSwapRssi() == -63);
    e.huntSwapAccept(seen + 11000);
    ck("YES: the hunt follows the new address", e.isHunted(N, true) && !e.isHunted(T, true));
    ck("and the question is gone", !e.huntSwapOffered());

    suite("What is not offered");
    e.huntBle(T, "(iPhone?)", RAW_KIND_IPHONE);
    e.checkHuntSwap(O, -60, IPHONE, sizeof IPHONE, millis());       // there before the target went quiet
    SimClock::nowMs += 20000;
    e.checkHuntBle(T, -62);
    uint32_t s2 = millis();
    e.checkHuntSwap(O, -60, IPHONE, sizeof IPHONE, s2 + 20000);
    ck("a phone that was already about", !e.huntSwapOffered());
    e.checkHuntSwap(N, -63, ANDROID, sizeof ANDROID, s2 + 20000);
    ck("a different kind of phone", !e.huntSwapOffered());
    e.checkHuntSwap(N, -90, IPHONE, sizeof IPHONE, s2 + 20000);
    ck("much weaker than the target was", !e.huntSwapOffered());
    const uint8_t N2[6] = { 0x40, 0, 0, 0, 0, 4 };
    e.checkHuntSwap(N2, -60, IPHONE, sizeof IPHONE, s2 + 700000);
    ck("after ten minutes of quiet", !e.huntSwapOffered());

    suite("NO is remembered");
    const uint8_t N3[6] = { 0x50, 0, 0, 0, 0, 5 };
    e.huntBle(T, "(iPhone?)", RAW_KIND_IPHONE);
    e.checkHuntBle(T, -62);
    uint32_t s3 = millis();
    e.checkHuntSwap(N3, -61, IPHONE, sizeof IPHONE, s3 + 12000);
    ck("offered", e.huntSwapOffered());
    e.huntSwapDecline();
    e.checkHuntSwap(N3, -61, IPHONE, sizeof IPHONE, s3 + 14000);
    ck("not asked again after NO", !e.huntSwapOffered());
    ck("the hunt stays on the old address", e.isHunted(T, true));

    suite("Only phones the scan labelled");
    e.huntBle(T, "AirTag");                                         // from the LOG: no kind
    e.checkHuntBle(T, -62);
    uint32_t s4 = millis();
    e.checkHuntSwap(N, -62, IPHONE, sizeof IPHONE, s4 + 12000);
    ck("no guessing for a hunt without a kind", !e.huntSwapOffered());
    e.huntWifi(T, "net");
    e.checkHuntSwap(N, -62, IPHONE, sizeof IPHONE, s4 + 12000);
    ck("nor for a WiFi hunt", !e.huntSwapOffered());
    return report();
}
