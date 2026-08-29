// SquachWatch-CYD — boot splash implementation
#include "ui_boot.h"
#include "theme.h"
#include "squachy.h"

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

    // Full vaporwave sunset backdrop — sky, sun, seagulls, floor — redrawn
    // from scratch every frame (cheap enough at boot-screen size, and
    // simpler than dirty-rect tracking for a screen that's only up for
    // 3s). Text below is drawn with a transparent background so the
    // scene shows through around every glyph instead of solid boxes.
    Theme::drawSunsetSky(t, now, 0, yHoriz);
    Theme::drawSunsetSun(t, w / 2, yHoriz - 25, 30, 0, yHoriz);
    Theme::drawSeagulls(t, now, 0, yHoriz);
    Theme::drawRetroFloor(t, now, yHoriz, h);

    // Big title, Bangers comic-impact font — landscape-only screen
    // (boot always starts at rotation 1) so the width is never tight.
    const char* title = "SQUACHWATCH";
    int tw = Theme::bangersTextWidth(title, Theme::BangersSize::LG);
    Theme::drawBangersText(t, (w - tw) / 2, 8, title, Theme::VAPOR_PINK, Theme::BangersSize::LG);

    // gradient line — moved up right under the title (the v1.0 line
    // used to sit here; removed so Squachy below gets that room too).
    for (int x = 0; x < w; x++) {
        t.drawFastHLine(x, 46, 1, Theme::titlebarColor(x, w));
    }

    // TALKING SASQUACH subtitle — the brand line under the product
    // name, not just the product name again.
    t.setTextSize(2);
    t.setTextColor(Theme::VAPOR_PURPLE);
    const char* sub = "TALKING SASQUACH";
    int sw = t.textWidth(sub);
    t.setCursor((w - sw) / 2, 54);
    t.print(sub);

    // Squachy himself, standing on the floor just past the horizon,
    // giving a friendly wave (and a little on-brand attitude) while
    // everything spins up. Sized as big as the gap between the
    // subtitle and INITIALIZING... below allows — dropping the v1.0
    // line and tightening the title stack above is what buys him the
    // extra room to be this big.
    Squachy::drawWaving(t, w / 2, yHoriz + 50, now, 1.6f, BOOT_LINES[s_bootLineIdx]);

    // INITIALIZING...
    t.setTextSize(1);
    t.setTextColor(Theme::CYAN);
    const char* init = "INITIALIZING...";
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
