// SquachWatch-CYD — the crowd benchmark: how many Squachys the CLEAR screen
// can hold, and what each one costs, measured on the board.
//
// A TEST BUILD ONLY. Compiled into the *-crowd environments and nothing else;
// every shipping build leaves CROWD_BENCH undefined and this is empty.
//
// After boot, the first time CLEAR is up, it runs a set of scenes -- rows on
// the ground, a two-row crowd, and floating crowds that fill the screen --
// each for ten seconds of drawn frames, with two of them talking. Each scene's
// frame time, crowd draw time, background and push are averaged, printed to
// serial as it ends, and put in a table on screen at the end. Then the screen
// goes back to normal.
//
// A tap skips to the next scene, or closes the table. Over serial: CROWD
// restarts it, CROWD OFF stops it, CROWD N holds scene N until told otherwise.
#pragma once
#if CROWD_BENCH
#include <TFT_eSPI.h>
#include <stdint.h>

namespace CrowdBench {
bool active();
// Draws the crowd in place of the Squachys on CLEAR, between `top` and `floorY`.
void draw(TFT_eSPI& t, uint32_t now, int top, int floorY);
// After the whole screen is drawn: the numbers at the top, or the table.
void drawOver(TFT_eSPI& t, uint32_t now);
// The end of every loop: the whole frame, and the push inside it.
void noteFrame(uint32_t frameUs, uint32_t pushUs);
void tap();
void command(const char* args);
}
#endif
