// SquachWatch-CYD — ALERT screen implementation
#include "ui_alert.h"
#include "theme.h"
#include "signatures.h"
#include "detection.h"
#include "ignore_list.h"
#include <Arduino.h>

static const char* targetLabel(DetectionType t) {
    switch (t) {
        case DetectionType::FLOCK:   return "FLOCK CAM";
        case DetectionType::AXON:    return "AXON BODY";
        case DetectionType::META:    return "META GLASSES";
        case DetectionType::SKIMMER: return "CARD SKIMMER";
        case DetectionType::RAVEN:   return "RAVEN";
        case DetectionType::AIRTAG:  return "AIRTAG";
        case DetectionType::DRONE:   return "DRONE";
        case DetectionType::ALPR:    return "ALPR";
        case DetectionType::CAMERA:  return "GAMERA";
        case DetectionType::SAMSUNG_TAG: return "SAMSUNG TAG";
        case DetectionType::GOOGLE_TAG:  return "GOOGLE TAG";
        case DetectionType::TILE:        return "TILE";
        case DetectionType::RING:        return "RING CAM";
        case DetectionType::EVILTWIN:    return "EVIL TWIN AP";
        case DetectionType::IBEACON:     return "PROXIMITY BEACON";
        case DetectionType::HACKER:      return "HACKER HARDWARE";
        default:                     return "UNKNOWN";
    }
}

static Detection s_last;
static bool s_touched = false;
static bool s_redacted = false;
void uiAlertSetRedacted(bool r) { s_redacted = r; }

// Six saturated stops blended pairwise. The same construction the CLEAR
// headline's rainbow uses, and for the same reason: RGB332 has 8 levels of
// red and green but only FOUR of blue, so a computed HSV sweep bands badly
// while a walk between colours known to survive the downconvert does not.
static uint16_t hue6(float pos) {
    static const uint16_t STOPS[6] = {
        Theme::RED, Theme::AMBER, Theme::GREEN,
        Theme::CYAN, Theme::VAPOR_PURPLE, Theme::PINK
    };
    pos = fmodf(pos, 6.0f);
    if (pos < 0) pos += 6.0f;
    const int i0 = (int)pos, i1 = (i0 + 1) % 6;
    return Theme::blend(STOPS[i0], STOPS[i1], (uint16_t)((pos - i0) * 255));
}

static uint16_t lighten(uint16_t c, uint16_t amt) { return Theme::blend(c, Theme::WHITE, amt); }
static uint16_t darken (uint16_t c, uint16_t amt) { return Theme::blend(c, Theme::BLACK, amt); }

// The RGB frame. One full spectrum wrapped exactly once around the
// perimeter, rotating -- so every hue is on screen at all times and the
// border never goes dark or reads as switched off.
//
// The walk is what makes that possible: d is the distance travelled around
// the outer rectangle, continuous from 0 back to P, so the hue at the last
// pixel of the left edge meets the hue at the first pixel of the top. Each
// step paints BORDER_W pixels inward, which means the four corners are
// painted twice -- they agree, because both visits carry the same d.
static const int   BORDER_W  = 2;
static const float BORDER_MS = 400.0f;  // ms per sixth of the spectrum

static void drawRgbBorder(TFT_eSPI& t, int w, int h, uint32_t now) {
    const float P = (float)(2 * (w + h));
    const float phase = (float)now / BORDER_MS;
    for (int x = 0; x < w; x++) {
        t.drawFastVLine(x, 0, BORDER_W, hue6((float)x / P * 6.0f + phase));
        t.drawFastVLine(x, h - BORDER_W, BORDER_W,
                        hue6((float)(w + h + (w - 1 - x)) / P * 6.0f + phase));
    }
    for (int y = 0; y < h; y++) {
        t.drawFastHLine(w - BORDER_W, y, BORDER_W,
                        hue6((float)(w + y) / P * 6.0f + phase));
        t.drawFastHLine(0, y, BORDER_W,
                        hue6((float)(2 * w + h + (h - 1 - y)) / P * 6.0f + phase));
    }
}

