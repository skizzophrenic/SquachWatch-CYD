// SquachWatch-CYD — clear (idle) screen implementation
#include "ui_clear.h"
#include "theme.h"
#include "squachy.h"
#include <Arduino.h>

void uiClearInit(TFT_eSPI& t) {
    // fillScreen() relies on TFT_eSPI's base-class width/height, which
    // TFT_eSprite::createSprite() never updates — it leaves stale
    // remnants of whatever screen was drawn before when t is a sprite.
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
}

static const char* counterLabel(DetectionType t) {
    switch (t) {
        case DetectionType::FLOCK:   return "FLOCK";
        case DetectionType::AXON:    return "AXON";
        case DetectionType::META:    return "META";
        case DetectionType::SKIMMER: return "SKIM";
        case DetectionType::AIRTAG:  return "AIR";
        case DetectionType::DRONE:   return "DRONE";
        case DetectionType::RAVEN:   return "RAV";
        case DetectionType::ALPR:    return "ALPR";
        case DetectionType::CAMERA:  return "CAM";
        default:                     return "?";
    }
}

void uiClearTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng) {
    int w = t.width();

    Theme::ButtonBarGeom bar = Theme::computeButtonBar(w, t.height());
    const int lineH          = 14;
    const int countersTop    = bar.y - 2 * lineH - 6;
    const int countersBottom = bar.y - 4;

    // Status line sits directly against the counter lines (no gap) —
    // reserve its max height (the 2x "ALL CLEAR" variant) so Squachy's
    // size stays stable regardless of which status text is showing.
    const int titleBottom  = 16;
    const int statusH      = 16;
    const int statusTop    = countersTop - statusH;
    const int rainEnd      = statusTop - 2;

    // Background matrix rain, from below the title bar down to just
    // above the status line.
    Theme::drawMatrixRain(t, now, titleBottom, rainEnd);

    // Squachy: main character, reacts to events, cracks jokes when idle.
    // He gets everything between the title bar and the status line —
    // as big as the current screen size allows. A big bounce can push
    // his dirty-rect clear a few px above titleBottom into the title
    // bar's row, so the title bar is drawn AFTER him — it fully
    // repaints its own row every frame, so it always ends up on top
    // and never shows any bleed-over from his clear box.
    Squachy::tick(t, w / 2, titleBottom, statusTop - titleBottom, now);

    // Title bar at the top
    Theme::drawTitleBar(t, ">> SQUACHWATCH <<  SCANNING");

    // ALL CLEAR (only flash if there are NO active detections)
    bool anyActive = false;
    for (uint8_t i = 0; i < (uint8_t)DetectionType::COUNT; i++) {
        if (eng.countByType((DetectionType)i) > 0) { anyActive = true; break; }
    }
    // Kept small and bottom-aligned to the counters — Squachy is the
    // main character now, this is just a status line touching his feet.
    if (!anyActive) {
        float pulse = 0.55f + 0.45f * sinf((float)(now % 2000) / 2000.0f * 6.2831853f);
        uint16_t col = (uint16_t)Theme::blend(Theme::GREEN, Theme::CYAN, (uint16_t)(pulse * 200.0f));
        t.setTextSize(2);
        t.setTextColor(col, Theme::BG);
        const char* clear = "ALL CLEAR";
        int cw = t.textWidth(clear);
        t.setCursor((w - cw) / 2, countersTop - t.fontHeight(2));
        t.print(clear);
    } else {
        t.setTextSize(1);
        t.setTextColor(Theme::PINK, Theme::BG);
        const char* seen = "DETECTIONS LOGGED";
        int sw = t.textWidth(seen);
        t.setCursor((w - sw) / 2, countersTop - t.fontHeight(1));
        t.print(seen);
    }

    // Counter lines above the buttons — a fixed, centered two-line
    // split (FLOCK/AXON/META/SKIM/RAV, then AIR/DRONE/ALPR/CAM). Whole
    // label:count tokens only, so nothing ever breaks mid-word.
    t.setTextSize(1);
    t.setTextColor(Theme::CYAN, Theme::BG);
    t.setTextWrap(false);
    t.fillRect(0, countersTop, w, countersBottom - countersTop, Theme::BG);

    static const DetectionType LINE1[] = {
        DetectionType::FLOCK, DetectionType::AXON, DetectionType::META,
        DetectionType::SKIMMER, DetectionType::RAVEN
    };
    static const DetectionType LINE2[] = {
        DetectionType::AIRTAG, DetectionType::DRONE, DetectionType::ALPR,
        DetectionType::CAMERA
    };

    char line1[48] = "";
    char line2[48] = "";
    int off1 = 0, off2 = 0;
    for (uint8_t i = 0; i < 5; i++) {
        off1 += snprintf(line1 + off1, sizeof(line1) - off1, "%s:%u  ",
                         counterLabel(LINE1[i]), eng.countByType(LINE1[i]));
    }
    for (uint8_t i = 0; i < 4; i++) {
        off2 += snprintf(line2 + off2, sizeof(line2) - off2, "%s:%u  ",
                         counterLabel(LINE2[i]), eng.countByType(LINE2[i]));
    }
    int w1 = t.textWidth(line1);
    t.setCursor((w - w1) / 2, countersTop);
    t.print(line1);
    int w2 = t.textWidth(line2);
    t.setCursor((w - w2) / 2, countersTop + lineH);
    t.print(line2);

    // Soft buttons
    Theme::drawButtonBar(t, ButtonId::NONE);
}
