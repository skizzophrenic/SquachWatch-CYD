// SquachWatch-CYD — outfit picker screen implementation
#include "ui_outfit.h"
#include "theme.h"
#include "squachy.h"
#include "detection.h"
#include <Arduino.h>

// Footer band height (name + unlock count + arrows), reserved at the
// bottom of the screen; Squachy gets everything above it down to the
// title bar, same idea as the CLEAR screen reserving room for its
// counter row.
static const int FOOTER_H    = 44;
static const int ARROW_ZONE_W = 50;

void uiOutfitInit(TFT_eSPI& t) {
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
}

void uiOutfitTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng) {
    int w = t.width();
    int h = t.height();

    const int titleBottom = 16;
    const int footerTop   = h - FOOTER_H;

    // The player's own background, running live behind the preview. The
    // footer is opaque and drawn after, so the band stops at footerTop --
    // and the floor is published so anything that stands on the ground
    // (Mowin' Man) uses that rather than the bottom of the screen.
    t.fillRect(0, footerTop, w, h - footerTop, Theme::BG);
    Theme::setBackgroundFloor(footerTop);
    Theme::drawActiveBackground(t, now, titleBottom, footerTop, eng);
    Theme::clearBackgroundFloor();

    // Live preview -- same call the CLEAR screen makes, just with no
    // background animation and a footer reserved for the name/arrows
    // instead of the counter row.
    Squachy::tick(t, w / 2, titleBottom, footerTop - titleBottom, now, true);

    // Title bar drawn after him, not before: a big bounce can push his
    // dirty-rect clear a few px above titleBottom into the title bar's
    // row, and this fully repaints its own row every frame so it
    // always ends up on top (same reasoning as the CLEAR screen).
    Theme::drawTitleBar(t, ">> OUTFIT <<");

    const char* name = Squachy::outfitName();
    t.setTextSize(2);
    t.setTextColor(Theme::WHITE, Theme::BG);
    int nw = t.textWidth(name);
    t.setCursor((w - nw) / 2, footerTop + 2);
    t.print(name);

    char buf[24];
    snprintf(buf, sizeof(buf), "%u / %u unlocked",
             (unsigned)Squachy::unlockedOutfitCount(), (unsigned)Squachy::outfitCount());
    t.setTextSize(1);
    t.setTextColor(Theme::CYAN, Theme::BG);
    int bw = t.textWidth(buf);
    t.setCursor((w - bw) / 2, footerTop + 22);
    t.print(buf);

    int ay = footerTop + 12;
    t.fillTriangle(24, ay, 10, ay + 10, 24, ay + 20, Theme::VAPOR_PINK);
    t.fillTriangle(w - 24, ay, w - 10, ay + 10, w - 24, ay + 20, Theme::CYAN);
}

bool uiOutfitTapArrow(int x, int y, int screenW, int screenH) {
    int footerTop = screenH - FOOTER_H;
    if (y < footerTop) return false;
    if (x < ARROW_ZONE_W) { Squachy::cyclePrevOutfit(); return true; }
    if (x > screenW - ARROW_ZONE_W) { Squachy::cycleOutfit(); return true; }
    return false;
}
