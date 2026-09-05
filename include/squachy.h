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

    // WOLF PELT is the one outfit not earned by a detection count --
    // main.cpp calls this when the werewolf easter egg on the FIRE
    // background is summoned. Persists immediately; a no-op once it has
    // already been earned, so re-summoning does not re-announce it.
    void unlockWolfPelt();

    // CHROME WING is the second outfit not earned by a detection count --
    // main.cpp calls this when the player catches the rare gold toaster on
    // the TOASTERS background. Persists immediately; a no-op once earned.
    void unlockChromeWing();

    // Unlock announcements. Any outfit that becomes available -- by
    // crossing its lifetime-detection threshold, or by the werewolf
    // summon -- is queued once, and main.cpp drains the queue by popping
    // the OUTFIT UNLOCKED screen. Returns false when there is nothing
    // pending. An outfit is marked as announced the moment it is handed
    // out here, so it never repeats across a reboot.
    bool consumeOutfitUnlock(uint8_t& outIdx);

    // Name of an outfit by index, for the popup -- outfitName() only
    // ever reports the one currently worn.
    const char* outfitNameAt(uint8_t idx);

    // Forces the whole draw path to render a given outfit regardless of
    // which one is actually selected, so the unlock popup can show off
    // the new costume without switching the player into it. -1 clears
    // the override. Set it, draw, clear it -- leaving it set would
    // silently take over every other screen.
    void setOutfitPreview(int8_t idx);

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
    // minScale: normally he never renders below scale 1.0 (keeps his
    // proportions legible) -- a call site with a genuinely tiny box
    // (e.g. the raw-scan screen's mini cameo) can lower this floor.
    // Leave it at the default everywhere else; it changes nothing
    // about the CLEAR screen's normal sizing.
    // scanningFx: draws a small radiating "ping" beside his head every
    // call while true -- purely a function of `now`, no state of its
    // own, so the caller can flip it on/off between ticks freely (see
    // ui_rawscan.cpp).
    // wanderRangePx: -1 (default) leaves Mood::WALK's wander and
    // Mood::SHOCKED's panicked jitter at their normal behavior (WALK
    // computes its own range from the full screen width; SHOCKED
    // doesn't move at all). A caller confined to a small box -- ALERT's
    // corner cameo, say -- can pass a small positive px bound instead,
    // which SHOCKED then uses to dart back and forth within that box
    // instead of standing still while he flails. Doesn't affect WALK's
    // own full-screen wander; that's unrelated to this screen's ask.
    void tick(TFT_eSPI& t, int cx, int topY, int availHeight, uint32_t now,
              bool advance = true, float minScale = 1.0f, bool scanningFx = false,
              int wanderRangePx = -1);

    // Themed one-liner reactions for the raw-scan screen (see
    // ui_rawscan.cpp), pulled from their own flavor pool instead of
    // the normal idle-chatter rotation -- call once per actual moment,
    // not every tick. count is the number of results so far/at finish
    // (ignored for STARTED); DONE_FOUND with a high count also
    // triggers his rare party-confetti flourish, the same one
    // milestone detections and the outfit-unlock easter egg use.
    enum class ScanMoment { STARTED, HIT, DONE_EMPTY, DONE_FOUND };
    void scanReaction(ScanMoment moment, uint8_t count = 0);

    // Same pattern as scanReaction(), for HUNT MODE's live gauge (see
    // ui_hunt.cpp) -- call once per actual moment, not every tick.
    // STARTED fires once on entering the screen with a fixed
    // instructional line (same reasoning as ScanMoment::STARTED: the
    // one place someone learns the body-fade technique, so it always
    // says the same thing rather than rolling flavor text that might
    // never mention it). FIRST_SIGNAL fires once the very first RSSI
    // sample for this target comes in. WARMER/COLDER fire only on an
    // actual trend change, not every tick the trend happens to still
    // read that way. HOT fires once when the signal crosses into
    // "basically on top of it" territory. STALLED fires once if a hunt
    // runs long without ever reaching HOT -- pure flavor, no state of
    // its own to track beyond "has this fired yet."
    enum class HuntMoment { STARTED, FIRST_SIGNAL, WARMER, COLDER, HOT, STALLED };
    void huntReaction(HuntMoment moment);

    // Fires once when the watch-alert screen appears (see
    // ui_watchalert.cpp) -- the target you set a WATCH on came back
    // into range. No STARTED-style fixed line here: by the time anyone
    // sees this screen they've already long-pressed a result and hit
    // WATCH, so the mechanic itself was already taught upstream (the
    // raw-scan/LOG screens' own hint lines).
    void watchAlertReaction();

    // A lightweight cameo draw for screens that just want him standing
    // and waving (the boot splash) without the full idle/quip state
    // machine tick() drives. cx = horizontal center, baseY = where his
    // feet line up (e.g. just past the boot screen's horizon), scale
    // sizes him relative to his normal full size (1.0). line, if given,
    // shows in a static speech bubble above his head.
    // talking: forces the open/close mouth-flap animation regardless of
    // whether a real speech bubble is up (default false, unchanged
    // behavior) -- for a caller drawing its own explanation text
    // separately (LOG's MORE INFO panel) rather than through line.
    // wanderRangePx: > 0 makes him patrol back and forth up to that many
    // px either side of cx, forever, at the same pace tick()'s
    // Mood::WALK uses. Default 0 leaves him standing still at cx,
    // unchanged behavior for every existing caller.
    void drawWaving(TFT_eSPI& t, int cx, int baseY, uint32_t now, float scale = 1.0f,
                    const char* line = nullptr, bool talking = false, int wanderRangePx = 0);
}
