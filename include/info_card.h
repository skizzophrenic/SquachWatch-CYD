// SquachWatch-CYD — the MORE INFO card (picked 2026-11-23 from the Info Card
// Studio: COLOURED BY KIND, "F1").
//
// A card in the device's own colours: a banner with its name and a SURE /
// LIKELY / MAYBE pill, a one-line headline, WHY (the reason for the pill),
// the description, a TIP box exactly as tall as its advice, and four tiles
// docked along the bottom (SEEN, PLACES, FIRST, and FOLLOWING or STAYS PUT).
// The description pages behind MORE when it does not fit. Sizes per panel:
// the T-Deck/2.8in, the watch, the 2.8in upright and the 3.5in each have
// their own (see sizeFor() in info_card.cpp).
//
// How it is reached: main.cpp hands the card in every frame an INFO page is
// up for a detection (feed()), and Theme::drawInfoPanel() draws it instead
// of the old panel while it is fresh. The RSSI primer, Bingo and the small
// boards' compact panel keep the old panel.
#pragma once
#include <stdint.h>
#include <TFT_eSPI.h>
#include "state.h"

namespace InfoCard {

struct Card {
    const char* name;      // the heading (a device page's title, or the type's)
    const char* head;      // one line: what it is
    const char* why;       // why the pill says what it says
    uint8_t     conf;      // 0 MAYBE, 1 LIKELY, 2 SURE
    const char* body;      // the paragraph
    const char* tip;       // what to do
    uint16_t    kindA, kindB;
    bool        history;   // the four tiles: only for a row the board has seen
    char        seen[8];   // "41x"
    char        places[4]; // "3"
    char        first[8];  // "08:12"
    bool        following; // FOLLOWING (red) or STAYS PUT
    DetectionType type;    // for the window skins' title icon
};

// main.cpp: the card for this frame. Copied; the strings must outlive it.
void feed(const Card& c, uint32_t now);
// Whether a card was fed this frame or the last (drawInfoPanel asks).
bool fresh(uint32_t now);
void draw(TFT_eSPI& t, int w, int h, uint32_t now);
// A tap while the card is up: true to close it (BACK or GOT IT); MORE turns
// the page and returns false.
bool tapCloses(int x, int y, int w, int h);
// Where the last draw put button i (0 BACK, 1 MORE, 2 GOT IT), for the
// keyboard boards' cursor. False when that button is not up.
bool buttonCenter(uint8_t i, int& x, int& y);

}  // namespace InfoCard
