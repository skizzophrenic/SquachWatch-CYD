// SquachWatch-CYD — the time zone card
//
// Shown over the main screen once the clock is set and nobody has ever
// picked a zone: the board is on UTC and every hour-of-day line would be
// wrong. The zone name, the live time in it, and three buttons. THIS IS
// RIGHT stores the choice and the card never comes back; the arrows cycle
// the same table the SYSTEM row uses. Taps outside the card fall through to
// the main screen, and an alert still takes the screen over it.
#pragma once
#include <TFT_eSPI.h>
#include <stdint.h>

bool uiZoneCardWanted();
void uiZoneCardDraw(TFT_eSPI& t, uint32_t now);

enum class ZoneHit : uint8_t { NONE, CARD, PREV, NEXT, OK };   // CARD: on it, not on a button
ZoneHit uiZoneCardHit(int x, int y, int screenW, int screenH);
// The three buttons as rectangles, < then THIS IS RIGHT then >, for a board
// that steps a cursor instead of taking a finger. Returns how many (3).
uint8_t uiZoneCardStops(int screenW, int screenH, int16_t* xs, int16_t* ys, int16_t* ws, int16_t* hs);
