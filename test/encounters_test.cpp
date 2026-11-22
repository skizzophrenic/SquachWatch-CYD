// The random-encounter dice (encounters.h): the odds promised on the
// Random Encounters page, checked against the code that rolls them.
#include "encounters.h"
#include "state.h"
#include "test_util.h"
#include <cstdio>

using namespace Encounters;

// The mean wait in rolls, worked out exactly from the per-roll chances.
static double meanRolls(uint8_t level) {
    double surv = 1.0, mean = 0.0;
    for (uint16_t k = 0; k < 200 && surv > 1e-12; k++) {
        const double p = chancePermille(level, k) / 1000.0;
        mean += surv * p * (k + 1);
        surv *= (1.0 - p);
    }
    return mean;
}

int main() {
    suite("The chance per roll");
    ck("OFF never rolls", chancePermille(0, 0) == 0 && chancePermille(0, 500) == 0);
    ck("NORMAL starts at 5%", chancePermille(2, 0) == 50);
    ck("each miss adds 2%", chancePermille(2, 3) == 110);
    ck("NORMAL is sure after 24 misses (4 h)", chancePermille(2, 23) < 1000 && chancePermille(2, 24) == 1000);
    ck("RARE is sure after 48 (8 h)", chancePermille(1, 48) == 1000 && chancePermille(1, 47) < 1000);
    ck("OFTEN is sure after 12 (2 h)", chancePermille(3, 12) == 1000);

    suite("What that comes to");
    const double normal = meanRolls(2) * 10, rare = meanRolls(1) * 10, often = meanRolls(3) * 10;
    printf("    mean wait: RARE %.0f min, NORMAL %.0f min, OFTEN %.0f min\n", rare, normal, often);
    ck("NORMAL: about an hour and a quarter", normal > 65 && normal < 80);
    ck("RARE: about an hour and three quarters", rare > 95 && rare < 120);
    ck("OFTEN: under an hour", often > 40 && often < 55);

    suite("The d100");
    ck("1-60 common", tierFor(1, false) == Tier::COMMON && tierFor(60, false) == Tier::COMMON);
    ck("61-85 uncommon", tierFor(61, false) == Tier::UNCOMMON && tierFor(85, false) == Tier::UNCOMMON);
    ck("86-96 rare", tierFor(86, false) == Tier::RARE && tierFor(96, false) == Tier::RARE);
    ck("97-100 legendary", tierFor(97, false) == Tier::LEGENDARY && tierFor(100, false) == Tier::LEGENDARY);
    ck("quiet air: legendary from 96", tierFor(96, true) == Tier::LEGENDARY && tierFor(95, true) == Tier::RARE);

    suite("The pick");
    Context c{};
    c.visits = true;
    bool icons = false, onlyRare = true;
    for (uint32_t r = 0; r < 100; r++) if (pick(Tier::COMMON, c, r) == Kind::ICONS) icons = true;
    ck("no catch yet: no living icons", !icons);
    c.lastCatch = (uint8_t)DetectionType::AIRTAG;
    icons = false;
    for (uint32_t r = 0; r < 100; r++) if (pick(Tier::COMMON, c, r) == Kind::ICONS) icons = true;
    ck("a catch: living icons can come", icons);
    for (uint32_t r = 0; r < 100; r++) { const Kind k = pick(Tier::RARE, c, r); if (tierOf(k) != Tier::RARE) onlyRare = false; }
    ck("a rare roll only picks rare ones", onlyRare);
    Context nv{}; nv.visits = false;
    bool visit = false;
    for (uint32_t r = 0; r < 200; r++) { const Kind k = pick(Tier::COMMON, nv, r); if (k == Kind::DRIFTER || k == Kind::GOBLIN) visit = true; }
    ck("no visit scene: no drifter, no goblin", !visit);
    ck("nothing possible: NONE", pick(Tier::COMMON, nv, 5) == Kind::NONE);
    c.lastCatch = (uint8_t)DetectionType::EVILTWIN;
    uint32_t twins = 0; for (uint32_t r = 0; r < 500; r++) if (pick(Tier::RARE, c, r) == Kind::TWIN) twins++;
    ck("after an evil twin catch, the twin is likelier", twins > 300);
    ck("every kind has a name", name(Kind::ELDER)[0] != '?' && name(Kind::DRIFTER)[0] != '?');

    suite("The free roll's d20");
    int fMiss = 0, fCommon = 0, fUncommon = 0, fRare = 0, fLegend = 0, fShiny = 0;
    for (uint8_t d = 1; d <= 20; d++) {
        const Face f = faceOf(d);
        if (f.miss) fMiss++;
        else if (f.shiny) fShiny++;
        else if (f.tier == Tier::COMMON) fCommon++;
        else if (f.tier == Tier::UNCOMMON) fUncommon++;
        else if (f.tier == Tier::RARE) fRare++;
        else fLegend++;
    }
    ck("five faces come to nothing", fMiss == 5 && faceOf(1).miss && faceOf(5).miss && !faceOf(6).miss);
    ck("seven are COMMON, four UNCOMMON, two RARE", fCommon == 7 && fUncommon == 4 && fRare == 2);
    ck("19 is LEGENDARY", fLegend == 1 && faceOf(19).tier == Tier::LEGENDARY && !faceOf(19).shiny);
    ck("only a 20 is a shiny", fShiny == 1 && faceOf(20).shiny && !faceOf(20).miss);
    ck("a new one every twelve hours of running", FREE_EVERY_MIN == 720);
    ck("its die waits thirty seconds, then rolls off", FREE_STAY_MS == 30000);

    suite("The mimic");
    bool picked = false;
    for (int tr = 0; tr < 4; tr++)
        for (uint32_t r = 0; r < 400; r++) if (pick((Tier)tr, c, r) == Kind::MIMIC) picked = true;
    ck("the dice never pick it: only the free roll's die turns into one", !picked);
    ck("it is RARE, and it has a name", tierOf(Kind::MIMIC) == Tier::RARE && name(Kind::MIMIC)[0] != '?');
    ck("ten kinds in the journal", (int)Kind::COUNT == 10);
    return report();
}
