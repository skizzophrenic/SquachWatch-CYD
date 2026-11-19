// SquachWatch-CYD — the SQUACHY-DEX screen, reached from Settings.
//
// Two views: the index, every entry as a tile with its icon (a silhouette
// until caught), and a card per entry with its lore and this board's record
// against it. Tap a tile for its card; < and > walk the cards; DEX goes back
// to the index; BACK on the index leaves the screen.
#pragma once
#include <TFT_eSPI.h>
#include <stdint.h>
#include "state.h"

class DetectionEngine;

void uiDexInit(TFT_eSPI& t);
// Open straight on a card: the emulator's --pose N, and anything that wants
// to show one entry.
void uiDexOpenCard(uint8_t entry);
// `advance` is false on the second of the 3.5"'s two band passes.
void uiDexTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng, bool advance = true);

enum class DexTap : uint8_t { NONE, HANDLED, BACK };
// Where a cursor should stop, for the boards with no touch panel.
uint8_t uiDexStops(int screenW, int screenH, int16_t* xs, int16_t* ys, uint8_t cap);
DexTap uiDexHitTest(TFT_eSPI& t, int x, int y, int screenW, int screenH);

// For the Settings row: "11/17".
uint8_t uiDexCaught(const DetectionEngine& eng);

#if defined(M5_STOPWATCH)
bool uiDexCardOpen();
#endif
