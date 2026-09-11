// SquachWatch-CYD — the payphone: a keypad (and a QWERTY bailout) for typing.
//
// Two jobs, one screen. Your Squachy's name -- twelve letters, saved on OK --
// and a SquachMesh message -- up to 48 characters with digits and punctuation,
// handed back to the message screen on OK to be read over before it is sent.
// The keyboards grow the extra characters only for a message: a name has to
// fit an advert every older board decodes, and letters are what they accept.
#pragma once
#if SQUACH_MESH
#include <TFT_eSPI.h>
#include "detection.h"

// Your Squachy's name, starting from the one set now.
void uiPhoneInit(TFT_eSPI& t);
// A message, starting from `text` (nullptr or "" for a blank one).
void uiPhoneInitMessage(TFT_eSPI& t, const char* text);
void uiPhoneTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng);

// All three edges of a touch, because the QWERTY board types on the release:
// the press previews a key, a slide follows the finger, and the release types
// whatever was last previewed. The keypad acts on the press and ignores the
// other two.
enum class PhoneTouch : uint8_t { DOWN, MOVE, UP };
void uiPhoneTouch(int x, int y, uint32_t now, PhoneTouch phase);

bool uiPhoneDone();
bool uiPhoneMessageMode();
// What was typed, if a message ended on OK; nullptr if it ended on BACK.
const char* uiPhoneMessage();
#endif
