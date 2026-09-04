// SquachWatch-CYD — boot splash implementation
#include "ui_boot.h"
#include "theme.h"
#include "squachy.h"
#include "settings.h"

// Stamped in by extra_script.py from `git describe` at build time --
// same macro the Diary screen already reads (see its own guard
// comment). Falls back to "unknown" so this still compiles standalone
// (the PC emulator, an IDE's syntax pass) without the build flag.
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "unknown"
#endif

// A dozen one-liners for Squachy's boot-splash speech bubble — one is
// picked at random each boot (see uiBootInit) so the splash doesn't
// say the exact same thing every single time.
static const char* BOOT_LINES[] = {
    "Surveillance state? Not on my watch.",
    "Smile! I'm watching the watchers.",
    "No cameras were harmed. Yet.",
    "Privacy is dead. I'm the eulogy.",
    "Big Brother's ugly cousin, actually.",
    "I collect MAC addresses, not friends.",
    "They see everything. I see them too.",
    "Cryptid by trade, snitch by hobby.",
    "Somewhere, a camera just got nervous.",
    "Not paranoid. Just well-informed.",
    "Trust no lens.",
    "Detecting nonsense since day one.",
};
static const uint8_t BOOT_LINE_COUNT = sizeof(BOOT_LINES) / sizeof(BOOT_LINES[0]);
static uint8_t s_bootLineIdx = 0;

void uiBootInit(TFT_eSPI& t) {
    // fillScreen() relies on TFT_eSPI's base-class width/height, which
    // TFT_eSprite::createSprite() never updates — it leaves stale
    // remnants when t is a sprite. fillRect() with the (virtual,
    // correctly-overridden) width()/height() clears the whole thing.
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
    s_bootLineIdx = (uint8_t)random(0, BOOT_LINE_COUNT);
}

void uiBootTick(TFT_eSPI& t, uint32_t now) {
    int w = t.width();
    int h = t.height();
    int yHoriz = (h * 5) / 8;

    // The same composed scene the SYNTHWAVE background uses -- sky,
    // sun, ridgeline, reflective water and rungs -- rather than the
    // four separate calls this used to make. That version predated the
    // reflection, so the splash was showing a strictly worse sunset
    // than the background did; this keeps the two from drifting apart
    // again. Gulls stay layered on top, since they belong to the splash
    // and not to the background. Redrawn from scratch every frame
    // (cheap at boot-screen size, and simpler than dirty-rect tracking
    // for a screen that is only up for 3s). Text below is drawn with a
    // transparent background so the scene shows through around every
    // glyph instead of sitting in solid boxes.
    Theme::drawSynthwave(t, now, 0, h, 5.0f / 8.0f);   // same waterline this screen always had
    Theme::drawSeagulls(t, now, 0, yHoriz);

    // Big title, Bangers comic-impact font — landscape-only screen
    // (boot always starts at rotation 1) so the width is never tight.
    const char* title = "SQUACHWATCH";
    int tw = Theme::bangersTextWidth(title, Theme::BangersSize::LG);
    int tx = (w - tw) / 2;
    // 3px black outline. The title sits over a bright sunset now, and
    // pink-on-orange had almost no separation where the sun passed
    // behind it. Drawn as a dilation -- the same glyphs at every offset
    // inside a radius-3 disc -- rather than a rectangular halo, so the
    // stroke follows the letterforms instead of boxing them.
    for (int dy = -3; dy <= 3; dy++) {
        for (int dx = -3; dx <= 3; dx++) {
            if (dx * dx + dy * dy > 9 || (dx == 0 && dy == 0)) continue;
            Theme::drawBangersText(t, tx + dx, 8 + dy, title, Theme::BLACK, Theme::BangersSize::LG);
        }
    }
    Theme::drawBangersText(t, tx, 8, title, Theme::VAPOR_PINK, Theme::BangersSize::LG);

    // gradient line — moved up right under the title (the v1.0 line
    // used to sit here; removed so Squachy below gets that room too).
    for (int x = 0; x < w; x++) {
        t.drawFastHLine(x, 46, 1, Theme::titlebarColor(x, w));
    }

    // TALKING SASQUACH subtitle — the brand line under the product
    // name, not just the product name again.
    //
    // Black outlined, same as the title above and for the same reason:
    // it sits over the sunset, and purple against the sun's oranges has
    // almost no separation where the two overlap.
    //
    // One pixel, not the title's three. This is the built-in font at
    // size 2, so the strokes are already only two pixels wide -- a
    // heavier outline competes with the letterform instead of just
    // separating it from the background, and the counters in A, G and Q
    // start filling in.
    //
    // All eight neighbours, so the stroke closes on the diagonals too;
    // the four-way cross alternative leaves visible gaps at every corner
    // of a glyph. The single-argument setTextColor leaves the background
    // transparent, which is what lets the copies build a stroke instead
    // of each one painting a box over the last.
    t.setTextSize(2);
    const char* sub = "TALKING SASQUACH";
    int sw = t.textWidth(sub);
    const int sx = (w - sw) / 2;
    t.setTextColor(Theme::BLACK);
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            if (dx == 0 && dy == 0) continue;
            t.setCursor(sx + dx, 54 + dy);
            t.print(sub);
        }
    }
    t.setTextColor(Theme::VAPOR_PURPLE);
    t.setCursor(sx, 54);
    t.print(sub);

    // Squachy himself, standing on the floor just past the horizon,
    // giving a friendly wave (and a little on-brand attitude) while
    // everything spins up. Sized as big as the gap between the
    // subtitle and INITIALIZING... below allows — dropping the v1.0
    // line and tightening the title stack above is what buys him the
    // extra room to be this big. Skipped in "boring mode" — the rest
    // of the boot splash (wordmark, subtitle, INITIALIZING...) is
    // unaffected, this only cuts the mascot cameo.
    if (!Settings::boringMode()) {
        Squachy::drawWaving(t, w / 2, yHoriz + 50, now, 1.6f, BOOT_LINES[s_bootLineIdx]);
    }

    // INITIALIZING...  vX.Y.Z -- version tacked onto this line rather
    // than given its own row. Everything from the subtitle down to
    // here is already tightly packed around Squachy's cameo (see its
    // own comment above about the room dropping the old standalone
    // version line bought him), and this is the one line on the splash
    // that already reads as status text, not brand/character content,
    // so it's the natural place for a version stamp without touching
    // his space.
    t.setTextSize(1);
    t.setTextColor(Theme::CYAN);
    char init[56];   // room for a full "git describe --dirty" string, not just a bare tag
    snprintf(init, sizeof(init), "INITIALIZING...  %s", FIRMWARE_VERSION);
    int iw = t.textWidth(init);
    t.setCursor((w - iw) / 2, h - 16);
    t.print(init);

    // animated scanline sweeping top to bottom every 600 ms
    int phase = (int)((now / 600) % (uint32_t)h);
    Theme::drawScanline(t, phase, Theme::VAPOR_PURPLE);
}

bool uiBootDone(uint32_t startMs) {
    return (millis() - startMs) >= 3000;
}
