// SquachWatch-CYD — ALERT screen implementation
#include "ui_alert.h"
#include "theme.h"
#include "signatures.h"
#include "squachy.h"
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

// Which margin the running cameo is anchored to right now -- left or
// right, clear of every centered element on this screen (title, target
// label, confidence, vendor, MAC, RSSI, radar, wordmark). The widest of
// those ("CARD SKIMMER" at Bangers LG) is ~173px, which on the
// narrowest 240px rotation leaves only x<33 / x>207 actually clear --
// MARGIN_CX sits well inside that with room for the small dart range
// tick() adds on top, so he can run freely without ever needing to
// know where any specific line of text is. Only the SIDE changes on a
// glitch beat (see the while loop below); his vertical position is a
// continuous up-down patrol (see patrolTopY()) computed fresh every
// frame, not another once-per-beat jump -- a single hard jump per beat
// read as teleporting rather than running, since there was nothing
// visibly moving in between.
static const int MARGIN_CX    = 22;
static const int MARGIN_DART  = 10;
static int       s_camCx = MARGIN_CX;

static int patrolTopY(uint32_t now) {
    const uint32_t PATROL_MS = 4000;
    const int PATROL_LO = 4, PATROL_HI = 154;
    float t01 = (float)(now % PATROL_MS) / (float)PATROL_MS;
    float tri = (t01 < 0.5f) ? (t01 * 2.0f) : (2.0f - t01 * 2.0f); // 0->1->0, no snap at the wrap
    return PATROL_LO + (int)(tri * (float)(PATROL_HI - PATROL_LO));
}

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
    s_camCx = (random(0, 2) == 0) ? MARGIN_CX : (t.width() - MARGIN_CX);
    Squachy::alertReaction(d.type);
    // fillScreen() relies on TFT_eSPI's base-class width/height, which
    // TFT_eSprite::createSprite() never updates — it leaves stale
    // remnants of whatever screen was drawn before when t is a sprite.
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
}

void uiAlertTick(TFT_eSPI& t, uint32_t now) {
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
        // Re-fires his panic in lockstep with each escalation step so
        // he stays visibly freaked out for as long as the screen does,
        // instead of settling back to idle a second or two in.
        Squachy::alertReaction(s_last.type);
        // Switches which margin he's running along -- his vertical
        // patrol (see patrolTopY()) keeps flowing continuously through
        // this, so only the side jumps, on the same beat the screen
        // tears/glitches anyway.
        s_camCx = (random(0, 2) == 0) ? MARGIN_CX : (w - MARGIN_CX);
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

    // Small SHOCKED cameo, drawn on top of everything above (title,
    // target label, confidence, vendor, MAC, RSSI, radar, wordmark) so
    // he never disappears behind any of it, but confined to the left/
    // right margins (see s_camCx's comment) so being on top never
    // means covering anything actually worth reading. Continuously
    // patrolling up and down (patrolTopY()) plus a small horizontal
    // dart (wanderRangePx) the whole time he's on screen, not just at
    // the moment his margin switches, so there's always visible motion
    // between beats instead of a jump with nothing happening in between.
    Squachy::tick(t, s_camCx, patrolTopY(now), 56, now, true, 0.4f, false, MARGIN_DART);

    // TV-static snow over the whole screen during the same random burst
    // the Bangers headline text above already glitches on -- drawn
    // last so it overlays everything, including the MAC/RSSI readout;
    // briefly obscuring the readout mid-burst reads as "interference"
    // rather than a bug, which fits an alert about surveillance gear.
    Theme::drawGlitchStatic(t, 0, 0, w, h);
}

bool uiAlertTouched() {
    return s_touched;
}

// Hook called by main when it polls touch during ALERT.
void uiAlertNoteTouch() { s_touched = true; }
