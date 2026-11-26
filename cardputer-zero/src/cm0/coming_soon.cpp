// SquachWatch-CM0 -- settings for features this board does not have yet:
// pressed, they say "FEATURE COMING SOON" (the firmware's own toast) and
// change nothing.
//
// The rows are upstream's (ui_settings.cpp, no visibility hook for these)
// and firmware files are never edited, so the settings calls behind them
// are routed here at link time: squachd links with --wrap for each symbol
// below (Makefile, COMING_SOON_LDFLAGS), so every call from main.cpp,
// ui_settings.cpp and ui_sysprops.cpp lands here instead of settings.cpp.
//
//  EXT SCREEN    a second ILI9341 on the ESP32 Cardputer ADV's EXT header.
//                Switched on here it crashed the app on every launch
//                (main.cpp:6758, pushFrame()'s ESP32 frame hash, SIGSEGV),
//                and the setting is saved -- so it is always OFF, and a
//                saved `extscr 1` from an earlier build is ignored.
//  UPDATE CHECK  "look for updates at boot": there is no updater on this
//                board yet (ota_cm0.cpp), so always OFF.
//
// UPDATE FIRMWARE opens its own screen, which says the same thing on its
// failure page (ota_cm0.cpp).
//
// EXT SCREEN's row (main.cpp) shows a toast of its own right after the
// toggle -- "EXT SCREEN OFF" -- and there is one toast slot, so it would
// replace ours in the same frame (seen on the device). Theme::showToast is
// wrapped too: that one message, and only that one, becomes ours; every
// other toast goes through untouched (__real). And since the firmware never
// draws a toast over SETTINGS, a coming-soon toast also asks the harness to
// close the menu (coming_soon.h), so it shows at once.
#include "coming_soon.h"
#include "settings.h"
#include "theme.h"

#include <cstdio>
#include <cstring>

static const char COMING_SOON[] = "FEATURE COMING SOON";
static const char EXT_WHY[]     = "No EXT screen on this board yet";

void cm0ShowToast(const char* head, const char* sub, uint16_t accent, uint32_t ms)
    asm("__wrap__ZN5Theme9showToastEPKcS1_tj");
void realShowToast(const char* head, const char* sub, uint16_t accent, uint32_t ms)
    asm("__real__ZN5Theme9showToastEPKcS1_tj");

// The head of the last toast actually shown (the selftest's view of it:
// the firmware keeps its toast text to itself).
static char s_lastHead[40] = "";
const char* cm0LastToastHead() { return s_lastHead; }

// Set with every coming-soon toast: the menu it was raised from closes
// first, so the toast is seen at once (coming_soon.h).
static bool s_leave = false;
bool cm0TakeLeaveSettings() { const bool l = s_leave; s_leave = false; return l; }

void cm0ShowToast(const char* head, const char* sub, uint16_t accent, uint32_t ms) {
    if (head && (!strcmp(head, "EXT SCREEN ON") || !strcmp(head, "EXT SCREEN OFF"))) {
        head = COMING_SOON; sub = EXT_WHY; accent = Theme::AMBER; ms = 3000;
    }
    if (head && !strcmp(head, COMING_SOON)) s_leave = true;
    snprintf(s_lastHead, sizeof s_lastHead, "%s", head ? head : "");
    realShowToast(head, sub, accent, ms);
}

static void comingSoon(const char* what) { cm0ShowToast(COMING_SOON, what, Theme::AMBER, 3000); }

bool cm0ExtScreen() asm("__wrap__ZN8Settings9extScreenEv");
void cm0ToggleExtScreen() asm("__wrap__ZN8Settings15toggleExtScreenEv");
bool cm0UpdateCheck() asm("__wrap__ZN8Settings11updateCheckEv");
void cm0ToggleUpdateCheck() asm("__wrap__ZN8Settings17toggleUpdateCheckEv");

bool cm0ExtScreen() { return false; }
void cm0ToggleExtScreen() { comingSoon(EXT_WHY); }
bool cm0UpdateCheck() { return false; }
void cm0ToggleUpdateCheck() { comingSoon("Updates are not on this board yet"); }
