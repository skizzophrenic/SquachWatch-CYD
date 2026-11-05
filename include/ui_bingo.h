// SquachWatch-CYD — the bingo card screen, reached from Settings' BINGO row.
//
// Two views behind one screen: the card, sixteen squares of the real
// detection icons, and the stats behind the STATS button. A tap on a square
// opens the type's own MORE INFO paragraph, which is why a game about
// finding a plate reader also teaches what one looks like.
#pragma once
#include <TFT_eSPI.h>
#include <stdint.h>
#include "state.h"

class DetectionEngine;

void uiBingoInit(TFT_eSPI& t);
// `advance` is false on the second of the 3.5"'s two band passes -- the
// same frame drawn again -- so anything that steps by the call rather
// than by the clock must sit still for it. Other boards draw once.
void uiBingoTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng, bool advance = true);

// What a tap did. HANDLED means the screen dealt with it (a square opened, a
// panel closed, NEW asked whether it really meant it); BACK means leave the
// screen -- for the board's own screen, not back into the settings menu.
enum class BingoTap : uint8_t { NONE, HANDLED, BACK };
BingoTap uiBingoHitTest(TFT_eSPI& t, int x, int y, int screenW, int screenH);
// The boards with buttons for a finger: every target drawn this frame, as
// rectangles, for the cursor to step through. Only what is live.
uint8_t uiBingoStopRects(TFT_eSPI& t, int16_t* rx, int16_t* ry, int16_t* rw, int16_t* rh, uint8_t cap);
bool    uiBingoBackRect(TFT_eSPI& t, int16_t& x, int16_t& y, int16_t& w, int16_t& h);
