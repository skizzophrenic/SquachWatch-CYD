// SquachWatch-CYD — ALERT screen implementation
#include "ui_alert.h"
#include "theme.h"
#include "signatures.h"
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
        default:                     return "UNKNOWN";
    }
}

static Detection s_last;
static bool s_touched = false;

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

bool uiAlertHitMoreInfo(int x, int y, int screenW, int screenH) {
    int bx, by, bw, bh;
    moreInfoBtnRect(screenW, screenH, bx, by, bw, bh);
    return x >= bx && x <= bx + bw && y >= by && y <= by + bh;
}

void uiAlertTick(TFT_eSPI& t, uint32_t now,
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

    // Ambient background, themed to what was actually detected — draws
    // first since it fully repaints the whole w x h region every call,
    // same as the CLEAR-screen backgrounds. Everything below (border,
    // text) draws on top of it.
    Theme::drawAlertFx(t, s_last.type, now, w, h);

    // Pulsing border (6 px, PINK <-> VAPOR_PINK)
    Theme::drawPulsingBorder(t, now, Theme::VAPOR_PINK, Theme::PINK, 6);

    // !! DETECTION !! (blink at 200ms on / 200ms off) — Bangers comic-
    // impact font, the bigger of the two baked-in sizes (same one the
    // boot splash uses). ~166px wide at this size, comfortably under
    // the narrowest (240px) screen width, so unlike the target-label
    // line below this one never needs a fallback/shrink path. The
    // region is cleared every frame (not just re-drawn when "on")
    // since the sparse glyph renderer only paints ink pixels, not a
    // full opaque cell the way t.print() does — without an explicit
    // clear the "off" half of the blink would never actually go away.
    {
        const char* title = "!! DETECTION !!";
        int tw = Theme::bangersTextWidth(title, Theme::BangersSize::LG);
        int tx = (w - tw) / 2;
        t.fillRect(tx - 2, 6, tw + 4, 38, Theme::BG);
        if (((now / 200) % 2) == 0) {
            Theme::drawBangersText(t, tx, 8, title, Theme::PINK, Theme::BangersSize::LG);
        }
    }

    // Target type — same Bangers LG face as "!! DETECTION !!" above,
    // so the two headline lines on this screen read as one voice
    // instead of one comic-impact line over one library-default line.
    // Widest label ("CARD SKIMMER") is ~173px at this size, still well
    // under the narrowest (240px) screen width, so no shrink fallback
    // needed here either.
    {
        const char* tgt = targetLabel(s_last.type);
        int tw2 = Theme::bangersTextWidth(tgt, Theme::BangersSize::LG);
        Theme::drawBangersText(t, (w - tw2) / 2, 50, tgt, Theme::VAPOR_PINK, Theme::BangersSize::LG);
    }

    // How sure we actually are — see docs/DETECTIONS.md. This is the
    // whole point of showing it here: a Medium/Low reading should look
    // visibly less certain than a High one, not get the same treatment.
    Confidence conf = confidenceFor(s_last.type);
    uint16_t confColor = (conf == Confidence::HIGH_CONF) ? Theme::GREEN
                        : (conf == Confidence::MED_CONF) ? Theme::AMBER
                        : Theme::RED;
    char confBuf[32];
    snprintf(confBuf, sizeof(confBuf), "%s  ~%u%%",
             confidenceLabel(conf), confidencePercent(conf));
    t.setTextSize(1);
    t.setTextColor(confColor, Theme::BG);
    int ccw = t.textWidth(confBuf);
    t.setCursor((w - ccw) / 2, 94);
    t.print(confBuf);

    // Vendor
    t.setTextSize(2);
    t.setTextColor(Theme::CYAN, Theme::BG);
    int vw = t.textWidth(s_last.vendor);
    t.setCursor((w - vw) / 2, 110);
    t.print(s_last.vendor);

    // MAC
    t.setTextSize(1);
    t.setTextColor(Theme::WHITE, Theme::BG);
    char mac[24];
    snprintf(mac, sizeof(mac), "MAC: %02X:%02X:%02X:%02X:%02X:%02X",
             s_last.mac[0], s_last.mac[1], s_last.mac[2],
             s_last.mac[3], s_last.mac[4], s_last.mac[5]);
    int mw = t.textWidth(mac);
    t.setCursor((w - mw) / 2, 140);
    t.print(mac);

    // RSSI / channel
    char info[32];
    snprintf(info, sizeof(info), "RSSI: %d dBm   CH: %u", s_last.rssi, s_last.channel);
    int iw = t.textWidth(info);
    t.setCursor((w - iw) / 2, 160);
    t.print(info);

    // Signal radar: bearing is derived from the MAC so it stays put
    // for the duration of this alert instead of jittering every frame;
    // distance from center reflects RSSI (closer = stronger signal).
    float bearing = (float)((s_last.mac[4] ^ (s_last.mac[5] << 3)) & 0xFF)
                    / 255.0f * 6.2831853f;
    Theme::drawSignalRadar(t, w / 2, 192, 22, now, s_last.rssi, bearing);

    // Glitchy wordmark
    Theme::drawGlitchText(t, 220, "SQUACHWATCH", Theme::VAPOR_PINK, now);

    // MORE INFO -- opens the same explanation panel LOG's long-press
    // menu does (see uiAlertHitMoreInfo()), so a fresh detection can be
    // looked up without having to remember to go find it in LOG after.
    // "MORE"/"INFO" stacked on two lines (see moreInfoBtnRect()'s
    // comment) rather than Theme::drawButton()'s usual single-line
    // label, so this button doesn't need to go wide to stay legible.
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
