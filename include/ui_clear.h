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
// The mascot's own clock. Squachy, the pet, a visitor, the crowd and the
// idle events step once per call with no notion of elapsed time -- they were
// tuned by eye on a board that drew sixteen frames a second, and the day the
// frame rate doubled they ran at double speed. The backgrounds scale their
// motion by elapsed time and did not. So the screen draws every frame, and
// the mascot STEPS on this clock: true when at least MASCOT_STEP_MS has
// passed since the last step, and the caller hands it to every tick() as
// `advance` -- which already means "draw, but do not move" when false,
// because the 3.5" board draws twice per frame and needed exactly that.
//
// 62 ms was the pace the 80 MHz boards had before they got fast; 120 is
// what two boards side by side said was right once they had. Live on the
// console as PACE N (milliseconds a step) and kept in settings, so the
// number is chosen by eye on a real board rather than argued about.
bool     uiMascotStep(uint32_t now, bool advance);
void     uiMascotStepSet(uint32_t ms);
uint32_t uiMascotStepMs();

void uiClearTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng,
                  bool advance = true, bool scanMenu = false);

#if defined(CROWPANEL7)
// The LORA pill: the main screen's door to the sniffer, in the title bar's
// empty middle beside the WATCH one. True when a tap landed on it; main.cpp
// opens the LORA screen, and BACK there returns HERE rather than to a settings
// page nobody opened.
//
// Drawn on every frame whether or not a module answered -- Lora::present()
// decides what it says and what colour it says it in, nothing more. A board
// with K1 on the card position has to be able to reach the screen that
// explains that ("NO MODULE: set K1 to the wireless position and restart",
// ui_lora.cpp), and a hidden pill would be one more thing with no way in.
//
// CrowPanel 7 only, the same condition SETTINGS' own LoRa rows are compiled
// under (ui_settings.cpp): it is the one board with a wireless slot, and on
// the others there is nothing behind the door.
bool uiClearLoraPillHit(int x, int y);
#endif

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
// The NEARBY headline, which only exists while something is live. Long-pressing
// it opens the closest device -- see main.cpp. False whenever it is not drawn.
bool uiClearNearbyHit(int x, int y);
// A tap on somebody in the CROWD, which puts his name over him for a few
// seconds. True if it landed on one. Past four of them the nameplates come
// off -- at eight they are more clutter than label -- and this is how you ask
// who one of them is. Takes `now` rather than reading the clock itself so the
// hit test stays a pure function of what the caller already knows.
bool uiClearCrowdTap(int x, int y, uint32_t now);

#if SQUACH_MESH
// The roaming crowd, drawn into a band. The desk borrows it (see
// ui_desk.cpp) rather than growing a second copy: the shrink-to-fit rule,
// the seat ours holds in the middle, the nameplates and the drift all
// live here. `bottomInset` is what the caller keeps clear under the band
// -- 22 on the main screen for the squad badge and the counters, 0 on the
// desk, which has neither.
//
// `grow` enlarges them past the size the band would pick (still no taller
// than the band); `bubbleY`, when not -1, is the row the speech bubbles
// hang from instead of just over their heads. The desk uses both for a pair:
// bigger, with their words up on the clock.
void uiClearDrawCrowd(TFT_eSPI& t, uint32_t now, const Mesh::SquadMember* crowd,
                      uint8_t n, int top, int floorY, bool advance, bool msgFresh,
                      int bottomInset, float grow = 1.0f, int bubbleY = -1);

// The visit machine, for a screen other than the main one that has the squad
// on it (the desk): who is visiting, whose turn it is to talk, the laughs and
// the set pieces. Once a frame, before drawing.
void uiClearVisitTick(uint32_t now);
// The main screen's ordinary visit -- ours on the left, the visitor walking
// in on the right -- drawn into the band from `top` to `floorY`. False, and
// nothing drawn, when nobody is visiting.
bool uiClearDrawVisit(TFT_eSPI& t, uint32_t now, int top, int floorY, bool advance);
#endif
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
