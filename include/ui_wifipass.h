// SquachWatch-CYD — typing a WiFi password on the board.
//
// Its own keyboard rather than the payphone's QWERTY board, because that one
// is uppercase-only with a handful of punctuation keys -- right for a name or
// a message, wrong for a password, which is case-sensitive and usually has
// symbols in it. This one has SHIFT and a symbols page with every printable
// ASCII mark. It types on RELEASE with the same jump filter the payphone
// uses, because a resistive panel's last samples as the finger lifts are junk.
//
// The text is hidden by default (the last character shows briefly) with a
// SHOW toggle, and uiWifiPassClear() wipes the buffer once it has been used.
#pragma once
#include <TFT_eSPI.h>
#include <stdint.h>

enum class WifiPassTouch  : uint8_t { DOWN, MOVE, UP };
enum class WifiPassResult : uint8_t { NONE, OK, BACK };

void           uiWifiPassInit(TFT_eSPI& t, const char* ssid);
// The same keyboard for short text that is not a secret (MY ALERTS' patterns
// and labels): `prompt` replaces "PASSWORD FOR" in the header, `subject` is
// the line beside it, the text shows from the start, it opens holding
// `initial`, takes at most `maxLen` characters, and OK takes an empty field
// when `allowEmpty` (an optional label). uiWifiPassSsid() returns `subject`.
void           uiWifiPassInitText(TFT_eSPI& t, const char* prompt, const char* subject, const char* initial,
                                  uint8_t maxLen, bool allowEmpty);
void           uiWifiPassTick(TFT_eSPI& t, uint32_t now);
// The next tick draws everything, not only what changed. For a caller whose
// buffer does not keep the last frame (the 3.5"'s half-height sprite).
void           uiWifiPassRedrawAll();
void           uiWifiPassTouch(int x, int y, uint32_t now, WifiPassTouch phase);
// A character from a physical keyboard: '\b' deletes, '\n' is OK.
void           uiWifiPassKey(char c, uint32_t now);
bool           uiWifiPassBackRect(int16_t& x, int16_t& y, int16_t& w, int16_t& h);
WifiPassResult uiWifiPassResult();
const char*    uiWifiPassText();
const char*    uiWifiPassSsid();
void           uiWifiPassClear();
