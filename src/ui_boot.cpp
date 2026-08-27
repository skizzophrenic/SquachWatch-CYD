// SquachWatch-CYD — boot splash implementation
#include "ui_boot.h"
#include "theme.h"

void uiBootInit(TFT_eSPI& t) {
    // fillScreen() relies on TFT_eSPI's base-class width/height, which
    // TFT_eSprite::createSprite() never updates — it leaves stale
    // remnants when t is a sprite. fillRect() with the (virtual,
    // correctly-overridden) width()/height() clears the whole thing.
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
}

void uiBootTick(TFT_eSPI& t, uint32_t now) {
    int w = t.width();
    int h = t.height();
    int yHoriz = (h * 5) / 8;

    // Full vaporwave sunset backdrop — sky, sun, seagulls, floor — redrawn
    // from scratch every frame (cheap enough at boot-screen size, and
    // simpler than dirty-rect tracking for a screen that's only up for
    // 1.5s). Text below is drawn with a transparent background so the
    // scene shows through around every glyph instead of solid boxes.
    Theme::drawSunsetSky(t, now, 0, yHoriz);
    Theme::drawSunsetSun(t, w / 2, yHoriz - 25, 30, 0, yHoriz);
    Theme::drawSeagulls(t, now, 0, yHoriz);
    Theme::drawRetroFloor(t, now, yHoriz, h);

    // Big title (Font4)
    t.setTextSize(4);
    t.setTextColor(Theme::VAPOR_PINK);
    const char* title = "SQUACHWATCH";
    int tw = t.textWidth(title);
    t.setCursor((w - tw) / 2, 10);
    t.print(title);

    // v1.0
    t.setTextSize(2);
    t.setTextColor(Theme::CYAN);
    const char* v = "v1.0";
    int vw = t.textWidth(v);
    t.setCursor((w - vw) / 2, 55);
    t.print(v);

    // gradient line
    for (int x = 0; x < w; x++) {
        t.drawFastHLine(x, 84, 1, Theme::titlebarColor(x, w));
    }

    // SQUACH WATCH subtitle
    t.setTextSize(2);
    t.setTextColor(Theme::VAPOR_PURPLE);
    const char* sub = "SQUACH WATCH";
    int sw = t.textWidth(sub);
    t.setCursor((w - sw) / 2, 100);
    t.print(sub);

    // Sasquatch silhouette (the cryptid, NOT the brand), standing on
    // the floor just past the horizon.
    Theme::drawSasquatchSilhouette(t, w / 2, yHoriz + 45);

    // INITIALIZING...
    t.setTextSize(1);
    t.setTextColor(Theme::CYAN);
    const char* init = "INITIALIZING...";
    int iw = t.textWidth(init);
    t.setCursor((w - iw) / 2, h - 25);
    t.print(init);

    // animated scanline sweeping top to bottom every 600 ms
    int phase = (int)((now / 600) % (uint32_t)h);
    Theme::drawScanline(t, phase, Theme::VAPOR_PURPLE);
}

bool uiBootDone(uint32_t startMs) {
    return (millis() - startMs) >= 1500;
}
