// SquachWatch-CYD — clear (idle) screen implementation
#include "ui_clear.h"
#if SQUACH_MESH
#include "squachmesh.h"
#include "squachy.h"
#include "detection.h"

// A peer supplied from outside -- the emulator's --peer flag today, the radio
// eventually. Always wins over the demo below.
static const SquachMesh::Peer* s_guest = nullptr;
void uiClearSetGuest(const SquachMesh::Peer* p) { s_guest = p; }

#if SQUACH_MESH_DEMO
// LAB BUILDS ONLY. There is no radio yet, so a mesh build on real hardware
// would look identical to a normal one and there would be nothing to flash it
// for. This synthesises a visitor on a timer instead: he is absent for the
// first stretch so the screen can be compared against itself, then arrives and
// stays, wearing a different outfit on every cycle so one flash exercises the
// whole wardrobe rather than one costume.
//
// Deliberately NOT defined for the emulator, which builds SQUACH_MESH without
// this. A guest appearing unbidden in every sim render of the CLEAR screen
// would corrupt the instrument that every other visual decision is measured
// with -- the exact mistake the shim audit existed to undo.
static SquachMesh::Peer s_demo;
static bool     s_demoUp   = false;
static uint32_t s_demoSeen = 0xFFFFFFFFu;

static void demoTick(uint32_t now) {
    const uint32_t PERIOD = 20000;   // one full alone-then-visited cycle
    const uint32_t ALONE  = 6000;    // long enough to read the screen without him
    const uint32_t cycle  = now / PERIOD;
    if ((now % PERIOD) < ALONE) { s_demoUp = false; return; }
    if (s_demoUp && cycle == s_demoSeen) return;

    const uint8_t outfits = Squachy::outfitCount();
    s_demo.outfit = outfits ? (uint8_t)(cycle % outfits) : 0;
    s_demo.nick   = (uint8_t)(cycle % 10);
    s_demo.shade  = (uint8_t)(cycle % 4);
    s_demo.custom = false;
    s_demo.name[0] = '\0';
    s_demoUp   = true;
    s_demoSeen = cycle;
}
#endif // SQUACH_MESH_DEMO

// Defined below with the rest of the guest resolution, which reads more
// naturally next to the demo it falls back to than it would hoisted up here.
static const SquachMesh::Peer* rawGuest(uint32_t now);
static uint32_t                rawGuestId(uint32_t now);

// ---- the visit ------------------------------------------------------
// A visit has a shape: somebody turns up, they say hello, they stand around
// a while, somebody leaves. Modelling it as phases rather than "a guest is
// present" is what lets the arrival be an event and the hanging-around be a
// state -- the two things that were decided separately and have to coexist.
enum class VisitPhase : uint8_t { ARRIVING, MEETING, HANGING, LEAVING, GONE };
static VisitPhase   s_vp       = VisitPhase::GONE;
static uint32_t     s_vpAt     = 0;          // when this phase started
static uint32_t     s_beatAt   = 0;          // when the current line went up
static uint32_t     s_beatNo   = 0;          // advances once per line
static bool         s_guestTurn = false;
static const char*  s_visitGuestLine = nullptr;

static const uint32_t WALK_MS  = 1800;       // across the gap, either way
static const uint32_t BEAT_MS  = 4600;       // one line's time on screen
static const uint32_t HANG_MS  = 46000;      // before he thinks about leaving

// True while he should have a walk cycle under him.
static bool visitWalking() {
    return s_vp == VisitPhase::ARRIVING || s_vp == VisitPhase::LEAVING;
}

// Eased so he settles rather than stopping dead. Same shape both ways, with
// the endpoints swapped -- a departure that accelerated away would read as
// fleeing, and he is only going home.
static int visitGuestX(uint32_t now, int homeX, int offX) {
    if (s_vp == VisitPhase::ARRIVING || s_vp == VisitPhase::LEAVING) {
        float k = (float)(now - s_vpAt) / (float)WALK_MS;
        if (k < 0) k = 0; if (k > 1) k = 1;
        k = k * k * (3.0f - 2.0f * k);                 // smoothstep
        const float from = (s_vp == VisitPhase::ARRIVING) ? (float)offX : (float)homeX;
        const float to   = (s_vp == VisitPhase::ARRIVING) ? (float)homeX : (float)offX;
        return (int)(from + (to - from) * k);
    }
    return homeX;
}

