// SquachWatch-CYD — SquachWare theme tokens + draw helpers
// RGB565 values mapped from the user's SquachWare CSS variables
// (see docs/SQUACHWARE-AESTHETIC.md for the full mapping).
#pragma once
#include <TFT_eSPI.h>
#include "state.h"

class DetectionEngine;

namespace Theme {
    // Background and chrome — NOT constexpr. These are runtime
    // variables (default-initialized to the original SquachWare
    // vaporwave values in theme.cpp) so a theme preset can overwrite
    // them from the settings menu. Every draw call across the project
    // already just reads e.g. Theme::BG by name, so swapping the
    // value here reaches everywhere without touching another file.
    // See Palette/kPalettes/applyPalette below.
    extern uint16_t BG;            // #0a000f
    extern uint16_t TASKBAR;       // #0d001a
    extern uint16_t PURPLE;        // #b400ff
    extern uint16_t CYAN;          // #00fff5
    extern uint16_t PINK;          // #ff2d78
    extern uint16_t VAPOR_PINK;    // #ff71ce
    extern uint16_t VAPOR_PURPLE;  // #b967ff
    extern uint16_t VAPOR_BLUE;    // #01cdfe
    extern uint16_t VAPOR_YELLOW;  // #fffb96
    extern uint16_t GREEN;         // #00ff88
    extern uint16_t AMBER;
    extern uint16_t RED;
    // Fixed regardless of theme — raw black/white contrast, not part
    // of any preset's "personality" colors.
    constexpr uint16_t WHITE        = 0xFFFF;
    constexpr uint16_t BLACK        = 0x0000;

    // Squachy's fur — real Sasquach brown, matching the original
    // talkingsasquach.com drawSquachy() palette (not the neon set
    // above). Deliberately NOT part of the swappable palette — he
    // should look like Squachy no matter which UI theme is active.
    constexpr uint16_t FUR_DARK     = 0x38C0;  // #3d1800 shadow only
    constexpr uint16_t FUR_MAIN     = 0x5941;  // #5a2808 body/head fill
    constexpr uint16_t FUR_LIGHT    = 0x9326;  // #965a32 highlight/outline
    constexpr uint16_t SKIN_TAN     = 0xF60F;  // #f4c07a face patch
    constexpr uint16_t SKIN_DARK    = 0xCB88;  // #c87040 ear inner

    // A full color-theme preset. name is shown in the settings menu.
    struct Palette {
        const char* name;
        uint16_t bg, taskbar, purple, cyan, pink,
                 vaporPink, vaporPurple, vaporBlue, vaporYellow,
                 green, amber, red;
    };
    static const uint8_t PALETTE_COUNT = 6;
    extern const Palette kPalettes[PALETTE_COUNT];

    // Overwrites BG/PURPLE/etc. from kPalettes[idx] (clamped). Call
    // once at boot with the persisted choice, and again whenever the
    // settings menu changes it.
    void applyPalette(uint8_t idx);

    // Threat-tinted color for a detection type
    uint16_t colorFor(DetectionType t);

    // SquachWare titlebar gradient: cyan -> magenta across the bar
    uint16_t titlebarColor(int x, int w);

    // Linear blend between two RGB565 colors. t is 0..256 (8.8 fixed).
    uint16_t blend(uint16_t a, uint16_t b, uint16_t t);

    // Draws the gradient titlebar across the full width, with a 1-px
    // purple bottom border, centered white text, the settings (hamburger)
    // button in the top-left corner, and the rotate button in the
    // top-right corner.
    void drawTitleBar(TFT_eSPI& t, const char* title);

    // Hit test for the rotate button drawn by drawTitleBar (top-right
    // corner of the title bar). The tap target is deliberately bigger
    // than the visual icon (extends below the title bar) so it's easy
    // to hit with a finger, not just a stylus.
    bool rotateButtonHit(int x, int y, int w);

    // Hit test for the settings button drawn by drawTitleBar (top-left
    // corner — mirrors rotateButtonHit's oversized tap target).
    bool settingsButtonHit(int x, int y);

    // SquachWare-style soft button: cyan label, 1-px purple border,
    // BG fill; pressed = filled purple with white label.
    void drawButton(TFT_eSPI& t, int x, int y, int w, int h,
                    const char* label, bool pressed);

    // Bottom [SCAN][LOG][CLR] button bar, laid out from the current
    // screen width/height so it adapts to any rotation (landscape or
    // portrait). Button height is a fixed finger-sized touch target,
    // independent of screen size. Settings lives in the title bar (see
    // settingsButtonHit above), not this bar.
    struct ButtonBarGeom {
        int y, h;
        int x[3], w[3];
    };
    ButtonBarGeom computeButtonBar(int screenW, int screenH);
    void drawButtonBar(TFT_eSPI& t, ButtonId highlighted);
    ButtonId hitTestButtonBar(int x, int y, int screenW, int screenH);

    // 1-pixel horizontal scanline (used on the boot screen).
    void drawScanline(TFT_eSPI& t, int y, uint16_t color = VAPOR_PURPLE);

    // The ALERT screen's ambient background — a small animated scene
    // themed to whatever was actually detected (an apple for AirTag, a
    // camera+shutter for FLOCK/AXON/ALPR/CAMERA, sunglasses for META,
    // etc.) instead of one generic effect for every type. Fully
    // repaints the w x h region every call, same discipline as the
    // CLEAR-screen backgrounds, so nothing trails between frames.
    void drawAlertFx(TFT_eSPI& t, DetectionType type, uint32_t now, int w, int h);

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

