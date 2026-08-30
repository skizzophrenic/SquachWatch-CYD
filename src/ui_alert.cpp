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

// Picks the largest text size (up to maxSize) that fits within maxW,
// so the giant alert strings never overflow into a mid-word wrap on
// the narrower 240px portrait width (some, like "CARD SKIMMER", even
// overflow 320px landscape at size 4).
static int fitTextSize(TFT_eSPI& t, const char* s, int maxW, int maxSize) {
    for (int sz = maxSize; sz >= 1; sz--) {
        t.setTextSize(sz);
        if (t.textWidth(s) <= maxW) return sz;
    }
    return 1;
}

static Detection s_last;
static bool s_touched = false;

void uiAlertInit(TFT_eSPI& t, const Detection& d) {
    s_last = d;
    s_touched = false;
    // fillScreen() relies on TFT_eSPI's base-class width/height, which
    // TFT_eSprite::createSprite() never updates — it leaves stale
    // remnants of whatever screen was drawn before when t is a sprite.
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
}

void uiAlertTick(TFT_eSPI& t, uint32_t now) {
    int w = t.width();
    int h = t.height();

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
        t.fillRect(tx - 2, 4, tw + 4, 38, Theme::BG);
        if (((now / 200) % 2) == 0) {
            Theme::drawBangersText(t, tx, 6, title, Theme::PINK, Theme::BangersSize::LG);
        }
    }

    // Target type (giant, but shrinks to fit the longer labels like
    // "CARD SKIMMER" on narrower screens)
    const char* tgt = targetLabel(s_last.type);
    fitTextSize(t, tgt, w - 8, 4);
    t.setTextColor(Theme::VAPOR_PINK, Theme::BG);
    int t2 = t.textWidth(tgt);
    t.setCursor((w - t2) / 2, 60);
    t.print(tgt);

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
}

bool uiAlertTouched() {
    return s_touched;
}

// Hook called by main when it polls touch during ALERT.
void uiAlertNoteTouch() { s_touched = true; }
