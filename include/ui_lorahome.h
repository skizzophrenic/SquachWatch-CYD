// SquachWatch-CYD — LORA, the T-Deck's and the Pager's landing page for the radio.
//
// One screen with everything the radio knows right now: a radar of the
// nodes on the left, the four numbers (NOW, FLOOR, PEAK, MARGIN) and the last
// 80 seconds of signal on the right, one line with the GPS and the nearest
// node, then one chat window tabbed MESHTASTIC / MESHCORE, and a compose line
// that sends to the open tab's network. Picked from the LoRa landing lab
// (2026-10-15, "Under the Dish, revised"), a nod to Cairn's Radio page in
// Squachy's colours; the waterfall made way for the chat window on
// 2026-10-16.
//
// The radar is honest. A node that has sent its position sits at its real
// bearing and distance (log scale, rings in miles) when the board has a fix;
// a node without one sits on the dotted ring at the rim, spaced out, because
// one antenna gives no direction. A filled dot was heard directly, a hollow
// one only through relays, and signal strength is never drawn as distance.
//
// Opened by the LORA pill, and on the T-Deck by Enter on the home screen.
// Built only with SQUACH_LORA on the T-Deck and the Pager.
#pragma once
#include <TFT_eSPI.h>
#include <stdint.h>

enum class LoraHomeHit : uint8_t { NONE = 0, CHAT_MESHTASTIC, CHAT_MESHCORE, SWITCH, CHATS, BACK,
                                   TAB_MESHTASTIC, TAB_MESHCORE };

void        uiLoraHomeInit(TFT_eSPI& t);
void        uiLoraHomeTick(TFT_eSPI& t, uint32_t now);
LoraHomeHit uiLoraHomeHit(TFT_eSPI& t, int x, int y);
// The switch: which network the compose line sends to. 0 Meshtastic, 1 MeshCore.
void        uiLoraHomeToggleNet();
// The chat window's tabs and the switch are one setting: 0 Meshtastic, 1 MeshCore.
void        uiLoraHomeSetNet(uint8_t net);
uint8_t     uiLoraHomeNet();
// The keyboard while the page is up: printable keys type, '\b' deletes,
// '\n' sends what is typed on the switch's network.
void        uiLoraHomeKey(char c, uint32_t now);
// The Pager's cursor stops, boxed: BACK, the two tabs, the open chat, the
// switch, CHATS.
// Fills up to cap rectangles; returns how many.
uint8_t     uiLoraHomeStops(int16_t* xs, int16_t* ys, int16_t* ws, int16_t* hs, uint8_t cap);
