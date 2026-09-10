// SquachWatch-CYD — the SquachMesh consent gate.
//
// It stands in front of the SquachMesh menu, not in front of the TRANSMIT
// row, and that is deliberate: by the time somebody is looking at a switch
// they are already deciding whether to flip it, and a warning read in that
// position is read as an obstacle. This one is read as information, because
// it arrives before there is anything to click.
//
// The device's whole purpose is telling people when something nearby is
// broadcasting a stable identifier at them. SquachMesh asks the owner to do
// exactly that on purpose. That is a defensible thing to offer and an
// indefensible thing to switch on quietly, so the screen says plainly what
// the advert contains, how often it goes out, and what somebody with a
// scanner can reconstruct from it.
//
// Nothing transmits until YES is chosen here: Settings::meshTransmit() reads
// false without consent whatever the stored flag says, so the gate cannot be
// walked around by a stale preference from an older build.
#pragma once
#if SQUACH_MESH
#include <TFT_eSPI.h>
#include <stdint.h>

class DetectionEngine;

enum class MeshWarnHit : uint8_t { YES, NO, NONE };

void uiMeshWarnInit(TFT_eSPI& t);
// Takes the engine for the backdrop, same as the menu behind it -- see
// ui_meshmenu.h for why a screen without one silently gets digital rain.
void uiMeshWarnTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng);
MeshWarnHit uiMeshWarnHitTest(TFT_eSPI& t, int x, int y);
#endif