// The header strip's bevel, built from the strip's OWN colour rather than
// Win95 grey: the strip is a coloured title bar, and grey chrome around it
// would read as a second, unrelated widget sitting behind the type name.
//
// Pushed harder than the IGNORE button's bevel. That one has five distinct
// greys to build from; a bevel made out of a single colour has only what
// lightening and darkening can reach from that colour, and on a mid purple
// through RGB332 the gentle version came back as almost nothing.
static void stripBevel(TFT_eSPI& t, int x, int y, int w, int stripH, uint16_t c) {
    const uint16_t lit = lighten(c, 170), litSoft = lighten(c, 75);
    const uint16_t shd = darken(c, 170),  shdSoft = darken(c, 75);
    const int R = x + w - 1, Bm = y + stripH - 1;
    t.drawFastHLine(x, y, w, lit);
    t.drawFastHLine(x, y + 1, w, litSoft);
    t.drawFastHLine(x, Bm - 1, w, shdSoft);
    t.drawFastHLine(x, Bm, w, shd);
    t.drawFastVLine(x, y, stripH, lit);
    t.drawFastVLine(x + 1, y, stripH, litSoft);
    t.drawFastVLine(R - 1, y, stripH, shdSoft);
    t.drawFastVLine(R, y, stripH, shd);
}

// Whether the player's selected background animates behind the alert.
// Kept as a named constant rather than inlined so it is one edit to take
// back out, and 0..255 of dim so it can be tuned without touching the
// draw order.
static const bool     ALERT_SHOW_BACKGROUND = true;
static const uint16_t ALERT_PALETTE_DIM     = 150;  // palette knock-back
static const uint8_t  ALERT_BACKGROUND_DIM  = 128;  // every other row to BG

// ALERT gets its own deliberate glitch cadence instead of waiting on
// the shared ambient 5-10s roll -- one right away (so the screen reads
// as glitchy from the first frame, not just eventually), then again at
// 5s/8s/10s, each one louder than the last (see
// Theme::triggerGlitchBurst()'s intensity param), then holding at the
// loudest level every 2.5s for as long as the alert stays up. All of
// it fires through that same shared burst drawBangersText()/
// drawGlitchStatic() everywhere else already read from, so this is
// just a schedule + escalation curve, not a second rendering path.
static uint32_t s_alertStart = 0;
static uint8_t  s_glitchStep = 0;

// step 0 (immediate) -> level 1, step 1 (2.5s) -> level 2, step 2 (4s)
// -> level 3 (screen tear joins in), step 3 (5s) -> level 4 (loudest),
// step 4+ (every 1.25s after) stays pinned at 4. Twice the cadence of
// the original 0/5/8/10s + 2.5s schedule -- same shape, half the wait.
static uint32_t glitchStepOffsetMs(uint8_t step) {
    switch (step) {
        case 0: return 0;
        case 1: return 2500;
        case 2: return 4000;
        case 3: return 5000;
        default: return 5000 + (uint32_t)(step - 3) * 1250;
    }
}
static uint8_t glitchStepLevel(uint8_t step) {
    uint16_t lv = (uint16_t)step + 1;
    return (uint8_t)(lv > 4 ? 4 : lv);
}

void uiAlertInit(TFT_eSPI& t, const Detection& d) {
    s_last = d;
    s_touched = false;
    s_alertStart = millis();
    s_glitchStep = 0;
    // fillScreen() relies on TFT_eSPI's base-class width/height, which
    // TFT_eSprite::createSprite() never updates — it leaves stale
    // remnants of whatever screen was drawn before when t is a sprite.
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
}

