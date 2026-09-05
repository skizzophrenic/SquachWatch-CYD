// SquachWatch-CYD — ignored-devices screen, reached via Settings'
// "IGNORED DEVICES" row. A flat scrollable list, one row per muted MAC,
// each with a REMOVE hit zone on the right. Same row-list shape as the
// detection filter, which is the closest existing sibling: a list of
// things you turn off, one tap per row.
//
// The list is short by construction (IgnoreList::MAX is 64) and is
// usually far shorter, so there is no search or paging here -- scrolling
// a handful of rows is the whole interaction.
#pragma once
#include <TFT_eSPI.h>
#include <stdint.h>
#include "state.h"

void uiIgnoreListInit(TFT_eSPI& t);
void uiIgnoreListTick(TFT_eSPI& t, uint32_t now);
void uiIgnoreListScroll(int delta);   // positive = scroll down

// Index of the row whose REMOVE zone (x,y) falls in, or 0xFF for none.
// Only the REMOVE zone is hit-testable: the rest of a row does nothing,
// so a stray tap while scrolling cannot silently un-mute a device you
// deliberately muted. Needs a live TFT_eSPI& because row height comes
// from real font metrics, same as every other row list here.
uint8_t uiIgnoreListHitRemove(TFT_eSPI& t, int x, int y, int screenW, int screenH);
