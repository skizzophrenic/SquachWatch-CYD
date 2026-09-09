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

// IGNORE sits in the top-right. The bottom two corners are taken by HUNT
// and MORE INFO, and the top-left is where the "!! DETECTION !!" headline
// starts, so this is the remaining corner that stays clear on the narrow
// rotations as well as the wide ones.
static void ignoreBtnRect(int screenW, int screenH, int& bx, int& by, int& bw, int& bh) {
    (void)screenH;
    // Deliberately smaller than HUNT and MORE INFO, which are 70x48 down
    // in the corners where nothing else goes. Up here it shares a row with
    // the "!! DETECTION !!" headline: that is ~166px of Bangers LG, centred,
    // so on the 240px rotation it runs to x=203 and a 70px button starting
    // at x=166 sits right on top of it. At 50x24 with size-1 text the
    // button starts at x=186 and the headline clears it.
    bw = 50;
    bh = 24;
    bx = screenW - bw - 4;
    by = 4;
}

bool uiAlertHitIgnore(int x, int y, int screenW, int screenH) {
    int bx, by, bw, bh;
    ignoreBtnRect(screenW, screenH, bx, by, bw, bh);
    return x >= bx && x <= bx + bw && y >= by && y <= by + bh;
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
        Theme::drawAlertFx(t, s_last.type, now, w, h, false);
    } else {
        Theme::drawAlertFx(t, s_last.type, now, w, h);
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
    const int STRIP_H = 42;
    t.fillRect(0, 0, w, STRIP_H, typeCol);

    // The label in Bangers MD rather than LG: MD is narrower per glyph, and
    // the longest labels here ("PROXIMITY BEACON", "HACKER HARDWARE") are
    // longer than the "CARD SKIMMER" the LG sizing note was written for.
    // Measured anyway, with a fallback, because a label that runs off the
    // side of a 240px rotation is not something to find out on hardware.
    {
        const char* tgt = targetLabel(s_last.type);
        // The budget is the space left of the IGNORE button, NOT the screen
        // width. IGNORE sits at (w-54, 4) and is drawn over this strip, so
        // measuring against w put "PROXIMITY BEACON" straight through it on
        // the 240px rotation -- which is exactly the sort of thing that only
        // shows up on the narrow board.
        int ibx, iby, ibw, ibh;
        ignoreBtnRect(w, h, ibx, iby, ibw, ibh);
        const int avail = ibx - 10 - 8;
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
    const int PLATE_X = 14, PLATE_Y = STRIP_H + 12;
    const int PLATE_W = w - 28;
    const int PLATE_H = 100;
    t.fillRect(PLATE_X, PLATE_Y, PLATE_W, PLATE_H, Theme::BG);
    t.drawRect(PLATE_X, PLATE_Y, PLATE_W, PLATE_H, typeCol);

    // Vendor in Bangers, upper-cased. Bangers carries A-Z, 0-9, space, '!'
    // and (since the hyphen was added for exactly this) '-'. Anything else
    // is SKIPPED by drawBangersText with no advance, so "DroneID" would
    // come out "DID" -- upper-casing is what makes the vendor strings in
    // signatures.cpp renderable at all.
    {
        char up[sizeof(s_last.vendor)];
        size_t i = 0;
        for (; s_last.vendor[i] && i + 1 < sizeof(up); i++) {
            const char c = s_last.vendor[i];
            up[i] = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
        }
        up[i] = '\0';
        const int vw = Theme::bangersTextWidth(up, Theme::BangersSize::MD);
        if (vw > 0 && vw <= PLATE_W - 8) {
            Theme::drawBangersText(t, (w - vw) / 2, PLATE_Y + 4, up,
                                   Theme::CYAN, Theme::BangersSize::MD);
        } else {
            t.setTextSize(2);
            t.setTextColor(Theme::CYAN, Theme::BG);
            t.setCursor((w - t.textWidth(s_last.vendor)) / 2, PLATE_Y + 10);
            t.print(s_last.vendor);
        }
    }

    // The device's own name, where it has one -- a Flipper's nickname, a
    // Pwnagotchi's, a drone's serial, an iBeacon's deployment. Carried in
    // Detection all along and never drawn on this screen.
    t.setTextSize(1);
    t.setTextColor(Theme::WHITE, Theme::BG);
    if (s_last.name[0]) {
        t.setCursor((w - t.textWidth(s_last.name)) / 2, PLATE_Y + 40);
        t.print(s_last.name);
    }

    char mac[24];
    snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
             s_last.mac[0], s_last.mac[1], s_last.mac[2],
             s_last.mac[3], s_last.mac[4], s_last.mac[5]);
    t.setCursor((w - t.textWidth(mac)) / 2, PLATE_Y + 56);
    t.print(mac);

    // Signal as a bar as well as a number: -90 dBm empty, -40 full. The
    // number is for the log; the bar is what reads from across a room.
    {
        const int BAR_X = PLATE_X + 60, BAR_Y = PLATE_Y + 72;
        const int BAR_W = PLATE_W - 74, BAR_H = 10;
        t.setTextColor(Theme::CYAN, Theme::BG);
        t.setCursor(PLATE_X + 10, BAR_Y + 1);
        t.print("SIGNAL");
        int v = s_last.rssi;
        if (v < -90) v = -90;
        if (v > -40) v = -40;
        const int fill = (v + 90) * (BAR_W - 2) / 50;
        t.drawRect(BAR_X, BAR_Y, BAR_W, BAR_H, Theme::CYAN);
        if (fill > 0) t.fillRect(BAR_X + 1, BAR_Y + 1, fill, BAR_H - 2, Theme::CYAN);
    }

    // Grade, signal, channel, sightings -- one line, already drawn in the
    // grade's own colour. It used to sit in the strip, where it collided
    // with both the title and the IGNORE button on a narrow rotation, and
    // it belongs with the numbers anyway.
    char info[56];
    snprintf(info, sizeof(info), "%s   %d dBm   CH %u   x%u",
             confidenceLabel(conf), s_last.rssi, s_last.channel,
             (unsigned)s_last.hits);
    t.setTextColor(confColor, Theme::BG);
    t.setCursor((w - t.textWidth(info)) / 2, PLATE_Y + PLATE_H - 12);
    t.print(info);

    // Signal radar: bearing is derived from the MAC so it stays put for
    // the duration of this alert instead of jittering every frame;
    // distance from centre reflects RSSI (closer = stronger signal).
    float bearing = (float)((s_last.mac[4] ^ (s_last.mac[5] << 3)) & 0xFF)
                    / 255.0f * 6.2831853f;
    Theme::drawSignalRadar(t, w / 2, PLATE_Y + PLATE_H + 26, 20, now, s_last.rssi, bearing);

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
        Theme::drawButton(t, bx, by, bw, bh, "", false);
        // One line at size 1 now it is short enough to fit: "IGNORE" is
        // 36px at this size inside a 50px button, so it no longer needs
        // hyphenating across two rows the way the 70px version did.
        t.setTextSize(1);
        t.setTextColor(already ? Theme::GREEN : Theme::AMBER);
        const char* lbl = already ? "MUTED" : "IGNORE";
        t.setCursor(bx + (bw - t.textWidth(lbl)) / 2, by + 9);
        t.print(lbl);
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
