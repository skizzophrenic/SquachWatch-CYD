// SquachWatch-CYD -- the HUMAN SENSING screen (SENSE_CYD_APP).
//
// Reached from SETTINGS > HUMAN SENSING. While it is up the detector is paused
// and the CYD's radio senses the room instead (include/sense_cyd.h); the gear
// and BACK go to Settings, OFF straight back to the detector.
#pragma once
#include <TFT_eSPI.h>
#include <stdint.h>
#include "sense_cyd.h"

#if defined(SENSE_CYD_APP)
void uiSensingInit(TFT_eSPI& t);
void uiSensingTick(TFT_eSPI& t, uint32_t now, bool advance = true);

// What a tap asks main.cpp to do.
// ADD_WIFI: no saved network, the banner tapped (to WIFI NETWORKS).
// The same three buttons on every board: BACK (leave; the detector gets the
// radio back), the page's own action (RECAL, START RUN, NEW SESSION), and
// SLEEP (the screen off, sensing on; a tap or a key wakes it). SNEAK keeps
// LEVEL on the right: a game is not left running in the dark.
enum class SensingTap : uint8_t { NONE, BACK, RECAL, SLEEP, ADD_WIFI };
SensingTap uiSensingTap(int x, int y, int screenW, int screenH, uint32_t now);
// The middle of the banner, where the boards with no touch screen (StickS3,
// Cardputer) put their one cursor stop: pressed, it re-calibrates.
void uiSensingBannerStop(int screenW, int screenH, int16_t& x, int16_t& y);

#if SENSE_RICH
// The boards with SCOPE, SNEAK and VITALS (include/sense_cyd.h): four screens
// a swipe apart, 0 SENSING, 1 SCOPE, 2 SNEAK, 3 VITALS. The dots in the title
// bar say which, and a tap on them turns the page too.
void    uiSensingSwipe(int dir);          // -1 back a screen, +1 on
uint8_t uiSensingPage();
// The keyboard (T-Deck): , and . turn the page; ENTER starts a SNEAK run, L its
// level; N a new VITALS session. True when the key was taken.
bool    uiSensingKey(char c, uint32_t now);
// The trackball's stops: the three buttons and the page dots.
uint8_t uiSensingStops(int screenW, int screenH, int16_t* xs, int16_t* ys, uint8_t cap);
#endif
#endif