static void visitBeat(uint32_t now, Squachy::VisitMoment m) {
    s_beatAt = now;
    s_beatNo++;
    s_guestTurn = !s_guestTurn;
    if (s_guestTurn) {
        // The guest's line is held, not re-rolled: his bubble is redrawn
        // every frame it is up, and picking again each time would flicker
        // through the pool instead of saying one thing.
        s_visitGuestLine = Squachy::visitGuestLine(m, s_beatNo);
    } else {
        s_visitGuestLine = nullptr;
        Squachy::visitReaction(m);          // the host says it himself
    }
}

// The guest being drawn, held as a COPY rather than a pointer.
//
// Two things go wrong without this. A peer whose advert changes mid-visit
// would morph on screen -- outfit and colours swapping on a Squachy standing
// still, which reads as a glitch rather than as anything. And the visitor has
// to keep being drawn through LEAVING after the peer is already gone, which a
// pointer to a slot that has been cleared cannot do.
static SquachMesh::Peer s_hosting{};
static uint32_t         s_hostingId = 0;

// What the screen should actually draw: the copy, for as long as the visit
// lasts, and nothing once it is over.
static const SquachMesh::Peer* visitHosting() {
    return (s_vp == VisitPhase::GONE) ? nullptr : &s_hosting;
}

// Public answer to "who is visiting": the hosted copy, not the raw radio
// state. Anything asking should see the same visitor the screen does,
// including while he is still walking out after the peer has gone.
const SquachMesh::Peer* uiClearGuest() { return visitHosting(); }

static void visitTick(uint32_t now) {
    const uint32_t id = rawGuestId(now);

    // Told once per frame rather than on transitions, so it cannot get stuck
    // set if a phase change is ever missed.
    Squachy::setVisiting(s_vp != VisitPhase::GONE);

    if (s_vp == VisitPhase::GONE) {
        if (!id) return;
        const SquachMesh::Peer* g = rawGuest(now);
        if (!g) return;
        s_hosting   = *g;                   // copied once, on arrival
        s_hostingId = id;
        s_vp = VisitPhase::ARRIVING; s_vpAt = now;
        s_guestTurn = true;                 // so the HOST speaks first
        s_visitGuestLine = nullptr;
        return;
    }

    // A DIFFERENT guest, or none. Either way the one being hosted has to
    // leave properly rather than being quietly replaced -- which is exactly
    // what used to happen: the synthetic visitor took the slot from a real
    // one and the guest appeared to change outfit instead of walking off.
    // The same handover applies to two real peers, one arriving as another
    // goes.
    if (id != s_hostingId && s_vp != VisitPhase::LEAVING) {
        s_vp = VisitPhase::LEAVING;
        visitBeat(now, Squachy::VisitMoment::PART);
        s_vpAt = now + BEAT_MS;             // goodbye first, then the walk
        return;
    }
    switch (s_vp) {
        case VisitPhase::ARRIVING:
            // Nobody talks while he is still walking. A greeting delivered
            // to somebody's back is a worse joke than no greeting.
            if (now - s_vpAt >= WALK_MS) {
                s_vp = VisitPhase::MEETING; s_vpAt = now;
                visitBeat(now, Squachy::VisitMoment::MEET);
            }
            break;
        case VisitPhase::MEETING:
            if (now - s_beatAt >= BEAT_MS) {
                if (s_guestTurn) {          // guest has answered; hello is done
                    s_vp = VisitPhase::HANGING; s_vpAt = now;
                    s_visitGuestLine = nullptr;
                    s_beatAt = now;
                } else {
                    visitBeat(now, Squachy::VisitMoment::MEET);
                }
            }
            break;
        case VisitPhase::HANGING:
            // Gaps between lines, not a wall of them. Standing together in
            // silence is most of the point.
            if (now - s_beatAt >= BEAT_MS * 2) {
                if (s_visitGuestLine) { s_visitGuestLine = nullptr; s_beatAt = now; }
                else visitBeat(now, Squachy::VisitMoment::HANGOUT);
            }
            if (now - s_vpAt >= HANG_MS) {   // he has been here a while
                s_vp = VisitPhase::LEAVING; s_vpAt = now;
                visitBeat(now, Squachy::VisitMoment::PART);
                // Goodbyes happen before he moves, so the walk-off is the
                // last thing rather than something talked over.
                s_vpAt = now + BEAT_MS;
            }
            break;
        case VisitPhase::LEAVING:
            if ((int32_t)(now - s_vpAt) >= (int32_t)WALK_MS) {
                s_vp = VisitPhase::GONE;
                s_visitGuestLine = nullptr;
            }
            break;
        default: break;
    }
}

