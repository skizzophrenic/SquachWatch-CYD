// Shared Squachy rewards, including a fresh-process NVS reload. No radio or
// device is involved: the hardware's earned progress is never altered.
#include <cassert>
#include <cstdio>
#include <cstring>
#include <Arduino.h>
#include "squachy.h"
#include "settings.h"
#include "clock.h"

int main(int argc, char** argv) {
    const bool reload = argc == 2 && !strcmp(argv[1], "reload");
    SimClock::virtualTime = true;
    SimClock::nowMs = 1000;
    Settings::load(); Clock::begin();
    Squachy::trigger(Squachy::Event::BOOTED, DetectionType::UNKNOWN, reload ? 500 : 0);
    if (!reload) {
        assert(Squachy::unlockedOutfitCount() == 3);
        uint8_t index; uint32_t total;
        assert(Squachy::nextOutfit(index, total) && total == 5);
        assert(!strcmp(Squachy::outfitNameAt(index), "TINFOIL"));
        for (unsigned i = 0; i < 20; ++i) {
            Squachy::cycleOutfit(); assert(Squachy::outfitIndex() < 3);
        }
        const uint32_t counts[] = {4, 5, 14, 15, 24, 25, 99, 100, 149, 150, 499, 500};
        const uint8_t unlocked[] = {3, 4, 4, 5, 5, 6, 8, 9, 9, 10, 10, 10};
        for (unsigned i = 0; i < sizeof counts / sizeof counts[0]; ++i) {
            Squachy::trigger(Squachy::Event::DETECTION, DetectionType::AIRTAG, counts[i]);
            assert(Squachy::unlockedOutfitCount() == unlocked[i]);
            const uint32_t target = counts[i] < 25 ? 25 : counts[i] < 100 ? 100 : counts[i] < 500 ? 500 : 0;
            assert(Squachy::nextGrowthTotal() == target);
            const char* stage = counts[i] < 25 ? "FLEDGLING" : counts[i] < 100 ? "TRACKER" : counts[i] < 500 ? "VETERAN" : "LEGEND";
            assert(!strcmp(Squachy::growthStageName(), stage));
        }
        assert(!Squachy::nextOutfit(index, total)); // special event rewards stay locked
        for (unsigned i = 0; i < 25; ++i) {
            assert(Squachy::nextShadesPetCount() == (i < 10 ? 10u : 25u));
            Squachy::trigger(Squachy::Event::PETTED);
        }
        while (Squachy::outfitIndex()) Squachy::cycleOutfit();
        for (unsigned i = 0; i < 3; ++i) Squachy::cycleOutfit();
        Squachy::cycleShadesColor();
    }
    assert(Squachy::petCount() == 25);
    assert(Squachy::nextShadesPetCount() == 50);
    assert(!strcmp(Squachy::outfitName(), "TINFOIL"));
    assert(!strcmp(Squachy::shadesColorName(), "PINK"));
    assert(!strcmp(Squachy::growthStageName(), "LEGEND"));
    printf("Squachy progress %s: thresholds, locked rewards and persistence passed\n", reload ? "reload" : "seed");
}
