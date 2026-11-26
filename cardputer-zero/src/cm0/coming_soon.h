// SquachWatch-CM0 -- features this board does not have yet (coming_soon.cpp).
#pragma once

// The firmware draws toasts on the main screens, never over SETTINGS, so a
// "FEATURE COMING SOON" raised from a settings row would wait until the
// menu was closed. Raising one asks the harness to close the menu first:
// true once per request (main_linux.cpp sends BACK until SETTINGS is gone).
bool cm0TakeLeaveSettings();

// The head of the last toast actually shown (for --selftest).
const char* cm0LastToastHead();
