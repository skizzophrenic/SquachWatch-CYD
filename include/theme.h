// SquachWatch-CYD — SquachWare theme tokens + draw helpers
// RGB565 values mapped from the user's SquachWare CSS variables
// (see docs/SQUACHWARE-AESTHETIC.md for the full mapping).
#pragma once
#include <TFT_eSPI.h>
#include "state.h"

namespace Theme {
    // Background and chrome
    constexpr uint16_t BG           = 0x0801;  // #0a000f
    constexpr uint16_t TASKBAR      = 0x0803;  // #0d001a
    constexpr uint16_t PURPLE       = 0xAC1F;  // #b400ff
    constexpr uint16_t CYAN         = 0x07FF;  // #00fff5
    constexpr uint16_t PINK         = 0xF96F;  // #ff2d78
    constexpr uint16_t VAPOR_PINK   = 0xFB99;  // #ff71ce
    constexpr uint16_t VAPOR_PURPLE = 0xBB5F;  // #b967ff
    constexpr uint16_t VAPOR_BLUE   = 0x067F;  // #01cdfe
    constexpr uint16_t VAPOR_YELLOW = 0xFFD2;  // #fffb96
    constexpr uint16_t GREEN        = 0x07E0;  // #00ff88
    constexpr uint16_t AMBER        = 0xFD20;
    constexpr uint16_t RED          = 0xF800;
    constexpr uint16_t WHITE        = 0xFFFF;
    constexpr uint16_t BLACK        = 0x0000;

    // Squachy's fur — real Sasquach brown, matching the original
    // talkingsasquach.com drawSquachy() palette (not the neon set above).
    constexpr uint16_t FUR_DARK     = 0x38C0;  // #3d1800 shadow only
    constexpr uint16_t FUR_MAIN     = 0x5941;  // #5a2808 body/head fill
    constexpr uint16_t FUR_LIGHT    = 0x9326;  // #965a32 highlight/outline
    constexpr uint16_t SKIN_TAN     = 0xF60F;  // #f4c07a face patch
    constexpr uint16_t SKIN_DARK    = 0xCB88;  // #c87040 ear inner

    // Threat-tinted color for a detection type
    uint16_t colorFor(DetectionType t);

    // SquachWare titlebar gradient: cyan -> magenta across the bar
    uint16_t titlebarColor(int x, int w);

    // Linear blend between two RGB565 colors. t is 0..256 (8.8 fixed).
    uint16_t blend(uint16_t a, uint16_t b, uint16_t t);

    // Draws the gradient titlebar across the full width, with a 1-px
    // purple bottom border, centered white text, and the rotate button
    // in the top-right corner.
    void drawTitleBar(TFT_eSPI& t, const char* title);

    // Hit test for the rotate button drawn by drawTitleBar (top-right
    // corner of the title bar). The tap target is deliberately bigger
    // than the visual icon (extends below the title bar) so it's easy
    // to hit with a finger, not just a stylus.
    bool rotateButtonHit(int x, int y, int w);

    // SquachWare-style soft button: cyan label, 1-px purple border,
    // BG fill; pressed = filled purple with white label.
    void drawButton(TFT_eSPI& t, int x, int y, int w, int h,
                    const char* label, bool pressed);

    // Bottom [SCAN][LOG][CLR] button bar, laid out from the current
    // screen width/height so it adapts to any rotation (landscape or
    // portrait). Button height is a fixed finger-sized touch target,
    // independent of screen size.
    struct ButtonBarGeom {
        int y, h;
        int x[3], w[3];
    };
    ButtonBarGeom computeButtonBar(int screenW, int screenH);
    void drawButtonBar(TFT_eSPI& t, ButtonId highlighted);
    ButtonId hitTestButtonBar(int x, int y, int screenW, int screenH);

    // 1-pixel horizontal scanline (used on boot + alert screens).
    void drawScanline(TFT_eSPI& t, int y, uint16_t color = VAPOR_PURPLE);

    // Animated pulsing border (call once per frame from a ui tick).
    void drawPulsingBorder(TFT_eSPI& t, uint32_t now, uint16_t a, uint16_t b,
                           uint8_t thick = 4);

    // Boot-screen vaporwave sunset scene (dusk purple -> magenta ->
    // sunset orange sky, twinkling stars, a sinking retrowave sun,
    // drifting seagull silhouettes, and a dark synthwave floor with a
    // perspective grid) — same visual language as the CSI radar's 3-D
    // view. yTop/yHoriz/yBottom carve the screen into sky and floor.
    void drawSunsetSky(TFT_eSPI& t, uint32_t now, int yTop, int yHoriz);
    void drawSunsetSun(TFT_eSPI& t, int cx, int cy, int r, int yTop, int yHoriz);
    void drawSeagulls(TFT_eSPI& t, uint32_t now, int yTop, int yHoriz);
    void drawRetroFloor(TFT_eSPI& t, uint32_t now, int yHoriz, int yBottom);

    // Decorative Sasquach silhouette in the boot splash.
    // ~40 px tall, drawn at (cx, baseY) with PURPLE outline.
    void drawSasquachSilhouette(TFT_eSPI& t, int cx, int baseY);

    // 20-column Matrix digital rain tick. Columns fall at independent
    // speeds, heads cycle pink/cyan/green, trails fade to BG.
    void drawMatrixRain(TFT_eSPI& t, uint32_t now, int yStart, int yEnd);

    // Glitchy wordmark renderer for the ALERT screen bottom strip.
    // Horizontal jitter of +/-2 px every ~150 ms.
    void drawGlitchText(TFT_eSPI& t, int y, const char* text,
                        uint16_t color, uint32_t now);

    // Brief CRT-tear overlay drawn on top of an already-fully-drawn
    // frame right after a screen/rotation change — a few rows get
    // pixel-shifted sideways, fading out over totalMs. Call every
    // frame while elapsedMs < totalMs; a no-op once it's expired.
    void drawTransitionGlitch(TFT_eSPI& t, uint32_t elapsedMs, uint32_t totalMs);

    // Small rotating radar widget for the ALERT screen: a few range
    // rings, a continuously-rotating sweep, and a blip whose distance
    // from center reflects signal strength (closer = stronger) at a
    // bearing that's stable for a given MAC (so it doesn't jump around
    // between frames of the same alert).
    void drawSignalRadar(TFT_eSPI& t, int cx, int cy, int r, uint32_t now,
                        int8_t rssi, float bearingRad);
}
