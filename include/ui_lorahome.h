// SquachWatch-CYD — LORA, the T-Deck's landing page for its radio.
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
// Opened by Enter on the home screen and by the LORA pill. Built only with
// SQUACH_LORA on the T-Deck.
#pragma once
#include <TFT_eSPI.h>
#include <stdint.h>
#include "lora_pkt.h"

enum class LoraHomeHit : uint8_t { NONE = 0, CHAT_MESHTASTIC, CHAT_MESHCORE, SWITCH, CHATS, BACK,
                                   TAB_MESHTASTIC, TAB_MESHCORE, SETTINGS, CHANNEL, TAB_DMS, CHAT_DMS, RADAR_DOT };

void        uiLoraHomeInit(TFT_eSPI& t);
void        uiLoraHomeTick(TFT_eSPI& t, uint32_t now);
LoraHomeHit uiLoraHomeHit(TFT_eSPI& t, int x, int y);
// The switch: which network the compose line sends to. 0 Meshtastic, 1 MeshCore.
void        uiLoraHomeToggleNet();
// The channel chip at the compose line's right end: which channel of the
// switch's network Enter sends on. A tap steps it to the next one that can be
// sent on, round and back to the default (Lora::Chan::txStep, which stores
// it). No key does this: every printable key types, and Enter and DEL are
// taken.
void        uiLoraHomeStepChannel(uint32_t now);
// The chat window's tabs and the switch are one setting: 0 Meshtastic, 1 MeshCore.
void        uiLoraHomeSetNet(uint8_t net);
// The DMs tab of the chat window, and the conversation on the row at `y`
// (0 = no row there).
void        uiLoraHomeShowDms();
uint32_t    uiLoraHomeDmPeerAt(int y, Lora::Proto* p = nullptr);
// RADAR_DOT: the Meshtastic node whose dot on the dish was tapped.
uint32_t    uiLoraHomeTappedPeer();
Lora::Proto uiLoraHomeTappedProto();
uint8_t     uiLoraHomeNet();
// The keyboard while the page is up: printable keys type, '\b' deletes,
// '\n' sends what is typed on the switch's network.
void        uiLoraHomeKey(char c, uint32_t now);
