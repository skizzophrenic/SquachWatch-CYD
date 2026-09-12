// SquachWatch-CYD — the SquachMesh menu, reached via Settings' "SQUACHMESH"
// row.
//
// It exists because SquachMesh is more than one decision and they are not
// the same decision. Detecting and transmitting have genuinely different
// consequences -- one shows you other people's Squachys, the other tells
// everybody with a scanner that you are here -- and collapsing them into a
// single ON/OFF was reported as broken twice before it was split.
//
// It also buys a row back on the Settings screen, which carries twenty-five
// and has three that collide in portrait: the NAME row moved in here, so two
// rows out there became one.
//
// Deliberately not scrollable. Six rows fit, and a list that cannot
// overflow does not need the gesture -- see uiDetFilterScroll for the one
// that does.
#pragma once
#if SQUACH_MESH
#include <TFT_eSPI.h>
#include <stdint.h>

class DetectionEngine;

// What a tap landed on. NONE means it hit a gap.
// In the order they are drawn -- the hit test maps a row index straight onto
// this, so the two orders must never differ.
enum class MeshMenuRow : uint8_t { DETECT, TRANSMIT, MESSAGES, CROWD, PHRASE, NAME, BACK, NONE };

void uiMeshMenuInit(TFT_eSPI& t);
// Takes the engine only for the backdrop: THE GIBSON reads the log and the
// live per-channel activity, so it cannot be drawn without one. A screen
// that quietly served digital rain instead because it had no engine is a
// mistake this codebase has already made once.
void uiMeshMenuTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng);
MeshMenuRow uiMeshMenuHitTest(TFT_eSPI& t, int x, int y, int screenW, int screenH);
#endif
