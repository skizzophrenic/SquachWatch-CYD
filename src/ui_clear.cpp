// SquachWatch-CYD — clear (idle) screen implementation
#include "ui_clear.h"
#include "theme.h"
#include "squachy.h"
#include "pet.h"
#include "settings.h"
#include "idle_events.h"
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
        // Not just Meta any more -- Snap Spectacles and Luxottica land here too.
        case DetectionType::META:        return "GLASS";
        case DetectionType::SKIMMER:     return "SKIM";
        // AIRTAG doubles as the combined "TRACKER" bucket here -- see
        // counterCount() below. GOOGLE_TAG/TILE/SAMSUNG_TAG keep their
        // own labels everywhere else (LOG screen, colors, vendor
        // names); this is just the compact main-screen row folding all
        // four BLE-tracker types into one column to save space.
        case DetectionType::AIRTAG:      return "TRACKER";
        case DetectionType::DRONE:       return "DRONE";
        case DetectionType::RAVEN:       return "RAV";
        case DetectionType::ALPR:        return "ALPR";
        case DetectionType::CAMERA:      return "CAM";
        case DetectionType::SAMSUNG_TAG: return "STAG";
        case DetectionType::GOOGLE_TAG:  return "GTAG";
        case DetectionType::TILE:        return "TILE";
        case DetectionType::RING:        return "RING";
        case DetectionType::DEAUTH:      return "DEAUTH";
        case DetectionType::EVILTWIN:    return "EVIL";
        default:                         return "?";
    }
}

// GOOGLE_TAG, TILE and SAMSUNG_TAG's counts fold into AIRTAG's here
// (see counterLabel's "TRACKER" case above) -- they're still tracked
// and displayed as their own distinct types everywhere else (LOG
// screen, colors, vendor labels), just combined into one number/column
// on this compact row to free up space, especially under the 4-per-row
// portrait cap.
//
// SAMSUNG_TAG joined the fold when EVILTWIN was added: they're all
// "something is quietly tracking you" and read fine as one number,
// whereas a rogue AP is a different kind of problem and had nowhere to
// go. Keeps the row at 12 columns, so the layout is unchanged.
static uint16_t counterCount(const DetectionEngine& eng, DetectionType t) {
    uint16_t n = eng.countByType(t);
    if (t == DetectionType::AIRTAG) {
        n += eng.countByType(DetectionType::GOOGLE_TAG)
           + eng.countByType(DetectionType::TILE)
           + eng.countByType(DetectionType::SAMSUNG_TAG);
    }
    return n;
}

// All types in one place, chunked into rows of at most MAX_PER_ROW at
// draw time (see uiClearTick()) instead of two hand-split arrays --
// the old 7-and-6 split ran wide enough on a narrow 240px portrait
// screen that FLOCK (first on the line) got clipped off the left edge
// entirely. A hard per-row cap fixes that on both boards, not just
// AWOK's narrower panel. GOOGLE_TAG/TILE are deliberately absent --
// AIRTAG stands in for all four as "TRACKER" (see counterLabel/
// counterCount above).
static const DetectionType ALL_COUNTER_TYPES[] = {
    DetectionType::FLOCK,   DetectionType::AXON,     DetectionType::META,   DetectionType::SKIMMER,
    DetectionType::RAVEN,   DetectionType::AIRTAG,   DetectionType::DRONE,  DetectionType::ALPR,
    DetectionType::CAMERA,  DetectionType::EVILTWIN, DetectionType::RING,   DetectionType::DEAUTH,
};
static const uint8_t ALL_COUNTER_TYPES_N = sizeof(ALL_COUNTER_TYPES) / sizeof(ALL_COUNTER_TYPES[0]);
// Portrait (narrow) caps at 4 per row -- see the comment above. Landscape
// has plenty of width for the original 7-and-6 two-row split (that's
// exactly what this produces: 13 types / 2 rows), so row count is
// picked dynamically off the live orientation in uiClearTick() rather
// than fixed at compile time -- it changes every time the screen
// rotates, not just once per board.
static const uint8_t MAX_PER_ROW_PORTRAIT   = 4;
static const uint8_t COUNTER_ROWS_LANDSCAPE = 2;
static const uint8_t COUNTER_ROWS_PORTRAIT  =
    (ALL_COUNTER_TYPES_N + MAX_PER_ROW_PORTRAIT - 1) / MAX_PER_ROW_PORTRAIT;  // ceil

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
                        counterLabel(types[i]), counterCount(eng, types[i]));
    }
    int tw = t.textWidth(buf);
    t.setCursor((w - tw) / 2, y);
    t.print(buf);
}

void uiClearTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng, bool advance, bool scanMenu) {
    int w = t.width();
    int h = t.height();
    // Recomputed every tick, not cached per-board: rotating the screen
    // changes w/h live, and the counter layout should follow it rather
    // than staying stuck at whatever orientation was active at boot.
    bool landscape = w > h;
    const uint8_t counterRows = landscape ? COUNTER_ROWS_LANDSCAPE : COUNTER_ROWS_PORTRAIT;

    Theme::ButtonBarGeom bar = Theme::computeButtonBar(w, h);
    const int lineH          = 14;
    const int countersTop    = bar.y - counterRows * lineH - 6;
    // bar.y - 1, not bar.y - 4. Those three rows were a real seam rather
    // than spare margin: the animation repainted down to 209 and the button
    // bar starts at 214, so 210-213 were painted once at boot and never
    // touched again. On DIGITAL RAIN a flat black strip sat under the
    // falling glyphs; on TERMINAL LOG the text stopped a few rows short of
    // the buttons. Nothing draws there, so the animation may as well.
    //
    // It is also what lets the counter rows sit as low as they now do --
    // see counterTextTop. They have to land inside the repainted region or
    // they smear, and 209 was the wall.
    const int countersBottom = bar.y - 1;
    // The counter rows alone sit 9px lower than countersTop, and nothing
    // else does. Measured off a rendered landscape frame before any of
    // this: the headline's ink ended at row 172, the two counter rows ran
    // 180-186 and 194-200, and the button bar started at 214 -- so the
    // block sat 7px under the headline and 13px above the buttons, hugging
    // the text above it.
    //
    // Centring it in that slack was the first attempt and it was the wrong
    // one: 10px above and 10px below is balanced, and reads as belonging to
    // neither the headline nor the buttons. Low is better. The counters are
    // chrome, the same as the buttons are, so grouping the two into one
    // footer and leaving the headline up with Squachy says what goes with
    // what. 9 puts the last row's ink 4px off the button bar.
    //
    // Not further: 13 would touch the buttons. The real ceiling was lower
    // still until a moment ago -- the rows have to land inside the
    // background's repaint or they smear, and that wall sat at 209, which
    // is why countersBottom above had to move first.
    //
    // Deliberately NOT folded into countersTop, which would look like the
    // tidier fix. That value is the floor of everything above it: the
    // headline is bottom-aligned to it, Squachy sizes himself against it,
    // the pet takes it as its band, and the background repaints to it.
    // Moving it would slide the headline down with the rows (leaving the
    // grouping exactly as muddled as before) and hand Squachy nine more
    // pixels of height -- which he cannot take: his waving arm already
    // reaches row 4, and growing him walks the hand off the top edge.
    //
    // Orientation-independent by construction. The last row's ink lands at
    // bar.y - 14 + this whatever counterRows is, so portrait's four rows
    // clear the buttons by the same 4px landscape's two do.
    const int counterTextTop = countersTop + 9;

    // The headline hangs off the counter block now, not off countersTop.
    //
    // Measured off a render: ty puts the Bangers ink 4 rows lower and it
    // runs 23 rows, with the 24-pass outline adding 2 more each way. So the
    // whole painted band is ty+2 .. ty+28, and sitting it HEADLINE_PAD above
    // the counters is that arithmetic run backwards.
    //
    // It used to bottom-align to countersTop, which put it at row 146 --
    // floating across his shins in the middle of otherwise empty space,
    // with 17 rows of nothing between it and the numbers it belongs to.
    static const int HEADLINE_H   = 28;
    static const int HEADLINE_PAD = 5;
    const int headlineTop = counterTextTop - HEADLINE_PAD - HEADLINE_H;

    // ...which frees the rows it used to sit in, and Squachy takes them.
    //
    // His floor was countersTop, a number that stopped meaning anything to
    // him once the counters moved down into the footer: it is neither where
    // the text starts nor where the screen runs out. Two rows above the
    // counter ink is the real bottom of the space he has.
    //
    // He is sized from the band he is handed -- scale = charAvail /
    // BASE_HEIGHT -- so this is the whole of "make him bigger", and it is
    // bigger everywhere rather than per costume. His feet land on the new
    // floor for free; nothing else needs moving.
    //
    // The ceiling on this is his WAVING ARM, not his head. It reaches about
    // two rows above his crest scaled, and the crest is only 8 rows off the
    // top today, so there is far less room up there than the empty-looking
    // rows suggest. See the measurement in the commit that added this.
    const int squachyBottom = counterTextTop - 2;

    const int titleBottom  = 16;
    // The animation runs all the way down to just above the button
    // bar, covering Squachy's region, the text row, and the counters —
    // everything below the title bar erases and repaints together
    // every frame.
    const int rainEnd      = countersBottom;

    // Background animation, from below the title bar down to just
    // above the button bar — style picked from the settings menu.
    // The counters get drawn over the band further down, so tell the
    // background where its usable floor really is before it places
    // anything that stands on the ground.
    // Two arguments now, because the counters no longer start where Squachy's
    // feet land. Cameos still stand level with him at countersTop; the
    // backgrounds that fill a bright ground band keep filling to where the
    // numbers actually begin, instead of stopping nine pixels short of them.
    Theme::setBackgroundFloor(squachyBottom, counterTextTop);
    // From the very top of the screen, not from titleBottom. The title bar
    // used to own rows 0-15 and paint them every frame; with it gone they
    // belonged to nobody and kept whatever the previous frame left there.
    // Handing them to the background is also the point of removing the bar:
    // the animation now runs edge to edge behind the two corner buttons.
    //
    // Squachy is still told his band starts at titleBottom. He sizes himself
    // from the space he is given, so telling him about these rows would make
    // him a tenth bigger and move everything hanging off him.
    Theme::drawActiveBackground(t, now, 0, rainEnd, eng, advance);
    Theme::clearBackgroundFloor();

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
        // The last argument is the SIZE row in Settings. CLEAR is the only
        // screen that passes it: everywhere else he is a cameo in a box
        // somebody sized deliberately, and shrinking him there would just
        // leave a hole.
        Squachy::tick(t, w / 2, titleBottom, squachyBottom - titleBottom, now, advance,
                      1.0f, false, -1, Settings::squachySizePct());
    }


    // Rare decorative flourishes (UFO/sparkle/critter/glitch-line) --
    // see idle_events.h. Skipped for the same reasons Squachy's own
    // presence is skipped above (boring mode) or would collide with
    // the walkthrough's own bubble (onboarding) -- a UFO flying past
    // mid-lesson would be more distraction than delight.
    if (!Settings::boringMode() && !Squachy::onboardingActive()) {
        IdleEvents::tick(t, now, 0, titleBottom, w, squachyBottom, advance);
    }

    // Whatever the background wants on top of the mascot. Right now
    // that is the werewolf's speech bubble: drawFire computes it but
    // deliberately does not paint it, because the background is drawn
    // before Squachy and the bubble was ending up behind him. Same
    // reasoning as ALL CLEAR below -- text is the one thing here that
    // cannot afford to be half-covered.
    Theme::drawBackgroundOverlay(t, now);

    // Title bar at the top
    Theme::drawTitleBar(t, ">> SQUACHWATCH <<  SCANNING");

    // ALL CLEAR (only flash if there are NO active detections). Same
    // Bangers headline font as the ALERT screen's "!! DETECTION !!" —
    // now sitting directly on top of the counter block it labels, rather
    // than floating in the middle of the empty space above it. See
    // headlineTop.
    //
    // Still drawn AFTER Squachy, and that stays deliberate. Drawing it
    // first would let his shadow and feet fall across it, which sounds
    // better than it is: he is about 60px wide at the ankles against a
    // 185px headline, and a word with its middle punched out is not a
    // word. The 24-pass black outline is what keeps it legible over him.
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
            msg = "ACTIVE DETECTIONS";
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
        int ty = headlineTop;
        if (tw <= w - 8) {
            int tx = (w - tw) / 2;
            for (uint8_t i = 0; i < 24; i++) {
                Theme::drawBangersText(t, tx + OUTLINE_OFS[i][0], ty + OUTLINE_OFS[i][1],
                                        msg, Theme::BLACK, Theme::BangersSize::MD);
            }
            Theme::drawBangersText(t, tx, ty, msg, col, Theme::BangersSize::MD);
        } else {
            // "ACTIVE DETECTIONS" is long enough to overflow the
            // narrowest (240px portrait) rotation at this font's fixed
            // size — Bangers has no smaller step to fall back to like
            // the built-in font does, so drop to that instead rather
            // than clip.
            t.setTextSize(2);
            int sw = t.textWidth(msg);
            int sx = (w - sw) / 2, sy = counterTextTop - HEADLINE_PAD - t.fontHeight(2);
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

    // The pet, after Squachy AND after the headline.
    //
    // After Squachy because he perches on top of him. After the headline
    // because he spends most of his visits on the ground, and the ground on
    // this screen is the same rows the headline occupies -- drawn before it
    // he stood there for three seconds with his legs behind ACTIVE
    // DETECTIONS, which is a poor showing for the only other character on
    // the device.
    //
    // Squachy stays behind the headline on purpose and this does not change
    // that: he is 130px of opaque brown and the text has to survive him. The
    // pet is thirty pixels wide and moving, so passing in front reads as
    // depth rather than as an obstruction.
    //
    // Still before the counters, which he never reaches.
    if (!Settings::boringMode()) Pet::tick(t, now, w, titleBottom, squachyBottom);

    // Counter lines above the buttons — all 13 detection types, split
    // across counterRows (2 in landscape, capped at 4/row in portrait
    // -- see the constants above) so a row never runs wide enough to
    // clip off a narrow portrait screen, while landscape still gets
    // the more compact two-row layout it has room for. Whole
    // label:count tokens only, so nothing ever breaks mid-word. No
    // flat clear here anymore — the background animation now fully
    // repaints this whole row every frame (rainEnd extends down to
    // countersBottom), the same "let the background do the erasing"
    // pattern already relied on for Squachy and the status line above.
    t.setTextSize(1);
    t.setTextColor(Theme::CYAN, Theme::BG);
    t.setTextWrap(false);

    // Evenly balanced, not greedily packed (e.g. 4/4/4/1 in portrait)
    // -- a lone last row with a single item looked worse than several
    // similarly-sized rows does, and this still never exceeds the
    // per-orientation cap on any row.
    uint8_t base      = ALL_COUNTER_TYPES_N / counterRows;
    uint8_t remainder = ALL_COUNTER_TYPES_N % counterRows;
    uint8_t start = 0;
    for (uint8_t row = 0; row < counterRows; row++) {
        uint8_t n = base + (row < remainder ? 1 : 0);
        drawCounterLine(t, w, counterTextTop + row * lineH, eng, ALL_COUNTER_TYPES + start, n);
        start += n;
    }

    // Soft buttons. The background animation's own repaint stops at
    // countersBottom (see rainEnd above) and each button only fills
    // its own rect, so the row's margins/gaps around and between the
    // three buttons were never actually touched by anything -- on
    // cyd35 specifically (two-pass half-height rendering, see its
    // CLEAR case in main.cpp) that let content from elsewhere in the
    // shared sprite buffer show through there. An explicit flat clear
    // of the whole remaining strip first guarantees it's always clean
    // background before the buttons draw on top, regardless of cause.
    t.fillRect(0, countersBottom, w, h - countersBottom, Theme::BG);
    Theme::drawButtonBar(t, ButtonId::NONE,
                         scanMenu ? Theme::ButtonBarMode::SCAN_PICKER : Theme::ButtonBarMode::MAIN);
}
