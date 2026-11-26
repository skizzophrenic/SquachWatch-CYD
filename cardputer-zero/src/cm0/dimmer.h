// SquachWatch-CM0 -- auto-dim while the app is in front (--fb mode).
//
// The device's launcher pauses its own screen-off timer while an app it
// started is in front, so a running SquachWatch would keep the panel at full
// brightness forever. Turning it off would hide what this app is for (it is
// a display of what is around), so -- as ~/OMP/catputer does -- it DIMS:
// after DIM_AFTER_MS with no key, the backlight drops to a low level (never
// 0: 0 is "screen off" to the launcher and to cz-battery-saver); the first
// key brings it back and does nothing else (swallowed until its key-up, so
// waking with ESC neither goes back nor starts the exit). The user's level
// is put back on exit. While the app runs, its own BRIGHTNESS setting
// (dimmerSetAppDuty) is the awake level.
//
// The backlight is /sys/class/backlight/<first>/brightness (root:video 664),
// opened by dimmerOpen() before the UI thread's fs-identity switch.
#pragma once
#include <cstdint>

namespace cm0 {

const uint32_t DIM_AFTER_MS = 60000;

bool dimmerOpen(uint32_t nowMs);   // false: no writable backlight (dimming off)
// The same on a given backlight directory (brightness + max_brightness).
bool dimmerOpenAt(const char* dir, uint32_t nowMs);
void dimmerClose();                // restores the user's level

// A key event. Returns true when it must be swallowed (it woke the panel,
// or it is the rest of the key that did).
bool dimmerKey(uint16_t code, bool down, uint32_t nowMs);
// Activity that is not a key -- the case shaken or knocked (shake_iio.cpp).
// Restarts the idle clock and, when dimmed, brings the level back. Returns
// true when it woke the panel. No key is involved, so nothing is swallowed.
bool dimmerWake(uint32_t nowMs);
// From the loop: dims once DIM_AFTER_MS has passed without a key.
void dimmerTick(uint32_t nowMs);
bool dimmed();

// The firmware's own backlight request, its 0..255 duty: SETTINGS >
// BRIGHTNESS (32..255), or POWER SAVER's dim level while its saver has the
// screen dimmed. Read by the harness from the emulator's ledcWrite channel
// 0 (main.cpp, applyBrightness). Becomes the level shown whenever this
// dimmer is not dimming; exit still puts back the launcher's level.
void dimmerSetAppDuty(int duty);
// duty 0..255 -> a level of max, never 0 (exposed for the test).
int levelForDuty(int duty, int max);

// The dim level for a backlight at `level` of `max` (exposed for the test).
int dimLevelFor(int level, int max);

}   // namespace cm0
