// SquachWatch-CYD -- the HUMAN SENSING screen (SENSE_CYD_APP).
//
// Reached from SETTINGS > HUMAN SENSING. While it is up the detector is paused
// and the CYD's radio senses the room instead (include/sense_cyd.h); the gear
// and BACK go to Settings, OFF straight back to the detector.
#pragma once
#include <TFT_eSPI.h>
#include <stdint.h>

#if defined(SENSE_CYD_APP)
void uiSensingInit(TFT_eSPI& t);
void uiSensingTick(TFT_eSPI& t, uint32_t now, bool advance = true);

// What a tap asks main.cpp to do.
// ADD_WIFI: no saved network, the banner tapped (to WIFI NETWORKS).
enum class SensingTap : uint8_t { NONE, BACK, RECAL, OFF, ADD_WIFI };
SensingTap uiSensingTap(int x, int y, int screenW, int screenH, uint32_t now);
// The middle of the banner, where the boards with no touch screen (StickS3,
// Cardputer) put their one cursor stop: pressed, it re-calibrates.
void uiSensingBannerStop(int screenW, int screenH, int16_t& x, int16_t& y);
#endif
