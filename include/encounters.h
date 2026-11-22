// SquachWatch-CYD -- random encounters on the main screen.
//
// Everybody gets them: the dice are on the board, not in the air. While the
// main screen is up (no alert, no visitor, no POCKET or TERMINAL MODE, screen
// on) the board rolls every ten minutes. A roll starts at 5% and every miss
// adds 2%, and after 24 misses -- four hours of screen time -- the next roll
// always hits: on average one encounter every hour and a quarter. RARE halves
// the odds, OFTEN doubles them, OFF stops the dice.
//
// A hit rolls a d100 for the rarity (COMMON 1-60, UNCOMMON 61-85, RARE 86-96,
// LEGENDARY 97-100; in quiet air LEGENDARY starts at 96), then a weighted pick
// inside it that the board's own day tilts: the last catch, how busy the air
// is, the night, the pet. One in 64 is a shiny. Every one is written to the
// FIELD JOURNAL (counts, the first date, shinies), kept in NVS like the DEX.
//
// And one roll anybody can make: the FREE ROLL. One is waiting after the
// update, and another turns up after every twelve hours the board has been
// running (counted across restarts, so pulling the plug earns nothing). Its
// die sits on the main screen's floor for thirty seconds of that screen's time
// (FREE_STAY_MS); tapped, it is an honest d20 -- faceOf() -- that the journal
// counts like any other roll, and left alone it rolls off and that one is gone.
//
// This file is the dice and the journal and nothing that draws: the host tests
// in test/encounters_test.cpp check the odds. The encounters themselves are
// played by ui_clear.cpp.
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace Encounters {

enum class Kind : uint8_t {
    DRIFTER = 0,   // a stranger Squachy visits                    COMMON
    GOBLIN,        // a wandering goblin visits                     COMMON
    ICONS,         // the last catch's icon grows legs              COMMON
    ZOOMIES,       // the pet runs laps                             COMMON
    KAIJU,         // a giant Mothman over the horizon              UNCOMMON
    LOST,          // a lost WiFi creature: tap it home             UNCOMMON
    TWIN,          // Squachy's evil twin visits, then glitches     RARE
    ABDUCT,        // a UFO beams up the pet, and gives it back     RARE
    ELDER,         // the Elder rises over the ridge                LEGENDARY
    MIMIC,         // the free roll's die was never a die           RARE (the free roll only)
    COUNT,
    NONE = 255
};
enum class Tier : uint8_t { COMMON, UNCOMMON, RARE, LEGENDARY };

// What the board knows that tilts the pick.
struct Context {
    uint8_t lastCatch;   // DetectionType of the last catch, 0 for none yet
    bool    city;        // busy air: lots of networks and adverts
    bool    woods;       // quiet air
    bool    night;
    bool    pet;         // a pet is out
    bool    visits;      // the board has the visit scene (SQUACH_MESH)
};

// ---- the dice (pure) --------------------------------------------------------
// level: 0 OFF, 1 RARE, 2 NORMAL, 3 OFTEN.
// The chance a roll hits after `misses` empty rolls, in thousandths.
uint16_t chancePermille(uint8_t level, uint16_t misses);
Tier     tierFor(uint8_t d100, bool woods);           // d100 is 1..100
Tier     tierOf(Kind k);
// A weighted pick inside the tier from a random number; NONE if nothing in
// that tier can happen here (a RARE roll on a board with no visits and no
// UFO weights, say), and the caller rolls again.
Kind     pick(Tier t, const Context& c, uint32_t r);
const char* name(Kind k);       // "THE DRIFTER", for the journal
const char* tierName(Tier t);
// What a face of the free roll's d20 comes to: 1-5 nothing, 6-12 COMMON,
// 13-16 UNCOMMON, 17-18 RARE, 19 LEGENDARY, 20 a shiny (its tier is rolled as
// any hit's is). The same faces the die on screen shows for every roll.
struct Face { bool miss; bool shiny; Tier tier; };
Face     faceOf(uint8_t d20);
// Minutes of running between free rolls.
const uint16_t FREE_EVERY_MIN = 12 * 60;
// How long its die waits on the main screen before it rolls off, unrolled.
const uint32_t FREE_STAY_MS = 30UL * 1000UL;

// ---- the run (encounters.cpp) -----------------------------------------------
void begin();
// From the loop: `eligible` is true while the main screen could take an
// encounter. Counts that time; every ten minutes of it, a roll.
void tick(uint32_t nowMs, bool eligible, const Context& c);
Kind     active();              // the one playing, or NONE
bool     activeShiny();
uint32_t activeSince();
void     finish();              // the player is done with it
// The console's ENCOUNTER <n>, for the bench: that one, now. With `count`
// false it is only played: the FIELD JOURNAL does not hear of it and the run
// of misses is left as it was (the P key's scenes).
void     force(Kind k, bool shiny = false, bool count = true);
// A roll that came to nothing, and that the board chose to show: about one
// miss in four. True once; the main screen then drops the die for nothing and
// Squachy wonders what it was. forceMiss() is the console's ENCOUNTER MISS.
bool     takeMiss();
// `face` is the number the dud shows, 1-5; 0 rolls one. A 1 is the one that
// bounces off his head.
void     forceMiss(uint8_t face = 0);
uint8_t  missRolled();          // the face of the miss being shown

// ---- the free roll ------------------------------------------------------------
// From the loop, on every board, whatever screen is up: counts running time
// towards the next one. tick() calls it too.
void     runTick(uint32_t nowMs);
bool     freeReady();           // one is waiting (never with ENCOUNTERS OFF)
uint16_t freeInMin();           // running time until the next, 0 while one waits
// Spends it: an honest d20, a hit written to the journal. The face it rolled,
// 0 when none was waiting or something is already playing.
uint8_t  rollFree(const Context& c);
void     setFree(bool waiting); // the bench's ENCOUNTER FREE, and the emulator
// Asked as the free roll's die is touched: about one time in MIMIC_ONE_IN it
// was never a die. True spends the roll and starts the MIMIC, with no throw.
// The dice never pick the mimic any other way.
const uint8_t MIMIC_ONE_IN = 8;
bool     takeMimic();
// Rolls waiting to happen in the next minute... for DIAGNOSTICS.
uint16_t misses();

// ---- the setting ---------------------------------------------------------------
uint8_t     level();
void        cycleLevel();
const char* levelName();

// ---- the FIELD JOURNAL ---------------------------------------------------------------
uint16_t met(Kind k);
uint32_t firstEpoch(Kind k);    // UTC of the first, 0 when the clock was not set or never met
uint16_t shinies(Kind k);
uint8_t  kindsMet();
// The Elder's photo was taken (tapped in time) this many times.
uint16_t photos();
void     notePhoto();
void     wipe();

}  // namespace Encounters
