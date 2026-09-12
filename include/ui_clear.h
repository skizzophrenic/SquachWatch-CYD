// SquachWatch-CYD — "clear" (idle) screen
#pragma once
#include <TFT_eSPI.h>
#include <stdint.h>
#include "detection.h"

void uiClearInit(TFT_eSPI& t);
// advance: see Squachy::tick()'s header comment -- gates state
// mutation for boards that render in multiple physical bands per
// logical frame. Defaults to true (unchanged behavior for single-pass
// boards).
// scanMenu: true while the SCAN button's BLE/WIFI picker is open --
// swaps the bottom bar to Theme::ButtonBarMode::SCAN_PICKER. Nothing
// else on this screen changes; the caller (main.cpp) is what actually
// interprets a tap on the relabeled slots differently.
void uiClearTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng,
                  bool advance = true, bool scanMenu = false);

#if SQUACH_MESH
// SPIKE: the peer currently visiting, or nullptr. Owned by whatever discovers
// peers -- for now that is only the emulator's --peer flag, so the CLEAR screen
// can be built and looked at before any radio exists.
namespace SquachMesh { struct Peer; }
const SquachMesh::Peer* uiClearGuest();
void uiClearSetGuest(const SquachMesh::Peer* p);
// The little speech bubble beside a visitor, which opens the message screen.
// Its rectangle is filled in by the draw; this reads it.
bool uiClearBubbleHit(int x, int y);
// The "+N" squad badge beside a visitor, which opens the SQUAD screen.
bool uiClearSquadHit(int x, int y);

// The watch/hunt indicator, bottom left of CLEAR. True when a tap landed on
// it; main.cpp opens the watch-alert screen, which is where the target is
// named and where REMOVE FROM WATCH LIST lives. Only ever true while a watch
// or a hunt is actually set -- the pill is not drawn otherwise.
bool uiClearWatchPillHit(int x, int y);
// A tap on somebody in the CROWD, which puts his name over him for a few
// seconds. True if it landed on one. Past four of them the nameplates come
// off -- at eight they are more clutter than label -- and this is how you ask
// who one of them is. Takes `now` rather than reading the clock itself so the
// hit test stays a pure function of what the caller already knows.
bool uiClearCrowdTap(int x, int y, uint32_t now);
// An emote (MeshMsg::Emote) and its setup: one this board just sent, or one from the
// visitor's board (fromGuest). The two of them act it out at the next free
// moment of the visit -- whoever sent it going first -- or not at all if none
// comes within a few seconds. False when there is no visit to act it out in.
bool uiClearEmote(uint8_t emote, uint8_t setup, bool fromGuest);
// Received emotes: taken off MeshTalk, held until the visit can act one out,
// and expired if it never can.
//
// Called from main.cpp's loop, NOT from the draw. It lived inside
// uiClearTick()'s Squachy branch, which runs only on frames that actually
// draw him -- so an emote that arrived while a message bubble was up, the
// scan picker was open, an alert had the screen, or boring mode was on sat
// untouched until it aged out. On hardware that was most of them: decrypted,
// logged, never acted out. The sender never noticed because it plays its own
// straight from the compose screen.
void uiClearEmoteTick(uint32_t now);
#endif
