// SquachWatch-CM0 -- the CM0 half of the audio path: ALSA on the
// ES8389 card (i2s_alsa.cpp implements the ESP32 I2S API it is called
// through, sys_shim/driver/i2s.h).
//
//   SQUACH_AUDIO_DEV  the playback PCM; default plughw:ES8389Audio
//                     (tests use "null", which plays into the void)
//   SQUACH_AUDIO_CTL  the mixer card name; default ES8389Audio
//   (both ignored when AT_SECURE, i.e. the installed binary with its caps)
#pragma once
#include <cstdint>

namespace cm0 {

// A transport mute: while set, the player keeps consuming frames (the
// stream's timing does not drift) but writes zeros. Nothing in the app
// sets it any more -- the backlight auto-dim used to, which silenced every
// alert on an idle board; alerts now sound while dimmed and the firmware's
// own play() rules (SOUND mode, night, its POWER SAVER hush) decide.
// Kept for audio_test.
void audioMute(bool on);

// Keep the ES8389 powered between sounds. Cold, the codec takes the first
// part of a sound to come up, and the firmware's alerts are short (NEW_DEVICE
// is 0.25 s of 45-60 ms blips): from cold they were inaudible, while with the
// codec held up the very same sound was heard (2026-11-20, by ear). A lead-in
// of silence did not help, even 2 s of it. What does: a capture stream held
// open on the card -- the microphone, read and thrown away, never stored.
// The harness holds it only while the SOUND setting is ALERTS or ALL (user
// decision); OFF lets the codec sleep. Idempotent; logs each change.
void audioKeepAwake(bool on);

// Frames that actually went to the transport (muted writes are not
// counted). A test hook; the sound task is the only writer.
uint64_t audioFramesWritten();

// The card's mixer, by control name (as amixer lists them). audioMixerSwitch
// is the on/off controls, audioMixerVolume a percent of the control's max.
// false on a missing control -- the ES8389's DTS exposes a few more or
// fewer than the bench's, so callers must not assume.
bool audioMixerSwitch(const char* name, bool on);
bool audioMixerVolume(const char* name, int percent);

}   // namespace cm0
