// SquachWatch-CYD — the SquachMesh message screen: the last message received,
// and something to say back -- a ready-made line, or your own typed on the
// payphone. Opened from the speech bubble on the main screen.
#pragma once
#if SQUACH_MESH
#include <TFT_eSPI.h>
#include <stdint.h>

class DetectionEngine;

// HELP: the "?" -- replay the messages tutorial (see meshtutor.h).
// TYPE: open the keyboard with uiMeshComposeTyped() -- TYPE, or EDIT on a
// typed message waiting to be sent.
enum class ComposeHit : uint8_t { NONE, SENT, BACK, HELP, TYPE };

void       uiMeshComposeInit(TFT_eSPI& t);
void       uiMeshComposeTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng);
ComposeHit uiMeshComposeTouch(int x, int y, uint32_t now);

// Back from the keyboard with a message: show it large, with EDIT and SEND.
void        uiMeshComposeSetTyped(const char* text);
// What the keyboard should start from ("" for a new message).
const char* uiMeshComposeTyped();
#endif
