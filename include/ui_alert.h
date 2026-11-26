// SquachWatch-CYD — full-screen ALERT screen: the Trail card.
//
// Two colour tabs (the verdict, the type) with the owner and met-before
// chips beside them; a transit line of the stops the device was heard at,
// with the times, the distances and the tag on the line behind HERE; a row
// of ticks saying why; the last minute of signal as a ladder; four rows of
// facts; and five equal buttons: RING (or HUNT), WATCH, SNOOZE, IGNORE, INFO.
#pragma once
#include <TFT_eSPI.h>
#include <stdint.h>
#include "state.h"

class DetectionEngine;

void uiAlertInit(TFT_eSPI& t, const Detection& d);
// The first of this type this board has ever caught: the card says so.
void uiAlertSetFirst(bool first);
// Caught between eleven at night and five in the morning, by the real
// clock: the card says so. False whenever the clock is not set.
void uiAlertSetNight(bool night);
// This device has just used the last of its AUTO SNOOZE allowance: the card
// says so, on the alert it is spending.
void uiAlertSetLastFree(bool lastFree);
// This alert is a spam flood's one announcement (see spam_watch.h): the
// verdict tab says SPAM FLOOD and the chip says how many fakes so far.
void uiAlertSetSpam(bool spam, uint16_t fakes);
// How many catches wait behind this card (the +N MORE chip). 0 hides it.
void uiAlertSetQueue(uint8_t n);
// WATCH is held down while this device is the watched one.
void uiAlertSetWatching(bool on);
// SNOOZE was tapped: the four time chips are up over the facts until one is
// picked or the card is tapped elsewhere.
void uiAlertSetSnoozePicking(bool on);
bool uiAlertSnoozePicking();
// Kept for the callers that still set a label: no-ops on this card.
void uiAlertSetSnoozePick(const char* label, uint16_t left);
// While the device is locked with ALERTS WHEN LOCKED at TYPE ONLY: the type
// and the signal still show, the device's name, label and address do not.
void uiAlertSetRedacted(bool redacted);
#if defined(CYD_GPS)
// Where the board was when it caught this (unused by the trail card, which
// shows places instead; kept so the callers compile).
void uiAlertSetPlace(bool has, int32_t lat7, int32_t lon7, bool fake);
#endif
// eng: the live row is re-read from it each frame, so the ladder and the
// trail move while the card is up.
void uiAlertTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng,
                 bool infoPending, const char* infoTypeName, const char* infoText);
bool uiAlertTouched();   // any touch since uiAlertInit
void uiAlertNoteTouch();

// The five buttons along the bottom. Slot 1 is RING on a tracker alert on
// the boards built with it, HUNT otherwise: uiAlertHitRing() and
// uiAlertHitHunt() answer for whichever it is.
bool uiAlertHitMoreInfo(int x, int y, int screenW, int screenH);
bool uiAlertHitHunt(int x, int y, int screenW, int screenH);
bool uiAlertHitWatch(int x, int y, int screenW, int screenH);
bool uiAlertHitSnooze(int x, int y, int screenW, int screenH);
bool uiAlertHitIgnore(int x, int y, int screenW, int screenH);
bool uiAlertRingShown();
bool uiAlertHitRing(int x, int y, int screenW, int screenH);
bool uiAlertRingCentre(int screenW, int screenH, int& cx, int& cy);
void uiAlertSetRinging(bool on);
// The +N MORE chip, when a queue is showing.
bool uiAlertHitQueue(int x, int y, int screenW, int screenH);
// While the snooze chips are up: which one (0..3) a tap landed on, or -1.
int  uiAlertHitSnoozeChip(int x, int y, int screenW, int screenH);
// The key-cursor stops (StickS3, Cardputer, the T-Deck's trackball): the
// five buttons left to right, then the queue chip when it is up.
uint8_t uiAlertStops(int screenW, int screenH, int16_t* xs, int16_t* ys, uint8_t cap);

// The card's skin (Settings > APPEARANCE > ALERT CARD, see alert_skin.h) reads
// the 8-bit frame back for its glass and scanlines. The board's frame, when it
// draws the whole screen in one; nullptr otherwise.
void uiAlertSetFrame(uint8_t* buf, int w, int h);
// ALERT CARD's PREVIEW: a made-up AirTag that has been following for 41
// minutes, so the chosen skin can be seen without waiting for a real one.
// uiAlertSetPreview(true) after uiAlertInit() with it; the card then reads
// FOLLOWING whatever the trail engine would say about a row it has never seen.
void uiAlertMakeSample(Detection& d);
void uiAlertSetPreview(bool on);
bool uiAlertPreviewing();
// The style browser's strip along the bottom: the style's name, which of the
// twelve, and an arrow in each corner. A tap on an arrow steps the style (and
// keeps it) and restarts the card's opening: true. Anywhere else: false.
bool uiAlertPreviewArrow(int x, int y, int screenW, int screenH);
// A card opened by hand -- holding a LOG row -- rather than by a catch. For a
// tracker its third button is HUNT instead of SNOOZE: the first is RING, and
// hunting a tag you looked up on purpose is likelier than snoozing it.
void uiAlertSetByHand(bool on);
