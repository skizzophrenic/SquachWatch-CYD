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

    // Blends every swappable accent color toward BG by `t` (0..256,
    // same convention as blend() below -- 0 leaves colors alone, 256
    // replaces them entirely with BG). Used to show one of the
    // CLEAR-screen background effects at reduced visual strength
    // behind another screen's own content (Settings, specifically)
    // without needing true per-pixel alpha blending, which TFT_eSPI
    // has no cheap way to do. Returns the ORIGINAL colors (reusing the
    // Palette struct purely as a save-slot, not as a real preset) so a
    // matching restorePalette() call can put them back -- nesting
    // isn't supported, don't call this twice without restoring first.
    // BG/name aren't touched -- blending BG toward BG is a no-op.
    Palette dimPaletteForOverlay(uint16_t t);
    void restorePalette(const Palette& saved);

    // Threat-tinted color for a detection type
    uint16_t colorFor(DetectionType t);

    // SquachWare titlebar gradient: cyan -> magenta across the bar
    uint16_t titlebarColor(int x, int w);

    // Linear blend between two RGB565 colors. t is 0..256 (8.8 fixed).
    uint16_t blend(uint16_t a, uint16_t b, uint16_t t);

    // Draws the gradient titlebar across the full width, with a 1-px
    // purple bottom border, centered white text, the settings (hamburger)
    // button in the top-left corner, and the rotate button in the
    // top-right corner (unless hidden, see setRotateIconVisible()).
    void drawTitleBar(TFT_eSPI& t, const char* title);

    // Hides (or restores) the rotate icon drawTitleBar() would
    // otherwise always draw -- AWOK calls this once at boot with
    // false, since that board has no rotate button at all (see
    // main.cpp's rotate handler in loop(), which isn't even compiled
    // in on that board). Defaults to true (icon shown) for every other
    // board, unchanged from before this existed.
    void setRotateIconVisible(bool visible);

    // Hit test for the rotate button drawn by drawTitleBar (top-right
    // corner of the title bar). The tap target is deliberately bigger
    // than the visual icon (extends below the title bar) so it's easy
    // to hit with a finger, not just a stylus.
    bool rotateButtonHit(int x, int y, int w);

    // Hit test for the settings button drawn by drawTitleBar (top-left
    // corner — mirrors rotateButtonHit's oversized tap target).
    bool settingsButtonHit(int x, int y);

    // SquachWare-style soft button: cyan label, 1-px purple border,
    // BG fill; pressed = filled purple with white label. textSize
    // defaults to 1 (every existing caller's original look); a screen
    // with just one standalone button and room to spare (LOG's MORE
    // INFO panel's GOT IT) can pass 2 for a more prominent label --
    // caller's responsibility to size w/h generously enough to fit it.
    void drawButton(TFT_eSPI& t, int x, int y, int w, int h,
                    const char* label, bool pressed, uint8_t textSize = 1);

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

    // MAIN is the normal [SCAN][LOG][CLR] bar. SCAN_PICKER relabels the
    // exact same three slots as [BLE][WIFI][BACK] -- CLEAR's SCAN
    // button opens this in place rather than switching screens, so the
    // slot positions (and hitTestButtonBar's ButtonId::SCAN/LOG/CLR
    // return values) stay identical; only the caller's interpretation
    // of a hit changes based on which mode it asked to draw.
    enum class ButtonBarMode { MAIN, SCAN_PICKER };
    void drawButtonBar(TFT_eSPI& t, ButtonId highlighted, ButtonBarMode mode = ButtonBarMode::MAIN);
    ButtonId hitTestButtonBar(int x, int y, int screenW, int screenH);

    // 1-pixel horizontal scanline (used on the boot screen).
    void drawScanline(TFT_eSPI& t, int y, uint16_t color = VAPOR_PURPLE);

    // Scroll-position indicator for a scrollable list -- a thin track
    // spanning the content area's full height plus a brighter "thumb"
    // sized and positioned proportionally to how much is visible vs.
    // total, so a user can actually tell there's more content below
    // (previously nothing on-screen ever hinted a list could scroll).
    // x is the right-hand edge column it's drawn at; reserve ~10px of
    // width there in the caller's own row layout so this doesn't
    // overlap right-aligned row content. Draws nothing at all when
    // totalItems <= visibleItems -- no indicator when everything
    // already fits on one screen.
    void drawScrollbar(TFT_eSPI& t, int x, int y, int h,
                       int totalItems, int visibleItems, int scrollOffset);

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
    // The boot splash's sunset scene as a full background: sky, sun,
    // parallax ridgeline, reflective water and the neon grid, composed
    // into one band-filling effect. See its comment in theme.cpp for
    // why the reflection is recomputed rather than sampled.
    // horizonFrac places the waterline within the band. The default
    // suits a background; the boot splash passes its own so the gulls
    // and Squachy, which are positioned against that line, stay where
    // they were.
    void drawSynthwave(TFT_eSPI& t, uint32_t now, int yTop, int yBottom,
                       float horizonFrac = 0.44f);


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

    // Textured corridor receding to a drifting vanishing point. Square
    // rather than round -- see its comment in theme.cpp for why that
    // choice is what makes it affordable without lookup tables.
    void drawWireframeTunnel(TFT_eSPI& t, uint32_t now, int yStart, int yEnd);

    // Classic Doom-style ASCII fire: a coarse heat grid seeded at the
    // bottom, propagated upward with random decay/drift, rendered
    // through a black -> red -> orange -> yellow -> white palette.
    void drawFire(TFT_eSPI& t, uint32_t now, int yStart, int yEnd);

    // A tap that landed on the idle background, in screen coordinates.
    // Backgrounds with something tappable in them consume it and return
    // true, in which case the caller must NOT also treat the tap as one
    // of the CLEAR screen gestures. Everything else returns false and
    // nothing changes.
    //
    // Only FIRE uses this today: ten taps on its moon summon a
    // werewolf. The moon waxes from crescent toward full as the taps
    // land, which is the only feedback that the count is going up --
    // without it the egg is unfindable and, worse, unconfirmable when
    // you are halfway through it.
    bool backgroundTap(int x, int y, uint32_t now);

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

    // True during the shared random glitch burst drawBangersText()
    // already rolls every ~5-10s (see its own comment) -- exposed so
    // other draw code can layer a coordinated effect onto the exact
    // same burst instead of running its own independent timer.
    bool glitchActive();

    // Force-starts a burst right now instead of waiting for the
    // ambient 5-10s roll, and reschedules the next ambient one from
    // this point -- lets a screen that wants its own more deliberate
    // cadence (e.g. ALERT firing one immediately, then again at fixed
    // offsets) drive the exact same shared burst everything else reads
    // from, rather than needing a second parallel glitch system.
    // intensity (0..4, clamped) scales how aggressive THIS burst reads
    // across every glitch-aware draw call -- jitter range, scanline
    // dropout rate, static speckle density, burst length, and (level 3+)
    // a full-screen pixel-shift tear layered on top. Ambient
    // auto-triggered bursts (nobody called this) always run at a fixed
    // mild level so idle screens stay consistent; only an explicit
    // caller can ask for something louder.
    void triggerGlitchBurst(uint8_t intensity = 1);

    // Scatters a light dusting of bright speckle marks across the
    // given region -- real TV-static snow, not the deterministic
    // per-bucket jitter drawBangersText() uses, since this has no
    // multi-pass outline to stay in sync with (it's meant to be called
    // once per frame, layered on top of whatever's already drawn).
    // A no-op whenever glitchActive() is false, so it's safe to call
    // unconditionally every tick.
    void drawGlitchStatic(TFT_eSPI& t, int x0, int y0, int x1, int y1);

    // Greedy word-wrap using the currently-set font's real measured
    // widths (not an assumed char width), so it stays correct even if
    // the font ever changes. No dynamic allocation -- lines[][48] is a
    // caller-owned fixed buffer, fine for the short-to-medium strings
    // this runs on (speech bubbles, the onboarding walkthrough, LOG's
    // MORE INFO panel) -- a line is force-broken the moment it would
    // fill that buffer, independent of maxW, so a generous maxW on a
    // wide panel can't silently truncate a line mid-word. Returns how
    // many of the up-to-maxLines rows it actually filled; text that
    // doesn't fit even at maxLines is silently truncated rather than
    // dropped entirely -- the last row just runs long instead of
    // losing the rest of the sentence.
    uint8_t wrapText(TFT_eSPI& t, const char* text, int maxW,
                     char lines[][48], uint8_t maxLines);

    // Modal "MORE INFO" explanation panel -- shared by LOG's confirm
    // panel and ALERT's own MORE INFO button (the two screens are never
    // showing it at the same time, so one implementation is enough).
    // Squachy explains via a lightweight drawWaving() cameo, forced
    // into his talking-mouth animation and patrolling back and forth
    // across the panel, rather than standing still -- a one-shot
    // explanation still reads better with some life in it than a
    // static pose. typeName is a Bangers-font heading (e.g. "RING") --
    // pass nullptr to skip it, which the one-time RSSI/confidence
    // primer page does since it isn't about any one detection type.
    // w/h are the caller's own screen dimensions (not necessarily
    // t.width()/height() -- CYD35's two-pass half-height rendering
    // temporarily changes what those report via setViewport()).
    void drawInfoPanel(TFT_eSPI& t, int w, int h, uint32_t now,
                       const char* typeName, const char* text);
    bool infoPanelHitDismiss(int x, int y, int screenW, int screenH);
}