// MORE INFO button, bottom-right corner -- every other element on this
// screen (title, target label, confidence, vendor, MAC, RSSI, radar,
// wordmark) stays horizontally centered regardless of rotation, so a
// corner is the one spot guaranteed clear on all of them.
static void moreInfoBtnRect(int screenW, int screenH, int& bx, int& by, int& bw, int& bh) {
    // Stacked "MORE"/"INFO" on two lines (see the draw call below)
    // instead of one wide "MORE INFO" row -- a size-2 attempt at a
    // bigger tap target that widened the button instead ate into the
    // running critter animation's clear margin on the narrowest 240px
    // landscape rotation. Going taller instead of wider keeps the
    // footprint narrow enough to actually clear the radar/MAC/wordmark
    // over there while still being a generously padded, easy target.
    // Sized off this font's known 12x16px-per-glyph metrics at size 2
    // (both words are 4 characters, so one width covers both) rather
    // than a live measurement, since these are fixed literal strings,
    // not runtime data -- the actual draw call still centers each line
    // with a live t.textWidth() regardless, so a few px of slop here
    // just shows up as slightly uneven padding, never a layout break.
    bw = 70;
    bh = 48;
    bx = screenW - bw - 4;
    by = screenH - bh - 4;
}

// HUNT button, bottom-LEFT corner -- the mirror of moreInfoBtnRect()
// above, and for the same reason: the corners are the only spots
// guaranteed clear of the centered column of text on every rotation.
// Same 70x48 footprint so the two read as a matched pair, even though
// "HUNT" is one short line and doesn't need the height -- matching the
// neighbour matters more here than shrinking to fit the label.
static void huntBtnRect(int screenW, int screenH, int& bx, int& by, int& bw, int& bh) {
    bw = 70;
    bh = 48;
    bx = 4;
    by = screenH - bh - 4;
}

// Height of the coloured header strip the detection type is named in. File
// scope rather than a local in the draw function because the IGNORE button
// sits inside this strip and is centred in it -- a number two things have to
// agree on is not one either of them should own privately.
static const int STRIP_H = 42;

// IGNORE sits in the header strip, at its right-hand end. The bottom two
// corners are taken by HUNT and MORE INFO, and the strip's left-hand end is
// where the detection type is named, so this is the remaining spot that
// stays clear on the narrow rotations as well as the wide ones.
static void ignoreBtnRect(int screenW, int screenH, int& bx, int& by, int& bw, int& bh) {
    (void)screenH;
    // Deliberately smaller than HUNT and MORE INFO, which are 70x48 down
    // in the corners where nothing else goes. Up here it shares the header
    // strip with the target label, and the label's budget is measured off
    // this rect (see the STRIP_H block), so every pixel taken here is a
    // pixel taken from the longest type names.
    //
    // 62x28. Thicker than the 54x20 it replaced, and WIDER in step with it
    // rather than just taller: "IGNORE" is 36px at size 1, so inside the 2px
    // bevel 62x28 leaves 11px of air each side against 8 above and below.
    // Horizontal padding exceeding vertical is the proportion that makes a
    // push button look like one, and growing the height alone would have
    // inverted it -- a Win95 button is a wide, shallow thing (75x23 dialog
    // units, about 3.3:1), so getting chunkier has to mean getting bigger,
    // not getting squarer.
    //
    // The width is capped by the header label beside it, whose budget is
    // measured off this rect: on the 240px rotation that budget is 218-bw,
    // and every type name was measured in Bangers MD to find the real
    // ceiling. The longest that still fits the face is "CARD SKIMMER" at
    // 151px, so bw can reach 67 before it would push that one into the
    // built-in font. 62 leaves 5px of margin. The only two names over the
    // line -- "PROXIMITY BEACON" at 188 and "HACKER HARDWARE" at 187 --
    // were already stepping down at any width.
    bw = 62;
    bh = 28;
    // 8 from the edge rather than 4. The margin has to clear the BORDER, not
    // the screen: with a 3px frame drawn over the outermost columns, a 4px
    // margin left the button one pixel off it, which reads as a collision
    // rather than a gap. 8 leaves 5 clear of the widest frame.
    bx = screenW - bw - 8;
    // Centred in the strip rather than pinned 4px below its top edge, which
    // left 18 rows of dead colour underneath and made the button read as
    // stuck to the ceiling instead of sitting in a title bar. The strip is
    // the only thing behind it, so centring on the strip IS centring on
    // everything it overlaps -- and it is derived, so if STRIP_H ever moves
    // the button moves with it.
    by = (STRIP_H - bh) / 2;
}

