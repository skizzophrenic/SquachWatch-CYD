// Emote scripts: every table the engine in ui_clear.cpp plays is well formed,
// and both boards reach the same result from the same setup byte.
//
// The engine itself draws, so it is checked in the emulator ("emote N"); what
// is checked here is everything it trusts without checking -- a pose or an
// effect past the end of its enum, a line pool that does not exist, a script
// too long to finish before the next one is due, a tab that leaves an emote out.
#include "emote_script.h"
#include "state.h"
#include "test_util.h"
#include <cstring>
#include <cstdio>

using namespace EmoteScript;
using E = MeshMsg::Emote;

int main() {
    suite("Every scripted emote has a well-formed script");
    {
        bool originals = true, all = true, beats = true, lines = true, total = true;
        for (uint8_t i = 0; i < (uint8_t)E::COUNT; i++) {
            const Script* s = script((E)i);
            if (i < (uint8_t)E::FIST_BUMP) { if (s) originals = false; continue; }
            if (!s || !s->beat || s->n == 0) { all = false; continue; }
            if (totalMs(*s) > MAX_MS || totalMs(*s) < 1500) {
                total = false;
                printf("    emote %u runs %lu ms\n", (unsigned)i, (unsigned long)totalMs(*s));
            }
            for (uint8_t b = 0; b < s->n; b++) {
                const Beat& bt = s->beat[b];
                if (bt.ms < 300 || (uint8_t)bt.a >= (uint8_t)Pose::COUNT ||
                    (uint8_t)bt.b >= (uint8_t)Pose::COUNT || (uint8_t)bt.fx >= (uint8_t)Fx::COUNT)
                    beats = false;
                if (bt.line && !isDyn(bt.line) && !line(bt.line, 0)) lines = false;
                if ((bt.fx == Fx::THROW || bt.fx == Fx::LOB || bt.fx == Fx::HOLD) &&
                    bt.fxArg >= (uint8_t)Obj::COUNT) beats = false;
                if (bt.fx == Fx::HIT && bt.fxArg >= (uint8_t)Burst::COUNT) beats = false;
            }
        }
        ck("the six originals have none -- ui_clear plays those itself", originals);
        ck("every other emote has one", all);
        ck("every beat's poses and effects are inside their enums", beats);
        ck("every line a beat names exists", lines);
        ck("every script runs between 1.5 and 9 seconds", total);
    }

    suite("Lines");
    {
        bool fits = true, filled = true;
        for (uint8_t p = 1; p < lineCount(); p++)
            for (uint8_t v = 0; v < VARIANTS; v++) {
                const char* l = line(p, v);
                if (!l || !l[0]) filled = false;
                else if (strlen(l) > 20) { fits = false; printf("    too long: %s\n", l); }
            }
        ck("every pool has three lines", filled);
        ck("and none is longer than a small bubble holds", fits);
        ck("pool 0 is silence", line(0, 0) == nullptr);
        ck("a pool past the end is silence too", line(lineCount(), 0) == nullptr);

        char b[32];
        dynLine(DYN_SPOTTED, E::SPOTTED, (uint8_t)DetectionType::FLOCK, b, sizeof b);
        ck("the detection is named", strcmp(b, "See that FLOCK?!") == 0);
        dynLine(DYN_SPOTTED, E::SPOTTED, 0, b, sizeof b);
        ck("and with nothing caught yet it still makes sense", strcmp(b, "Did you see that?!") == 0);
        bool spotFits = true;
        for (uint8_t t = 0; t < (uint8_t)DetectionType::COUNT; t++) {
            dynLine(DYN_SPOTTED, E::SPOTTED, t, b, sizeof b);
            if (strlen(b) > 20) { spotFits = false; printf("    too long: %s\n", b); }
        }
        ck("every detection's line fits a bubble", spotFits);
        dynLine(DYN_COIN_CALL, E::COIN, 1 | 2, b, sizeof b);
        ck("a right call on tails is TAILS", strcmp(b, "TAILS!") == 0);
        dynLine(DYN_COIN_CALL, E::COIN, 1, b, sizeof b);
        ck("a wrong call on tails is HEADS", strcmp(b, "HEADS!") == 0);
        dynLine(DYN_DICE, E::DICE, 5 * 6 + 2, b, sizeof b);
        ck("dice gloat with the winner's number first", strcmp(b, "6 beats 3!") == 0);
        dynLine(DYN_DICE, E::DICE, 1 * 6 + 5, b, sizeof b);
        ck("whichever side won", strcmp(b, "6 beats 2!") == 0);
        dynLine(DYN_DICE, E::DICE, 3 * 6 + 3, b, sizeof b);
        ck("and a tie is a tie", strcmp(b, "Tie! 4 and 4.") == 0);
    }

    suite("The setup byte: one roll, one result, on both boards");
    {
        bool inRange = true;
        for (uint32_t r = 0; r < 5000; r++) {
            if (roll(E::RPS, r * 2654435761u, 0) >= 9)   inRange = false;
            if (roll(E::COIN, r * 2654435761u, 0) >= 4)  inRange = false;
            if (roll(E::DICE, r * 2654435761u, 0) >= 36) inRange = false;
            if (roll(E::TUG, r * 2654435761u, 0) >= 2)   inRange = false;
        }
        ck("every roll lands inside what its emote reads", inRange);
        ck("an emote with nothing to agree on sends zero", roll(E::HUG, 12345, 3) == 0);
        ck("SPOTTED carries the last detection", roll(E::SPOTTED, 99, (uint8_t)DetectionType::DRONE) ==
                                                  (uint8_t)DetectionType::DRONE);
        ck("and never a type this build does not have",
           roll(E::SPOTTED, 99, (uint8_t)DetectionType::COUNT) == 0);

        ck("paper beats rock", outcome(E::RPS, 1 * 3 + 0) == Result::SENDER);
        ck("rock loses to paper", outcome(E::RPS, 0 * 3 + 1) == Result::RECEIVER);
        ck("scissors and scissors is a tie", outcome(E::RPS, 2 * 3 + 2) == Result::TIE);
        ck("the caller wins a coin they called", outcome(E::COIN, 2) == Result::RECEIVER);
        ck("the flipper wins one they did not", outcome(E::COIN, 1) == Result::SENDER);
        ck("the higher die wins", outcome(E::DICE, 5 * 6 + 0) == Result::SENDER &&
                                  outcome(E::DICE, 0 * 6 + 5) == Result::RECEIVER);
        ck("equal dice tie", outcome(E::DICE, 2 * 6 + 2) == Result::TIE);
        ck("dice faces run one to six", die(0, 0) == 1 && die(35, 0) == 6 && die(35, 1) == 6 && die(6, 1) == 1);
        ck("arm wrestling's bit is the sender winning",
           outcome(E::ARM_WRESTLE, 1) == Result::SENDER && outcome(E::ARM_WRESTLE, 0) == Result::RECEIVER);
        ck("an emote with no contest has no result", outcome(E::HUG, 1) == Result::NONE);
    }

    suite("The picker shows every emote exactly once");
    {
        uint8_t seen[(size_t)E::COUNT] = {};
        bool named = true;
        int empty = 0;
        for (uint8_t t = 0; t < TABS; t++)
            for (uint8_t i = 0; i < PER_TAB; i++) {
                const E e = atTab(t, i);
                // E::COUNT in a slot is an empty tile, which the picker skips.
                if ((uint8_t)e >= (uint8_t)E::COUNT) { empty++; continue; }
                seen[(uint8_t)e]++;
                if (!name(e)[0] || strlen(name(e)) > 10 || strlen(sub(e)) > 10) named = false;
            }
        bool once = true;
        for (uint8_t i = 0; i < (uint8_t)E::COUNT; i++) if (seen[i] != 1) once = false;
        ck("six tabs of six hold all thirty-five, and one empty tile", TABS * PER_TAB == (int)E::COUNT + 1);
        ck("exactly one tile is empty", empty == 1);
        ck("each on exactly one tab", once);
        ck("each with a name that fits its tile", named);
    }

    return report();
}
