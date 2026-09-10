// SquachWatch-CYD — the SquachMesh message screen, opened from the little
// speech bubble beside a visiting Squachy.
//
// The last message you received sits at the top in RED, which is the colour
// that means a person sent it -- scripted banter between Squachys is pink and
// black, and red never is. Below it are the canned lines; tap one and it goes.
//
// Canned lines rather than free text for now: a line is one byte, fits in bytes
// the scan response already has spare, and needs no fragmentation. Free text is
// the bigger half of the plan and waits until this half has been used.
#pragma once
#if SQUACH_MESH
#include <TFT_eSPI.h>
#include <stdint.h>

class DetectionEngine;

// HELP: the "?" -- replay the messages tutorial (see meshtutor.h).
enum class ComposeHit : uint8_t { NONE, SENT, BACK, HELP };

void       uiMeshComposeInit(TFT_eSPI& t);
void       uiMeshComposeTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng);
// SENT and BACK both mean "go back to the main screen"; SENT means a message
// is now on the air.
ComposeHit uiMeshComposeTouch(int x, int y, uint32_t now);
#endif
