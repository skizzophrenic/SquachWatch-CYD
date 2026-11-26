#pragma once
// The December look: snow, a floor drift and string lights on the home
// screen, and a Santa hat on him, from December 1 to 26 by the clock.
//
// Nothing is stored between frames. The snow is the desk clock's snow
// (every flake worked out from the time), the drift and the bulbs come
// from formulas, and the bulb that flares is picked from the advert count.
#include <stdint.h>

class TFT_eSPI;

namespace December {

// The clock says December 1 to 26, or the console forced it (XMAS).
// Read once a second at most.
bool active(uint32_t nowMs);

// XMAS 0 = by the date (and SEASONAL), 1 = always on, 2 = always off.
// Not saved.
void setForce(uint8_t mode);
uint8_t force();

// Behind him: the far snow, the string lights hanging under `top`, and the
// drift along `floorY`. `w` is the screen width.
void back(TFT_eSPI& t, uint32_t now, int w, int top, int floorY);
// In front of him: the near snow.
void front(TFT_eSPI& t, uint32_t now, int w, int top, int floorY);

// The hat, on a head whose top edge is `hy` and centre `cx`, the way the
// outfits are drawn (see drawOutfit() in squachy.cpp).
void hat(TFT_eSPI& t, int cx, int hy, uint32_t now, float scale);

// The HOLIDAY MODE card: once a year, over the main screen, the first time
// December comes on. KEEP IT leaves it on; TURN OFF sets SEASONAL to OFF
// (Settings > APPEARANCE turns it back on). Either answer stops the card
// asking again that year. Taps beside the card fall through, like the time
// zone card's (ui_zone.h).
bool cardWanted(uint32_t now);
void cardDraw(TFT_eSPI& t, uint32_t now);
enum class CardHit : uint8_t { NONE, CARD, OFF, KEEP };   // CARD: on it, not on a button
CardHit cardHit(int x, int y, int screenW, int screenH);
void cardStops(int screenW, int screenH, int16_t xs[2], int16_t ys[2]);
void cardAnswer(bool keep);

}  // namespace December
