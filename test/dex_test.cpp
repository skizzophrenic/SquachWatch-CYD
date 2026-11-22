// SquachWatch-CYD — the SQUACHY-DEX's entries and record.
//
// What this guards: every detection type has a card, in type order, with
// every field filled and short enough to fit the screen; and the board's
// record does what the card says it does -- the first catch stays the
// first, the closest signal only ever gets closer, and the clock decides
// what counts as night. A missing entry would show the wrong card for
// every type after it, quietly.
#include "test_util.h"
#include "dex.h"
#include "clock.h"
#include <cstring>

namespace Clock {
bool     g_trusted = true;
uint32_t g_epoch   = 1789560000u;   // 2026-09-16 around noon UTC
bool     g_night   = false;
bool     trusted()  { return g_trusted; }
uint32_t nowEpoch() { return g_epoch; }
bool     night()    { return g_night; }
}

int main() {
    suite("Every type has a card, in order");
    ck("nineteen entries", Dex::ENTRIES == 19 && Dex::ENTRIES == CATALOGUE_N);
    ck("LISTENER is the last", Dex::typeAt(Dex::ENTRIES - 1) == DetectionType::LISTENER);
    // MY ALERTS is the owner's own list, not a kind of kit to collect.
    ck("and none for MY ALERTS", Dex::indexOf(DetectionType::MINE) == Dex::ENTRIES);
    bool order = true, filled = true, fits = true;
    for (uint8_t i = 0; i < Dex::ENTRIES; i++) {
        const DetectionType t = Dex::typeAt(i);
        if (Dex::indexOf(t) != i) order = false;
        const char* f[] = { Dex::shortName(t), Dex::kind(t), Dex::lore(t), Dex::habitat(t), Dex::quip(t), Dex::hint(t), Dex::radio(t) };
        for (const char* s : f) if (!s || !*s) filled = false;
        // Six lines of thirty-three characters in the landscape column, two
        // lines each for the rest.
        if (strlen(Dex::lore(t)) > 33 * 6 || strlen(Dex::habitat(t)) > 66 ||
            strlen(Dex::quip(t)) > 66 || strlen(Dex::hint(t)) > 66 || strlen(Dex::shortName(t)) > 7) fits = false;
    }
    ck("typeAt and indexOf agree", order);
    ck("no empty field", filled);
    ck("every text fits its box", fits);
    ck("UNKNOWN has no entry", Dex::indexOf(DetectionType::UNKNOWN) == Dex::ENTRIES);
    ck("stars follow rarity", Dex::stars(Dex::Rarity::COMMON) == 1 && Dex::stars(Dex::Rarity::RARE) == 3);

    suite("The record");
    Dex::begin();
    const DetectionType T = DetectionType::AIRTAG;
    ck("empty to start", Dex::record(T).firstEpoch == 0 && Dex::record(T).bestRssi == -128 && Dex::record(T).night == 0);
    Dex::note(T, -70);
    ck("first catch stamped", Dex::record(T).firstEpoch == Clock::g_epoch && Dex::record(T).lastEpoch == Clock::g_epoch);
    ck("closest is -70", Dex::record(T).bestRssi == -70);
    Clock::g_epoch += 3600;
    Dex::note(T, -85);
    ck("first stays, last moves", Dex::record(T).firstEpoch == Clock::g_epoch - 3600 && Dex::record(T).lastEpoch == Clock::g_epoch);
    ck("a weaker signal does not replace the closest", Dex::record(T).bestRssi == -70);
    Dex::note(T, -41);
    ck("a stronger one does", Dex::record(T).bestRssi == -41);
    Clock::g_night = true;
    Dex::note(T, -90);
    Dex::note(T, -90);
    ck("two at night", Dex::record(T).night == 2);
    ck("another type untouched", Dex::record(DetectionType::TILE).firstEpoch == 0);

    suite("No clock: signal only");
    Clock::g_trusted = false;
    Dex::note(DetectionType::TILE, -50);
    ck("no date without a clock", Dex::record(DetectionType::TILE).firstEpoch == 0);
    ck("but the signal is kept", Dex::record(DetectionType::TILE).bestRssi == -50);

    suite("Reset");
    Dex::reset();
    ck("everything back to empty", Dex::record(T).firstEpoch == 0 && Dex::record(T).night == 0 && Dex::record(T).bestRssi == -128);

    return report();
}