    // Idle-screen background styles, picked from the settings menu
    // (see Settings::Background). All four share the same signature —
    // draw into the band between yStart/yEnd, self-seed static state
    // on first call, and are safe to call every frame.

    // 20-column Matrix digital rain tick. Columns fall at independent
    // speeds, heads cycle pink/cyan/green, trails fade to BG.
    // advance: gates state mutation (column position/speed, the rare
    // glitch-message trigger) to once per logical frame -- see the
    // matching comment on Squachy::tick(). Boards that render in a
    // single pass never need to touch this (defaults to true).
    void drawMatrixRain(TFT_eSPI& t, uint32_t now, int yStart, int yEnd, bool advance = true);

    // Classic "flying through space" starfield: points radiate outward
    // from the band's center, accelerating and brightening as they
    // approach, then wrap back to the center once they exit the band.
    void drawStarfield(TFT_eSPI& t, uint32_t now, int yStart, int yEnd);

    // After Dark-style flying toasters: a handful of pixel-art toasters
    // with flapping wings drift up-and-right across the band, wrapping
    // around when they exit.
    void drawFlyingToasters(TFT_eSPI& t, uint32_t now, int yStart, int yEnd);

    // Lava lamp: a few soft warm blobs drift vertically with a gentle
    // sideways wobble inside a dark "glass tube", brightening as they
    // near neighbors to approximate a blob merge.
    void drawLavaLamp(TFT_eSPI& t, uint32_t now, int yStart, int yEnd);

    // Night-vision cryptid cam: green monochrome wash, grain/scanline
    // texture, a corner-cut "goggle tunnel" vignette, and a few Bigfoot-
    // silhouette walkers with glowing eyes pacing across the band.
    void drawCryptidCam(TFT_eSPI& t, uint32_t now, int yStart, int yEnd);

    // Radar sweep: range rings, a continuously-rotating sweep with a
    // fading trail wedge, and a few stationary blips that flash bright
    // as the sweep passes over them.
    void drawRadarSweep(TFT_eSPI& t, uint32_t now, int yStart, int yEnd);

    // Rain streaking down a pane of glass, with an occasional brief
    // lightning-flash brightness pulse across the whole band.
    void drawRainGlass(TFT_eSPI& t, uint32_t now, int yStart, int yEnd);

    // A handful of simple fish silhouettes drifting side to side at
    // different depths, with slow rising bubbles.
    void drawAquarium(TFT_eSPI& t, uint32_t now, int yStart, int yEnd);

    // Scrolling fake system log — hacker-movie-style boot chatter,
    // freshly assembled each line from small word banks, fading out
    // as it scrolls up and off.
    void drawTerminalLog(TFT_eSPI& t, uint32_t now, int yStart, int yEnd);

    // Fireflies: soft pulsing dots drifting slowly and bouncing off the
    // band's edges — a calm, low-contrast option.
    void drawFireflies(TFT_eSPI& t, uint32_t now, int yStart, int yEnd);

    // Live RF spectrum waterfall: channels 1-13 across the top,
    // scrolling activity history below, fed by DetectionEngine's real
    // per-channel activity tracking — not decorative, this one shows
    // actual ambient WiFi traffic.
    void drawSpectrumWaterfall(TFT_eSPI& t, uint32_t now, int yStart, int yEnd,
                               const DetectionEngine& eng);

    // Demoscene-style rotating wireframe tunnel: perspective hexagon
    // rings receding toward a pulsing vanishing point.
    void drawWireframeTunnel(TFT_eSPI& t, uint32_t now, int yStart, int yEnd);

    // Aurora: a few wavy vertical "curtains" of color drifting sideways
    // at different speeds/depths.
    void drawAurora(TFT_eSPI& t, uint32_t now, int yStart, int yEnd);

    // Classic Doom-style ASCII fire: a coarse heat grid seeded at the
    // bottom, propagated upward with random decay/drift, rendered
    // through a black -> red -> orange -> yellow -> white palette.
    void drawFire(TFT_eSPI& t, uint32_t now, int yStart, int yEnd);

    // Falling snow with a gentle sideways sway, a few larger bright
    // flakes mixed into a field of smaller dim ones.
    void drawSnowfall(TFT_eSPI& t, uint32_t now, int yStart, int yEnd);

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

    // Bangers (SIL OFL) comic-impact display font, baked in as a 1bpp
    // bitmap glyph set (see include/bangers_font.h) — used for
    // headline text where the built-in monospace font is too flat.
    // Covers A-Z, 0-9, space, '!'; anything else is silently skipped.
    // LG is sized for the boot splash title, MD for the ALERT screen's
    // "!! DETECTION !!" — pick whichever fits the string in question.
    enum class BangersSize { LG, MD };

    // x,y is the top-left of the font's ascender box, same convention
    // as TFT_eSPI's setCursor for the built-in font.
    void drawBangersText(TFT_eSPI& t, int x, int y, const char* s,
                        uint16_t color, BangersSize size);

    // Total advance width of s at the given size, for centering —
    // same role as TFT_eSPI's textWidth().
    int bangersTextWidth(const char* s, BangersSize size);
}
