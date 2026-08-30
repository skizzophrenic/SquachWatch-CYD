// SquachWatch-CYD — clear (idle) screen implementation
#include "ui_clear.h"
#include "theme.h"
#include "squachy.h"
#include "settings.h"
#include <Arduino.h>

void uiClearInit(TFT_eSPI& t) {
    // fillScreen() relies on TFT_eSPI's base-class width/height, which
    // TFT_eSprite::createSprite() never updates — it leaves stale
    // remnants of whatever screen was drawn before when t is a sprite.
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
}

static const char* counterLabel(DetectionType t) {
    switch (t) {
        case DetectionType::FLOCK:       return "FLOCK";
        case DetectionType::AXON:        return "AXON";
        case DetectionType::META:        return "META";
        case DetectionType::SKIMMER:     return "SKIM";
        case DetectionType::AIRTAG:      return "AIR";
        case DetectionType::DRONE:       return "DRONE";
        case DetectionType::RAVEN:       return "RAV";
        case DetectionType::ALPR:        return "ALPR";
        case DetectionType::CAMERA:      return "CAM";
        case DetectionType::SAMSUNG_TAG: return "STAG";
        case DetectionType::GOOGLE_TAG:  return "GTAG";
        case DetectionType::TILE:        return "TILE";
        case DetectionType::RING:        return "RING";
        default:                         return "?";
    }
}

static void drawCounterLine(TFT_eSPI& t, int w, int y, const DetectionEngine& eng,
                            const DetectionType* types, uint8_t n) {
    // 80, not 56: worst case is 7 entries x up to "XXXXX:999  " (11
    // chars) = 77 -- the old 56-byte buffer was already marginal for
    // 6 entries at high counts and would silently truncate (snprintf
    // is bounds-safe, just visually cuts off) once TILE/RING pushed a
    // line to 7.
    char buf[80] = "";
    int off = 0;
    for (uint8_t i = 0; i < n; i++) {
        off += snprintf(buf + off, sizeof(buf) - off, "%s:%u  ",
                        counterLabel(types[i]), eng.countByType(types[i]));
    }
    int tw = t.textWidth(buf);
    t.setCursor((w - tw) / 2, y);
    t.print(buf);
}

void uiClearTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng, bool advance) {
    int w = t.width();

    Theme::ButtonBarGeom bar = Theme::computeButtonBar(w, t.height());
    const int lineH          = 14;
    const int countersTop    = bar.y - 2 * lineH - 6;
    const int countersBottom = bar.y - 4;

    // statusH: height of the ALL CLEAR / DETECTIONS LOGGED text row,
    // sized to fit the Bangers MD font's glyph box (ascent 27 +
    // descent 6, same font as the ALERT screen's "!! DETECTION !!").
    // Squachy's own region now runs past this (see his tick() call
    // below) so his feet land on top of it instead of stopping above.
    const int titleBottom  = 16;
    const int statusH      = 34;
    // The animation runs all the way down to just above the button
    // bar, covering Squachy's region, the text row, and the counters —
    // everything below the title bar erases and repaints together
    // every frame.
    const int rainEnd      = countersBottom;

    // Background animation, from below the title bar down to just
    // above the button bar — style picked from the settings menu.
    switch (Settings::background()) {
        case Settings::Background::STARFIELD: Theme::drawStarfield(t, now, titleBottom, rainEnd); break;
        case Settings::Background::TOASTERS:   Theme::drawFlyingToasters(t, now, titleBottom, rainEnd); break;
        case Settings::Background::LAVALAMP:   Theme::drawLavaLamp(t, now, titleBottom, rainEnd); break;
        case Settings::Background::CRYPTID:    Theme::drawCryptidCam(t, now, titleBottom, rainEnd); break;
        case Settings::Background::RADAR:      Theme::drawRadarSweep(t, now, titleBottom, rainEnd); break;
        case Settings::Background::RAIN:       Theme::drawRainGlass(t, now, titleBottom, rainEnd); break;
        case Settings::Background::AQUARIUM:   Theme::drawAquarium(t, now, titleBottom, rainEnd); break;
        case Settings::Background::TERMINAL:   Theme::drawTerminalLog(t, now, titleBottom, rainEnd); break;
        case Settings::Background::FIREFLIES:  Theme::drawFireflies(t, now, titleBottom, rainEnd); break;
        case Settings::Background::AURORA:     Theme::drawAurora(t, now, titleBottom, rainEnd); break;
        case Settings::Background::FIRE:       Theme::drawFire(t, now, titleBottom, rainEnd); break;
        case Settings::Background::SNOWFALL:   Theme::drawSnowfall(t, now, titleBottom, rainEnd); break;
        case Settings::Background::SPECTRUM:   Theme::drawSpectrumWaterfall(t, now, titleBottom, rainEnd, eng); break;
        case Settings::Background::TUNNEL:     Theme::drawWireframeTunnel(t, now, titleBottom, rainEnd); break;
        default:                               Theme::drawMatrixRain(t, now, titleBottom, rainEnd, advance); break;
    }

    // Squachy: main character, reacts to events, cracks jokes when idle.
    // His available region runs all the way to countersTop (not
    // statusTop) — past where the ALL CLEAR text sits — so he scales up
    // further and his feet land on top of its upper portion. The text
    // itself draws AFTER him (below) so it stays fully legible on top
    // of his body wherever they overlap, instead of being covered. A
    // big bounce can push his dirty-rect clear a few px above
    // titleBottom into the title bar's row, so the title bar is drawn
    // after him too — it fully repaints its own row every frame, so it
    // always ends up on top and never shows any bleed-over from his
    // clear box.
    //
    // "Boring mode" skips this call entirely — his internal state
    // (mood/quip timers, the stats Squachy::trigger() tracks for the
    // Diary screen) keeps updating regardless since that's driven from
    // main.cpp's trigger() calls, not from here; this only turns off
    // his actual on-screen presence. The background above already
    // fully repaints this whole region every frame, so skipping him
    // just leaves it as animated negative space — no layout changes
    // needed anywhere else on this screen.
    if (!Settings::boringMode()) {
        Squachy::tick(t, w / 2, titleBottom, countersTop - titleBottom, now, advance);
    }

    // Title bar at the top
    Theme::drawTitleBar(t, ">> SQUACHWATCH <<  SCANNING");

    // ALL CLEAR (only flash if there are NO active detections). Same
    // Bangers headline font as the ALERT screen's "!! DETECTION !!" —
    // sits right above the counter lines, bottom-aligned to
    // countersTop (see statusH above). Drawn AFTER Squachy so it stays
    // readable on top of him rather than getting covered by his feet.
    bool anyActive = false;
    for (uint8_t i = 0; i < (uint8_t)DetectionType::COUNT; i++) {
        if (eng.countByType((DetectionType)i) > 0) { anyActive = true; break; }
    }
    {
        uint16_t col;
        const char* msg;
        if (!anyActive) {
            // Full rainbow cycle instead of a two-color pulse — same
            // hue-wash technique as Squachy's party-mode confetti wash.
            static const uint16_t RAINBOW[6] = {
                Theme::RED, Theme::AMBER, Theme::GREEN,
                Theme::CYAN, Theme::VAPOR_PURPLE, Theme::PINK
            };
            float huePos = fmodf((float)now / 900.0f, 6.0f);
            int i0 = (int)huePos % 6, i1 = (i0 + 1) % 6;
            col = Theme::blend(RAINBOW[i0], RAINBOW[i1], (uint16_t)((huePos - (int)huePos) * 255));
            msg = "ALL CLEAR";
        } else {
            col = Theme::PINK;
            msg = "DETECTIONS LOGGED";
        }
        // 2px black outline: draw the same text at every offset in a
        // 5x5 grid around the real position (minus the center) in
        // black first, then the real color on top. A full grid, not
        // just a ring at radius 2, so there's no gap between the 1px
        // and 2px shells. The Bangers glyph renderer only paints ink
        // pixels (not a full opaque cell), so the offset passes land
        // as a clean outline rather than clobbering each other.
        static const int8_t OUTLINE_OFS[24][2] = {
            {-2,-2},{-1,-2},{0,-2},{1,-2},{2,-2},
            {-2,-1},{-1,-1},{0,-1},{1,-1},{2,-1},
            {-2, 0},{-1, 0},        {1, 0},{2, 0},
            {-2, 1},{-1, 1},{0, 1},{1, 1},{2, 1},
            {-2, 2},{-1, 2},{0, 2},{1, 2},{2, 2},
        };
        int tw = Theme::bangersTextWidth(msg, Theme::BangersSize::MD);
        int ty = countersTop - statusH;
        if (tw <= w - 8) {
            int tx = (w - tw) / 2;
            for (uint8_t i = 0; i < 24; i++) {
                Theme::drawBangersText(t, tx + OUTLINE_OFS[i][0], ty + OUTLINE_OFS[i][1],
                                        msg, Theme::BLACK, Theme::BangersSize::MD);
            }
            Theme::drawBangersText(t, tx, ty, msg, col, Theme::BangersSize::MD);
        } else {
            // "DETECTIONS LOGGED" is long enough to overflow the
            // narrowest (240px portrait) rotation at this font's fixed
            // size — Bangers has no smaller step to fall back to like
            // the built-in font does, so drop to that instead rather
            // than clip.
            t.setTextSize(2);
            int sw = t.textWidth(msg);
            int sx = (w - sw) / 2, sy = countersTop - t.fontHeight(2);
            t.setTextColor(Theme::BLACK, Theme::BG);
            for (uint8_t i = 0; i < 24; i++) {
                t.setCursor(sx + OUTLINE_OFS[i][0], sy + OUTLINE_OFS[i][1]);
                t.print(msg);
            }
            t.setTextColor(col, Theme::BG);
            t.setCursor(sx, sy);
            t.print(msg);
        }
    }

    // Counter lines above the buttons — a fixed, centered two-line
    // split across all 11 detection types. Whole label:count tokens
    // only, so nothing ever breaks mid-word. No flat clear here
    // anymore — the background animation now fully repaints this
    // whole row every frame (rainEnd extends down to countersBottom),
    // the same "let the background do the erasing" pattern already
    // relied on for Squachy and the status line above.
    t.setTextSize(1);
    t.setTextColor(Theme::CYAN, Theme::BG);
    t.setTextWrap(false);

    static const DetectionType LINE1[] = {
        DetectionType::FLOCK, DetectionType::AXON, DetectionType::META,
        DetectionType::SKIMMER, DetectionType::RAVEN, DetectionType::AIRTAG,
        DetectionType::TILE
    };
    static const DetectionType LINE2[] = {
        DetectionType::DRONE, DetectionType::ALPR, DetectionType::CAMERA,
        DetectionType::SAMSUNG_TAG, DetectionType::GOOGLE_TAG, DetectionType::RING
    };

    drawCounterLine(t, w, countersTop,              eng, LINE1, 7);
    drawCounterLine(t, w, countersTop + lineH,       eng, LINE2, 6);

    // Soft buttons
    Theme::drawButtonBar(t, ButtonId::NONE);
}
