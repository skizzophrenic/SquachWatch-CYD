// SquachWatch-CM0 -- test-only seam for audio_test.
//
// settings.o calls Theme::applyPalette() on every Settings::load(); the
// only undefined reference it leaves after the sound objects are linked
// is that one. Linking the real theme.o would drag the whole render
// pipeline (Squachy, Security, DetectionEngine) into a test that never
// draws, so this no-op is exactly right here: the test asserts on
// Sound state and bytes, never on colours.
#include "theme.h"

void Theme::applyPalette(uint8_t) {
}
