// SquachWatch-CM0 — the LORA page (T-Deck style, 320x170).
//
// The firmware's LORA_HOME page is TDECK_PLUS-only (T-Deck layout, 320x240).
// lora_home.cpp is a port of its drawing code (src/ui_lorahome.cpp) with the
// vertical layout compressed for this panel: the dish, the tiles, the strip,
// the info line, the tabbed chat window, the compose line and the switch.
// No touch panel here, so keys drive it. Transmit is real (via
// Lora::sendText); the compose line says "LORA TX IS OFF: SETTINGS" while
// the firmware's LORA TX setting (SETTINGS > SYSTEM, as upstream) is off. It is harness-owned: it never
// touches the firmware's drawing, which is overwritten every frame while
// the page is up.
#pragma once
#include <cstdint>
#include "TFT_eSPI.h"

// The page's keys, by direction: the card keys fold Down/Right/TAB into one
// NEXT, but the page's cursor needs them apart.
enum class LmKey { None, Back, Enter, Up, Down, Left, Right, Tab };

namespace cm0 {

void  loraHomeEnter();     // enter the page (reset its own state)
void  loraHomeLeave();     // leave the page (clear own state)
bool  loraHomeActive();

// Draw the whole page onto `t` (the firmware's canvas, which is what gets
// blitted). `now` is on the caller's clock (SDL/mono ticks, same as the app).
void  loraHomeDraw(TFT_eSPI& t, uint32_t now);

// One key on the page (key-down only). The page has a cursor like the rest
// of the app: TAB steps it through tabs -> chat rows -> radio switch ->
// BACK; arrows move it (left/right change tab, up/down scroll the rows);
// ENTER presses what it is on. Returns true when the page should close
// (BACK pressed, or the Back key).
bool  loraHomeKey(LmKey k, uint32_t now);

// Compose + TX: one typed character at a time (printable chars, '\n' to
// send, '\b' to delete). Mirrors upstream's uiLoraHomeKey(char, now):
// the line takes text whether or not TX is on; Enter refuses when off.
void  loraHomeChar(char c, uint32_t now);
// True while the compose box has text (the harness routes Enter/BACKSPACE
// to the page instead of nav while this is set).
bool  loraHomeComposing();

// The switch (upstream's uiLoraHomeToggleNet): which network the page shows
// and sends to. It never retunes the radio -- that is SETTINGS > SYSTEM >
// LORA, saved, as upstream.
void  loraHomeToggleNet();

}