// When a real peer was last in range, so the demo does not pounce on the
// slot the instant one walks away. Without this, unplugging the other device
// swapped the synthetic visitor straight in and the guest appeared to change
// outfit rather than to leave.
static uint32_t s_realSeenAt = 0;
static const uint32_t DEMO_COOLDOWN_MS = 15000;

// Whoever WOULD be visiting right now, before the visit machine has decided
// what to do about it. Not what gets drawn -- see visitHosting().
static const SquachMesh::Peer* rawGuest(uint32_t now) {
    if (s_guest) return s_guest;          // the emulator's --peer, when set
    if (const SquachMesh::Peer* p = Mesh::peer()) { s_realSeenAt = now; return p; }
#if SQUACH_MESH_DEMO
    // The synthetic visitor is a FALLBACK, not the feature: it fills an empty
    // room in a lab build and stands aside for anybody real. The cooldown is
    // what stops it treading on a real guest's exit.
    if (s_demoUp && (s_realSeenAt == 0 || now - s_realSeenAt > DEMO_COOLDOWN_MS))
        return &s_demo;
#endif
    return nullptr;
}

// Who that is, as a value the visit machine can compare against last frame.
// A real peer is its MAC folded down; the emulator's and the demo's are
// constants, because there is only ever one of each.
static uint32_t rawGuestId(uint32_t now) {
    if (s_guest) return 0x5111u;
    if (Mesh::peer()) {
        const uint8_t* m = Mesh::peerMac();
        uint32_t h = 2166136261u;                  // FNV-1a, plenty for six bytes
        for (int i = 0; i < 6; i++) { h ^= m[i]; h *= 16777619u; }
        return h ? h : 1u;                         // 0 means "nobody"
    }
#if SQUACH_MESH_DEMO
    if (s_demoUp && (s_realSeenAt == 0 || now - s_realSeenAt > DEMO_COOLDOWN_MS))
        return 0xDE1140u;
#endif
    (void)now;
    return 0;
}
#endif
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
        // Four letters, not "HACKER": this row is packed six-across in
        // landscape and four-across on a 240px portrait panel. EVILTWIN
        // keeps its own label everywhere else -- it just does not get its
        // own column here, because its count is inside this one.
        case DetectionType::HACKER:      return "HACK";
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
// go. It has somewhere to go now -- HACKER, below. Row stays at 12
// columns either way, so the layout has never moved.
static uint16_t counterCount(const DetectionEngine& eng, DetectionType t) {
    uint16_t n = eng.countByType(t);
    if (t == DetectionType::AIRTAG) {
        n += eng.countByType(DetectionType::GOOGLE_TAG)
           + eng.countByType(DetectionType::TILE)
           + eng.countByType(DetectionType::SAMSUNG_TAG);
    }
    // EVILTWIN folds into HACKER the same way, and for a better reason than
    // saving a column: a rogue AP is not a category of hardware, it is a
    // thing pentest hardware DOES. A Pineapple running PineAP karma is an
    // evil twin -- the same box, seen by its behaviour instead of by its
    // signature. Counting them apart would split one device across two
    // columns and read as two problems.
    if (t == DetectionType::HACKER) {
        n += eng.countByType(DetectionType::EVILTWIN);
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
    DetectionType::CAMERA,  DetectionType::HACKER,   DetectionType::RING,   DetectionType::DEAUTH,
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
    // LG, not MD. The size is a consequence of the word: at 90px LG the
    // headline uses under a third of a 320px row, where the old 17-character
    // one needed MD just to fit and still ran 216px. Short text does not
    // want the same face at a smaller size, it wants the bigger face -- and
    // this row is a burst that cuts in for a moment, so it has to land.
    static const Theme::BangersSize HEADLINE_SIZE = Theme::BangersSize::LG;
    // Measured off a render at HEADLINE_SIZE by diffing a seeded frame
    // against a --noseed one, which isolates the headline from a background
    // that repaints every row of this band: the painted result runs ty+4 to
    // ty+32, so 29 rows of which the 24-pass outline is the outer 2 each
    // way. 33 is what puts that last painted row exactly HEADLINE_PAD above
    // the counter text.
    static const int HEADLINE_H   = 33;
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
#if SQUACH_MESH
#if SQUACH_MESH_DEMO
        demoTick(now);
#endif
        // SquachMesh: when a peer is visiting, BOTH Squachys drop to SMALL and
        // stand apart. Small is not a courtesy to the guest, it is what makes
        // two of them fit at all -- at the default size he is already most of
        // the usable height, and two of those would overlap before either had
        // room to stand.
        //
        // The host keeps the left. A visitor arriving on the right is the
        // reading order and it means the resident does not appear to move
        // aside for a stranger.
        visitTick(now);
        // The hosted COPY, not whoever the radio is hearing right now. That
        // is what keeps a visitor from morphing mid-visit and what lets him
        // still be drawn while he walks out after the peer has gone.
        const SquachMesh::Peer* guest = visitHosting();
        if (guest) {
            const int SMALL_PCT = 70;
            const int gap  = w / 4;
            const int homeX = w / 2 + gap;     // where the guest stands
            const int offX  = w + 40;          // off the right edge

            Squachy::tick(t, w / 2 - gap, titleBottom, squachyBottom - titleBottom,
                          now, advance, 1.0f, false, 0, SMALL_PCT);

            // The visitor is drawn, not ticked: tick() owns mood, quip timers
            // and the walk state, all of which are file-static singletons
            // describing OUR Squachy. Calling it twice would have the guest
            // driving the host's animation. drawWaving is the same body with
            // none of that -- it is what the alert screen's cameo already uses.
            //
            // Both previews are the existing overrides the unlock popup uses
            // to show a costume nobody owns yet. Set them, draw, clear them:
            // left set they would silently redress our own Squachy everywhere.
            Squachy::setOutfitPreview((int8_t)guest->outfit);
            Squachy::setShadesPreview((int8_t)guest->shade);
            // The scale tick() actually used, not SMALL_PCT again: tick
            // derives its scale from the height it was given, so the two
            // numbers are different units and passing 0.7 here drew a
            // visitor less than half the host's size.
            const float gs = Squachy::lastScale();
            const int   gx = visitGuestX(now, homeX, offX);
            // wanderRangePx is what animates his legs. Walking in with it at 0
            // slid him across the floor like furniture; a couple of pixels of
            // wander is enough to put a walk cycle under the movement without
            // reading as a stagger.
            // Waves while walking in and through the hellos, then settles.
            // A guest who never stops waving reads as a stuck frame rather
            // than as a greeting once he has been there half a minute.
            const bool stillGreeting = (s_vp == VisitPhase::ARRIVING ||
                                        s_vp == VisitPhase::MEETING ||
                                        s_vp == VisitPhase::LEAVING);
            Squachy::drawWaving(t, gx, squachyBottom, now, gs,
                                s_visitGuestLine, s_visitGuestLine != nullptr,
                                visitWalking() ? 2 : 0, stillGreeting,
                                // Just above his own head, not the boot
                                // splash's 34 -- that lands in the host's
                                // bubble row and the two paint over each
                                // other.
                                20);
            Squachy::setShadesPreview(-1);
            Squachy::setOutfitPreview(-1);

            // Nameplate, under his feet rather than over his head: above is
            // where his speech bubble goes, and a name there would be hidden
            // for exactly the moments he is worth identifying.
            {
                const char* nm = (guest->custom && guest->name[0])
                                   ? guest->name
                                   : Squachy::nicknameAt(guest->nick);
                t.setTextSize(1);
                t.setTextWrap(false);
                const int nw = t.textWidth(nm);
                const int nx = gx - nw / 2;
                const int ny = squachyBottom + 2;
                // Only if it actually fits between his feet and the counters.
                if (ny + 8 <= counterTextTop - 2 && nx > 2 && nx + nw < w - 2) {
                    t.setTextColor(Theme::CYAN, Theme::BG);
                    t.setCursor(nx, ny);
                    t.print(nm);
                }
            }
        } else
#endif
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
    // Is anything actually live right now? Not lifetime -- a camera seen an
    // hour ago is not something happening, and the counters decay for the
    // same reason.
    bool anyActive = false;
    for (uint8_t i = 0; i < (uint8_t)DetectionType::COUNT; i++) {
        if (eng.countByType((DetectionType)i) > 0) { anyActive = true; break; }
    }
    // Its ARRIVAL is the event, so the glitch fires on the edge rather than
    // on the state -- once, when the screen goes from nothing to something,
    // not again when a second detection joins the first. Level 3 is where
    // the shared burst adds a full-screen tear on top of the jitter and
    // dropout, which is the point: the headline does not fade in, it cuts
    // in badly, the way a signal does.
    //
    // triggerGlitchBurst drives the same burst drawBangersText already reads
    // from, so the headline glitches on its own with nothing else wired up.
    static bool s_wasActive = false;
    if (anyActive && !s_wasActive) Theme::triggerGlitchBurst(3);
    s_wasActive = anyActive;

    // Nothing on the row while nothing is happening. ALL CLEAR is gone and
    // the headline does not replace it: a permanent label asserting anything
    // over a screen of zeroes is just untrue, and the counters underneath
    // already say the same thing more precisely.
    //
    // Blank is not a compromise here, it is the better screen. Squachy's
    // geometry hangs off counterTextTop rather than this row, so he does not
    // move -- he is simply no longer painted over by a 25-pass headline that
    // exists to be legible ON TOP of him. The pet and the Mowin' Man stop
    // running in behind it. And the 24 outline passes plus the fill come off
    // the frame the device spends nearly all its time rendering.
    //
    // Nothing needs erasing: the background repaints this whole band every
    // frame, so a headline that stops being drawn is simply gone next frame.
    if (anyActive) {
        // The rainbow ALL CLEAR used to own. It was the good part and it was
        // wasted on the state you see least; now it runs on the state that
        // actually matters. Same hue-wash as Squachy's party-mode confetti.
        static const uint16_t RAINBOW[6] = {
            Theme::RED, Theme::AMBER, Theme::GREEN,
            Theme::CYAN, Theme::VAPOR_PURPLE, Theme::PINK
        };
        const float huePos = fmodf((float)now / 900.0f, 6.0f);
        const int i0 = (int)huePos % 6, i1 = (i0 + 1) % 6;
        const uint16_t col =
            Theme::blend(RAINBOW[i0], RAINBOW[i1], (uint16_t)((huePos - (int)huePos) * 255));
        // Not a status label any more. This row now appears BECAUSE an
        // event happened, so it reads as the event rather than describing
        // the screen's state -- the job a label like ACTIVE DETECTIONS was
        // doing twice, and less precisely than the counters underneath.
        //
        // One word because the subject was the vague half. SOMETHING'S
        // NEARBY said nothing the counters do not say better; NEARBY is the
        // half that carries the meaning, and dropping the other one is what
        // buys the bigger face above.
        const char* msg = "NEARBY";
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
        int tw = Theme::bangersTextWidth(msg, HEADLINE_SIZE);
        int ty = headlineTop;
        if (tw <= w - 8) {
            int tx = (w - tw) / 2;
            for (uint8_t i = 0; i < 24; i++) {
                Theme::drawBangersText(t, tx + OUTLINE_OFS[i][0], ty + OUTLINE_OFS[i][1],
                                        msg, Theme::BLACK, HEADLINE_SIZE);
            }
            Theme::drawBangersText(t, tx, ty, msg, col, HEADLINE_SIZE);
        } else {
            // Kept as a guard, not because the current headline needs it:
            // NEARBY measures 90px in LG against the 232 a 240px portrait
            // screen leaves, so it clears by a factor of two and a half.
            // Bangers has no step below MD to fall back to the way the
            // built-in font does, so any future headline that outgrows the
            // narrow rotation drops to the built-in face rather than clip.
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
