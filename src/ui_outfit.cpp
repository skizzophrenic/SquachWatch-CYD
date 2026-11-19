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

void uiOutfitTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng, bool advance) {
    int w = t.width();
    int h = t.height();

    const int titleBottom = Theme::LIST_TOP;
    const int footerTop   = h -
#if defined(M5_STOPWATCH)
        166;
#else
        FOOTER_H;
#endif

    // The player's own background, running live behind the preview. The
    // footer is opaque and drawn after, so the band stops at footerTop --
    // and the floor is published so anything that stands on the ground
    // (Mowin' Man) uses that rather than the bottom of the screen.
    t.fillRect(0, footerTop, w, h - footerTop, Theme::BG);
    Theme::setBackgroundFloor(footerTop);
    // The background starts at the TOP OF THE SCREEN, not at the body's own
    // top. Those first sixteen rows used to be the title bar's; nothing owns
    // them now except the two corner buttons, which draw their own opaque
    // boxes over whatever is behind them. Leaving the animation to start
    // below them left a flat dead strip across the top of this screen --
    // the same relic CLEAR had, and the same fix.
    //
    // Only the BACKGROUND moves. Everything else on this screen still
    // begins where it did, so no content shifts.
    const int bgTop = 0;
    Theme::drawActiveBackground(t, now, bgTop, footerTop, eng, advance);
    Theme::clearBackgroundFloor();

    // Live preview -- same call the CLEAR screen makes, just with no
    // background animation and a footer reserved for the name/arrows
    // instead of the counter row.
    Squachy::tick(t, w / 2, titleBottom, footerTop - titleBottom, now, advance);

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
    Theme::bodyFontOn(t);
    t.setTextColor(Theme::CYAN, Theme::BG);
    int bw = t.textWidth(buf);
    t.setCursor((w - bw) / 2, footerTop + 22);
    t.print(buf);
    Theme::bodyFontOff(t);

#if defined(M5_STOPWATCH)
    const auto bar=Theme::computeButtonBar(w,h);
    Theme::drawButton(t,bar.x[0],bar.y,bar.w[0],bar.h,"PREV",false);
    Theme::drawButton(t,bar.x[1],bar.y,bar.w[1],bar.h,"BACK",false);
    Theme::drawButton(t,bar.x[2],bar.y,bar.w[2],bar.h,"NEXT",false);
#else
    int ay = footerTop + 12;
    t.fillTriangle(24, ay, 10, ay + 10, 24, ay + 20, Theme::VAPOR_PINK);
    t.fillTriangle(w - 24, ay, w - 10, ay + 10, w - 24, ay + 20, Theme::CYAN);
#endif
}

bool uiOutfitTapArrow(int x, int y, int screenW, int screenH) {
#if defined(M5_STOPWATCH)
    const ButtonId hit=Theme::hitTestButtonBar(x,y,screenW,screenH);
    if(hit==ButtonId::SCAN) {Squachy::cyclePrevOutfit();return true;}
    if(hit==ButtonId::CLR) {Squachy::cycleOutfit();return true;}
    return false;
#else
    int footerTop = screenH - FOOTER_H;
    if (y < footerTop) return false;
    if (x < ARROW_ZONE_W) { Squachy::cyclePrevOutfit(); return true; }
    if (x > screenW - ARROW_ZONE_W) { Squachy::cycleOutfit(); return true; }
    return false;
#endif
}
