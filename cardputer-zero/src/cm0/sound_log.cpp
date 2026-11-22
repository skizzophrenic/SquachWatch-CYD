// SquachWatch-CM0 -- one log line per sound request.
//
// The firmware's Sound::play() drops a request silently when a gate says
// no (the SOUND mode, night, the hush), and nothing else in the log says
// whether an alert even asked for a sound. On a board where "the alerts
// make no noise" had to be chased, that is the line that answers it. The
// Makefile links squachd with --wrap=_ZN5Sound4playENS_2IdEb, so every
// call from the firmware lands here first and then goes on, unchanged, to
// the real play().
//
// The verdict printed is the firmware's own rule, read from the same
// settings play() reads (sound.cpp): OFF plays nothing, ALERTS only the
// four alert sounds, night only WATCH_HIT; a sample (a settings preview)
// skips them all. The hush cannot be read from outside sound.cpp; it is
// only ever set while the firmware's POWER SAVER has the screen dimmed.
#include "sound.h"
#include "settings.h"
#include "clock.h"

#include <cstdio>

void cm0SoundPlay(Sound::Id id, bool sample) asm("__wrap__ZN5Sound4playENS_2IdEb");
void realSoundPlay(Sound::Id id, bool sample) asm("__real__ZN5Sound4playENS_2IdEb");

static const char* idName(Sound::Id id) {
    static const char* const N[] = {
        "NEW_DEVICE", "WATCH_HIT", "LOW_BATTERY", "MESSAGE", "VISITOR", "THROW", "LAND",
        "CLIPPY", "TOAST_POP", "PLUG_IN", "UNPLUG", "LAND_TOASTER", "LAND_YETI", "HEADS_UP",
        "WD_FIX", "WD_LOST", "WD_TICK",
    };
    static_assert(sizeof N / sizeof N[0] == (size_t)Sound::Id::COUNT, "a name per Sound::Id");
    const unsigned i = (unsigned)id;
    return i < (unsigned)Sound::Id::COUNT ? N[i] : "?";
}

void cm0SoundPlay(Sound::Id id, bool sample) {
    // Squachy's own bounces and pops are frequent and not the question:
    // log the alert sounds, and every request that would be dropped.
    const bool alert = id == Sound::Id::NEW_DEVICE || id == Sound::Id::WATCH_HIT ||
                       id == Sound::Id::LOW_BATTERY || id == Sound::Id::HEADS_UP;
    const uint8_t mode = Settings::soundMode();
    const char* why = nullptr;
    if (!Sound::present())                                   why = "no speaker";
    else if (!sample && mode == 0)                           why = "SOUND is OFF";
    else if (!sample && !alert && mode < 2)                  why = "SOUND is ALERTS, not ALL";
    else if (!sample && Clock::night() && id != Sound::Id::WATCH_HIT) why = "night (23:00-05:00)";
    if (alert || why)
        std::fprintf(stderr, "[sound] %s%s: %s\n", idName(id), sample ? " (sample)" : "",
                     why ? why : "plays (unless the POWER SAVER hush is on)");
    realSoundPlay(id, sample);
}
