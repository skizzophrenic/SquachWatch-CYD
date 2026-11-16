// SquachWatch-CYD — LORA > NODES, the T-Deck's list of every node heard.
//
// The LORA page's dish shows who is around; this is the same table as a list,
// so a node far away, one with no position, or one heard more than an hour
// ago can still be found by name. Opened by a tap on the LORA page's "N NODES"
// header or on an empty part of the dish; BACK returns there.
//
// Three tabs: ALL (every network the radio decodes), MESHTASTIC, MESHCORE,
// and a DIRECT switch beside them that keeps only the nodes this radio has
// heard transmit themselves -- its neighbours, not the whole mesh behind
// the relays. The switch is remembered, and the LORA page's NODES tab
// follows it.
// One row a node, the most recently heard first: its name, network, handle and
// role, how it was heard (direct with its signal, or through a relay and how
// many hops), and how long ago. A Meshtastic row, or a MeshCore one whose
// advert gave us its key, opens a private conversation when tapped.
//
// The table is what this radio has heard since it started: it lives in RAM, so
// a node shows up when it next transmits (a NodeInfo, a position, an advert, a
// message), not before.
//
// Built with SQUACH_LORA on the T-Deck, and in the emulator (SQUACH_LORA_FAKE).
#pragma once
#include <TFT_eSPI.h>
#include <stdint.h>
#include "lora_pkt.h"

enum class LoraNodesHit : uint8_t { NONE = 0, BACK, OPEN_DM };

void         uiLoraNodesInit(TFT_eSPI& t, bool fresh);
void         uiLoraNodesTick(TFT_eSPI& t, uint32_t now);
// A tap that was not a drag. BACK: leave for the LORA page. OPEN_DM: open the
// conversation with uiLoraNodesPeer() / uiLoraNodesPeerProto().
LoraNodesHit uiLoraNodesTap(int x, int y, uint32_t now);
uint32_t     uiLoraNodesPeer();
Lora::Proto  uiLoraNodesPeerProto();
// A drag, in pixels: positive shows what is further down.
void         uiLoraNodesScrollBy(int px);
// The keyboard: W / S scroll, A / T / C pick the ALL / MESHTASTIC / MESHCORE tab,
// D flips DIRECT (Settings::loraNodesDirect: only nodes heard first-hand).
void         uiLoraNodesKey(char c, uint32_t now);