bool uiAlertHitIgnore(int x, int y, int screenW, int screenH) {
    int bx, by, bw, bh;
    ignoreBtnRect(screenW, screenH, bx, by, bw, bh);
    // Padded outward. The button lost 4 rows becoming the right shape, and
    // a correctly proportioned button is no use if a resistive panel and a
    // fingertip cannot land on it -- so the drawn rect shrank and the
    // target grew. Nothing else is up here to steal a near miss from: the
    // slop runs into the screen edge on two sides and into the header
    // strip's own dead space on the others.
    const int SLOP = 6;
    return x >= bx - SLOP && x <= bx + bw + SLOP &&
           y >= by - SLOP && y <= by + bh + SLOP;
}

bool uiAlertHitHunt(int x, int y, int screenW, int screenH) {
    int bx, by, bw, bh;
    huntBtnRect(screenW, screenH, bx, by, bw, bh);
    return x >= bx && x <= bx + bw && y >= by && y <= by + bh;
}

bool uiAlertHitMoreInfo(int x, int y, int screenW, int screenH) {
    int bx, by, bw, bh;
    moreInfoBtnRect(screenW, screenH, bx, by, bw, bh);
    return x >= bx && x <= bx + bw && y >= by && y <= by + bh;
}

void uiAlertTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng,
                 bool infoPending, const char* infoTypeName, const char* infoText) {
    int w = t.width();
    int h = t.height();

    uint32_t elapsed = now - s_alertStart;
    // Capped well short of wrapping the uint8_t step counter -- an
    // alert would need to sit on screen for ~10+ minutes to get here,
    // at which point it's already been holding at the loudest level
    // for a long time and one more (or zero more) re-trigger makes no
    // visible difference.
    while (s_glitchStep < 200 && elapsed >= glitchStepOffsetMs(s_glitchStep)) {
        Theme::triggerGlitchBurst(glitchStepLevel(s_glitchStep));
        s_glitchStep++;
    }

    // The player's chosen background runs behind the alert, then the
    // themed detection FX layer on top of it without their own erase.
    // Dimmed on the way past: at full brightness a starfield or a fire
    // competes with the headline type for attention, and this screen has
    // to stay readable first. ALERT_SHOW_BACKGROUND is the single switch
    // if that turns out to be the wrong call -- false restores the flat
    // Theme::BG this screen had before.
    if (ALERT_SHOW_BACKGROUND) {
        // Two-stage knock-back, both cheap. The palette dim costs
        // nothing at all -- the background renderers read these globals,
        // so they simply draw darker -- but it only reaches colours that
        // come from the palette, and several backgrounds (the starfield's
        // tunnel rings and planets especially) pack their own literals.
        // The scanline pass catches whatever the palette could not.
        Theme::Palette saved = Theme::dimPaletteForOverlay(ALERT_PALETTE_DIM);
        Theme::drawActiveBackground(t, now, 0, h, eng);
        Theme::restorePalette(saved);
        Theme::dimRegion(t, 0, 0, w, h, ALERT_BACKGROUND_DIM);
    } else {
        t.fillRect(0, 0, w, h, Theme::BG);
    }

    // ---- header strip ------------------------------------------------
    // Replaces the pulsing border and the "!! DETECTION !!" headline with
    // one solid bar in the detection's own colour. Three things fall out of
    // that. The category is readable before any word is: HACKER and the
    // attack types come up red, cameras cyan, trackers purple. The border's
    // six pixels come back on all four sides. And "!! DETECTION !!" stops
    // spending the best row on the screen restating what the screen is.
    //
    // 42 rows because Bangers MD is a 33px cell and it has to sit inside.
    const uint16_t typeCol = Theme::colorFor(s_last.type);
    t.fillRect(0, 0, w, STRIP_H, typeCol);

    // Inset by the border width. The frame draws last and would otherwise
    // paint straight over the strip's lit top edge and both its side edges,
    // leaving only the bottom shadow -- which is not a bevel. The strip sits
    // INSIDE the frame, so its bevel has to as well.
    stripBevel(t, BORDER_W, BORDER_W, w - 2 * BORDER_W, STRIP_H - BORDER_W, typeCol);

    // The label in Bangers MD rather than LG: MD is narrower per glyph, and
    // the longest labels here ("PROXIMITY BEACON", "HACKER HARDWARE") are
    // longer than the "CARD SKIMMER" the LG sizing note was written for.
    // Measured anyway, with a fallback, because a label that runs off the
    // side of a 240px rotation is not something to find out on hardware.
    {
        const char* tgt = targetLabel(s_last.type);
        // The budget is the space left of the IGNORE button, NOT the screen
        // width. The button is drawn over this strip's right-hand end, so
        // measuring against w put "PROXIMITY BEACON" straight through it on
        // the 240px rotation -- which is exactly the sort of thing that only
        // shows up on the narrow board. Read from ignoreBtnRect rather than
        // restated, so the two cannot drift when the button is resized.
        int ibx, iby, ibw, ibh;
        ignoreBtnRect(w, h, ibx, iby, ibw, ibh);
        // The 6 is the gap between this label and the button, down from 8 to
        // pay for the button's new edge margin without shrinking the button
        // or pushing "CARD SKIMMER" -- the longest name that still fits the
        // Bangers face, at 151px -- into the built-in fallback. On the 240px
        // rotation the budget is now 154, so it clears by 3.
        const int avail = ibx - 10 - 6;
        const int tw = Theme::bangersTextWidth(tgt, Theme::BangersSize::MD);
        if (tw <= avail) {
            Theme::drawBangersText(t, 10, 4, tgt, Theme::BG, Theme::BangersSize::MD);
        } else {
            // No smaller Bangers exists, so step down through the built-in
            // font rather than clip.
            t.setTextSize(2);
            if (t.textWidth(tgt) > avail) t.setTextSize(1);
            t.setTextColor(Theme::BG, typeCol);
            t.setCursor(10, (STRIP_H - t.fontHeight()) / 2);
            t.print(tgt);
        }
    }

    // Just the grade, not the old "~60%": that was a number invented to
    // sound precise, and the grade is what the ALERT FILTER gates on.
    Confidence conf = s_last.conf;
    uint16_t confColor = (conf == Confidence::HIGH_CONF) ? Theme::GREEN
                        : (conf == Confidence::MED_CONF) ? Theme::AMBER
                        : Theme::RED;

    // ---- data plate ----------------------------------------------------
    // A solid ground with a hairline in the type's colour. The background
    // still runs behind and around it, so the screen keeps its character,
    // but six-pixel text stops competing with a sunset gradient -- which
    // was the single worst thing about the old layout.
    // Two columns when there is width for them: identity on the left,
    // the gauge on the right. The 240px rotation cannot hold both, so it
    // keeps the full-width plate and puts a smaller gauge underneath.
    const bool wide = (w >= 300);
    const int PLATE_X = wide ? 12 : 14;
    const int PLATE_Y = STRIP_H + 12;
    const int PLATE_W = wide ? 168 : (w - 28);
    const int PLATE_H = 98;
    t.fillRect(PLATE_X, PLATE_Y, PLATE_W, PLATE_H, Theme::BG);
    t.drawRect(PLATE_X, PLATE_Y, PLATE_W, PLATE_H, typeCol);

    // Vendor in Bangers, upper-cased. Bangers carries A-Z, 0-9, space, '!'
    // and (since the hyphen was added for exactly this) '-'. Anything else
    // is SKIPPED by drawBangersText with no advance, so "DroneID" would
    // come out "DID" -- upper-casing is what makes the vendor strings in
    // signatures.cpp renderable at all.
    {
        const char* src = s_redacted ? "LOCKED" : s_last.vendor;
        char up[sizeof(s_last.vendor)];
        size_t i = 0;
        for (; src[i] && i + 1 < sizeof(up); i++) {
            const char c = src[i];
            up[i] = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
        }
        up[i] = '\0';
        const int vw = Theme::bangersTextWidth(up, Theme::BangersSize::MD);
        if (vw > 0 && vw <= PLATE_W - 8) {
            Theme::drawBangersText(t, PLATE_X + (PLATE_W - vw) / 2, PLATE_Y + 2, up,
                                   Theme::CYAN, Theme::BangersSize::MD);
        } else {
            t.setTextSize(2);
            t.setTextColor(Theme::CYAN, Theme::BG);
            t.setCursor(PLATE_X + (PLATE_W - t.textWidth(src)) / 2, PLATE_Y + 10);
            t.print(src);
        }
    }

    // The device's own name, where it has one -- a Flipper's nickname, a
    // Pwnagotchi's, a drone's serial, an iBeacon's deployment. Carried in
    // Detection all along and never drawn on this screen.
    t.setTextSize(1);
    t.setTextColor(Theme::WHITE, Theme::BG);
    if (s_last.name[0] && !s_redacted) {
        t.setCursor(PLATE_X + (PLATE_W - t.textWidth(s_last.name)) / 2, PLATE_Y + 38);
        t.print(s_last.name);
    }

    char mac[24];
    if (s_redacted)
        snprintf(mac, sizeof(mac), "%s", "-- LOCKED --");
    else
        snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
                 s_last.mac[0], s_last.mac[1], s_last.mac[2],
                 s_last.mac[3], s_last.mac[4], s_last.mac[5]);
    t.setCursor(PLATE_X + (PLATE_W - t.textWidth(mac)) / 2, PLATE_Y + 50);
    t.print(mac);

    // Signal as a bar as well as a number: -90 dBm empty, -40 full. The
    // number is for the log; the bar is what reads from across a room.
    {
        const int BAR_X = PLATE_X + 8, BAR_Y = PLATE_Y + 72;
        const int BAR_W = PLATE_W - 16, BAR_H = 12;
        t.setTextColor(Theme::CYAN, Theme::BG);
        // Label above the bar rather than beside it: the two-column layout
        // leaves the plate 168 wide, and a label plus a usable meter do not
        // both fit on one line at that width. The grade shares that row,
        // right-aligned -- which is also what takes it out of the readout
        // line below, where the four fields together ran 174px into a 168px
        // plate and pushed the sighting count off the edge.
        t.setCursor(PLATE_X + 8, BAR_Y - 12);
        t.print("SIGNAL");
        t.setTextColor(confColor, Theme::BG);
        const char* cl = confidenceLabel(conf);
        t.setCursor(PLATE_X + PLATE_W - 8 - t.textWidth(cl), BAR_Y - 12);
        t.print(cl);
        int v = s_last.rssi;
        if (v < -90) v = -90;
        if (v > -40) v = -40;
        const int fill = (v + 90) * (BAR_W - 2) / 50;
        t.drawRect(BAR_X, BAR_Y, BAR_W, BAR_H, Theme::CYAN);
        if (fill > 0) t.fillRect(BAR_X + 1, BAR_Y + 1, fill, BAR_H - 2, Theme::CYAN);
    }

    // The numbers, without the grade -- that moved up to the SIGNAL row so
    // this line fits the narrower plate.
    char info[40];
    snprintf(info, sizeof(info), "%d dBm   CH %u   x%u",
             s_last.rssi, s_last.channel, (unsigned)s_last.hits);
    t.setTextColor(confColor, Theme::BG);
    t.setCursor(PLATE_X + (PLATE_W - t.textWidth(info)) / 2, PLATE_Y + PLATE_H - 11);
    t.print(info);

    // ---- the gauge -----------------------------------------------------
    // The detected thing, drawn large with the instrument grid over the top
    // of it. The old screen had a 22px radar tucked under the readout and a
    // separate icon at the bottom of the panel, and the two never met -- one
    // said "a contact is out there", the other said "this is what it is".
    // One object, one gauge.
    //
    // Deliberately NOT viewer-centred with the icon riding a bearing: the
    // bearing is derived from the MAC so it holds still during an alert, and
    // it is otherwise arbitrary. One antenna cannot do direction finding, and
    // putting a recognisable object at a compass position would claim it can.
    // Distance is honest -- RSSI really does map to a ring -- so that is what
    // the rings show, with the object at the centre of its own field.
    {
        // The band the gauge actually owns: below the header strip, above
        // the buttons. Centring it on the PLATE instead put its well three
        // pixels into the strip -- the plate starts 12 rows below the strip,
        // so a circle centred on the plate is not centred on the gap.
        const int ceilY  = STRIP_H + 4;
        const int floorY = h - 56;
        const int gx = wide ? (PLATE_X + PLATE_W + (w - PLATE_X - PLATE_W) / 2)
                            : (w / 2);
        const int gy = wide ? ((ceilY + floorY) / 2)
                            : (PLATE_Y + PLATE_H + 30);
        // Bounded by whichever runs out first: the width beside the plate,
        // or the height of the band -- and the well is 4px larger than the
        // radius on every side, so that is what has to fit, not the circle.
        int gr = wide ? ((w - PLATE_X - PLATE_W) / 2 - 6) : 26;
        if (gy - gr - 4 < ceilY)  gr = gy - ceilY - 4;
        if (gy + gr + 4 > floorY) gr = floorY - gy - 4;
        if (gr > 60) gr = 60;
        if (gr > 8) {
            t.fillRect(gx - gr - 4, gy - gr - 4, (gr + 4) * 2, (gr + 4) * 2, Theme::BG);
            t.drawRect(gx - gr - 4, gy - gr - 4, (gr + 4) * 2, (gr + 4) * 2, typeCol);
            Theme::drawTypeIcon(t, s_last.type, gx, gy, gr / 2);
            // Grid over the object, not under it.
            t.drawCircle(gx, gy, gr,         t.color565(0, 90, 86));
            t.drawCircle(gx, gy, gr * 2 / 3, t.color565(0, 64, 60));
            t.drawCircle(gx, gy, gr / 3,     t.color565(0, 48, 45));
            t.drawFastHLine(gx - gr, gy, 2 * gr, t.color565(0, 40, 38));
            t.drawFastVLine(gx, gy - gr, 2 * gr, t.color565(0, 40, 38));
            const float sweep = (float)(now % 2000) / 2000.0f * 6.2831853f;
            t.drawLine(gx, gy, gx + (int)(sinf(sweep) * gr),
                       gy - (int)(cosf(sweep) * gr), Theme::GREEN);
            t.drawCircle(gx, gy, gr, Theme::VAPOR_PINK);
        }
    }

    // MORE INFO -- opens the same explanation panel LOG's long-press
    // menu does (see uiAlertHitMoreInfo()), so a fresh detection can be
    // looked up without having to remember to go find it in LOG after.
    // "MORE"/"INFO" stacked on two lines (see moreInfoBtnRect()'s
    // comment) rather than Theme::drawButton()'s usual single-line
    // label, so this button doesn't need to go wide to stay legible.
    // IGNORE: stops this exact device raising the alert again. Labelled on
    // two lines like its neighbours so all three buttons match.
    {
        int bx, by, bw, bh;
        ignoreBtnRect(w, h, bx, by, bw, bh);
        const bool already = IgnoreList::contains(s_last.mac);
        // A real system button rather than the vaporwave chrome the rest of
        // the device wears. It is the one control on this screen that is not
        // about the detection -- it changes what the firmware will do from
        // now on -- and looking like it was lifted out of a system dialog is
        // how that reads at a glance.
        //
        // MUTED is the SAME button held down, not a differently coloured
        // one. Win95 had a word for a button that stays in after you let go
        // and this is it, so the state costs no new colour, no legend and
        // nothing to learn: the thing is pushed in, and pushed in means done.
        Theme::drawWin95Button(t, bx, by, bw, bh,
                               already ? "MUTED" : "IGNORE", already);
    }

    {
        int bx, by, bw, bh;
        moreInfoBtnRect(w, h, bx, by, bw, bh);
        t.fillRect(bx, by, bw, bh, Theme::BG);
        t.drawRect(bx, by, bw, bh, Theme::PURPLE);
        t.setTextSize(2);
        t.setTextColor(Theme::CYAN, Theme::BG);
        t.setTextWrap(false);
        int lineH = t.fontHeight();
        const int lineGap = 3;
        int ty = by + (bh - (lineH * 2 + lineGap)) / 2;
        int mw = t.textWidth("MORE");
        t.setCursor(bx + (bw - mw) / 2, ty);
        t.print("MORE");
        int iw = t.textWidth("INFO");
        t.setCursor(bx + (bw - iw) / 2, ty + lineH + lineGap);
        t.print("INFO");
    }

    // HUNT -- starts tracking this exact device straight from the alert,
    // the same engine.huntBle()/huntWifi() call LOG's long-press confirm
    // panel makes. Without it, chasing a device you were just warned
    // about meant dismissing the alert, opening LOG, finding the row
    // again and long-pressing it -- by which point a moving target may
    // already be out of range. Drawn in AMBER rather than the CYAN
    // MORE INFO uses: this one changes what the device is doing, the
    // other only opens a text panel.
    {
        int bx, by, bw, bh;
        huntBtnRect(w, h, bx, by, bw, bh);
        t.fillRect(bx, by, bw, bh, Theme::BG);
        t.drawRect(bx, by, bw, bh, Theme::PURPLE);
        t.setTextSize(2);
        t.setTextColor(Theme::AMBER, Theme::BG);
        t.setTextWrap(false);
        int hw = t.textWidth("HUNT");
        t.setCursor(bx + (bw - hw) / 2, by + (bh - t.fontHeight()) / 2);
        t.print("HUNT");
    }

    // TV-static snow over the whole screen during the same random burst
    // the Bangers headline text above already glitches on -- drawn
    // last so it overlays everything, including the MAC/RSSI readout;
    // briefly obscuring the readout mid-burst reads as "interference"
    // rather than a bug, which fits an alert about surveillance gear.
    Theme::drawGlitchStatic(t, 0, 0, w, h);

    // The RGB frame, after the static so the chrome stays clean: the static
    // is meant to read as interference in the CONTENT, and a frame that
    // breaks up along with it stops reading as a frame at all.
    drawRgbBorder(t, w, h, now);

    // Info panel drawn last, opaquely on top of everything above
    // (including the static) -- same "modal drawn every tick on top of
    // a screen that keeps rendering underneath" pattern LOG's confirm/
    // info panels use.
    if (infoPending) Theme::drawInfoPanel(t, w, h, now, infoTypeName, infoText);
}

bool uiAlertTouched() {
    return s_touched;
}

// Hook called by main when it polls touch during ALERT.
void uiAlertNoteTouch() { s_touched = true; }
