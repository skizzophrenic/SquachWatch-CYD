// SquachWatch-CYD — Squachy, the main character
// A small animated mascot with a speech bubble, drawn on the idle
// (CLEAR) screen. Reacts to detections/buttons/rotation, and cracks
// jokes on his own when nothing else is happening.
#pragma once
#include <TFT_eSPI.h>
#include "state.h"

namespace Squachy {
    enum class Event {
        DETECTION,    // pass the DetectionType that triggered it
        LOG_OPENED,
        LOG_CLEARED,
        ROTATED,
        BOOTED,
        PETTED,       // a quick tap on him — see hitTest()
        HELD,         // a stationary press-and-hold on him past a threshold
        PETTING,      // a dragging/stroking touch on him -- fires repeatedly
                      // (throttled) for as long as the stroke continues, not
                      // once per gesture like PETTED/HELD
    };

    // Call once from an event site (main.cpp) to make Squachy react.
    // lifetimeTotal (DETECTION only) is the engine's persisted
    // across-reboot detection count — used to fire milestone quips.
    // hitCount (DETECTION only) is that specific MAC+type's own total
    // hit count from its log entry — a device that's matched several
    // times is a real pattern (something following you, not a one-off
    // ping), so a high count gets its own "seen you before" reaction
    // instead of the normal fresh-detection line.
    void trigger(Event evt, DetectionType dt = DetectionType::UNKNOWN,
                 uint32_t lifetimeTotal = 0, uint32_t hitCount = 1);

    // Hit test against wherever he was actually drawn last tick() call
    // (position/scale tracked internally) — call this before falling
    // through to any other touch handling on the CLEAR screen, so a
    // tap on him pets him instead of doing nothing.
    bool hitTest(int x, int y);

    // Re-runs the first-boot walkthrough on demand (wired to Settings'
    // "REPLAY INTRO" row). trigger(Event::BOOTED) also starts it
    // automatically the very first time the device ever boots — this
    // is only for replaying it later.
    void replayIntro();

    // True while the walkthrough above is in progress. It runs on top
    // of the normal CLEAR screen — pet-tap, the background-cycle-on-
    // tap, and the button bar all keep working exactly as usual
    // throughout, so the caller doesn't need to change any of its own
    // touch handling except for the one addition below.
    bool onboardingActive();

    // Call this first, before any other CLEAR-screen touch handling,
    // whenever onboardingActive() is true. If the tap landed on the
    // walkthrough's speech bubble, advances (or, on the last step,
    // ends) it and returns true — the caller should treat the tap as
    // consumed. Returns false for a tap anywhere else, so the caller's
    // normal hit-testing (pet, buttons, etc.) still runs untouched.
    bool onboardingTapAdvance(int x, int y);

    // ---- Companion stats -----------------------------------------
    // All persisted, all derived from data already tracked elsewhere
    // (or trivially added) — no wall-clock/RTC dependency, since this
    // device doesn't have one. Shown on the Diary screen and worked
    // into idle chatter every so often.
    uint32_t petCount();
    uint32_t bootCount();
    uint32_t bestClearStreakMs();     // longest-ever gap between detections
    uint32_t currentClearStreakMs();  // the one happening right now
    uint32_t bestSessionCount();      // most detections seen in one boot
    DetectionType firstDetectionType(); // UNKNOWN if nothing's been caught yet

    // ---- Cosmetics --------------------------------------------------
    // Both persisted and both wired to Settings rows ("NICKNAME",
    // "SHADES COLOR") that just cycle through a short curated list —
    // free-text entry isn't worth the keyboard UI it'd need. Shade
    // options unlock progressively with pet count; cycling only ever
    // lands on ones already unlocked.
    const char* nickname();
    void        cycleNickname();
    const char* shadesColorName();
    void        cycleShadesColor();

    // Outfits: NONE (no costume), RACCOON and UNICORN are free/always
    // unlocked; the rest unlock progressively with lifetime detection
    // count (the same stat his growth stages use), in ascending
    // threshold order, so unlockedOutfitCount() is just "how many from
    // the front of the list qualify". cycleOutfit() only ever lands on
    // an unlocked one, same pattern as cycleShadesColor(). outfitCount()
    // is the total including locked ones, for a "N/total" readout.
    const char* outfitName();
    void        cycleOutfit();
    void        cyclePrevOutfit();
    uint8_t     unlockedOutfitCount();
    uint8_t     outfitCount();

    // Hidden unlock-everything trigger: main.cpp watches for a button
    // sequence (9x CLR, 1x SCAN, 1x CLR) and calls this when it
    // completes. Persists immediately, same as any other cosmetic.
    void unlockAllOutfits();

    // Draws Squachy and his speech bubble, and advances his idle
    // animation/quip timers. Call every tick from the CLEAR screen.
    // cx = horizontal center. topY = where the bubble row starts (just
    // below the title bar). availHeight = total vertical room from topY
    // down to wherever the caller's next element starts (e.g. the status
    // line) — Squachy scales himself up to fill it, so give him
    // everything that isn't needed for something else.
    // advance: true = normal single-pass rendering (default, unchanged
    // behavior). Boards that render in multiple physical bands per
    // logical frame (no room for a full-screen sprite buffer) call
    // tick() once per band with the SAME `now`, but must only pass
    // true on exactly one of those calls -- state mutation (mood
    // changes, idle-quip rolls, walk/confetti updates, the bubble's
    // erase-tracking) only happens when advance is true; the actual
    // pixel drawing runs every call so each band still gets painted.
    void tick(TFT_eSPI& t, int cx, int topY, int availHeight, uint32_t now, bool advance = true);

    // A lightweight cameo draw for screens that just want him standing
    // and waving (the boot splash) without the full idle/quip state
    // machine tick() drives. cx = horizontal center, baseY = where his
    // feet line up (e.g. just past the boot screen's horizon), scale
    // sizes him relative to his normal full size (1.0). line, if given,
    // shows in a static speech bubble above his head.
    void drawWaving(TFT_eSPI& t, int cx, int baseY, uint32_t now, float scale = 1.0f,
                    const char* line = nullptr);
}
