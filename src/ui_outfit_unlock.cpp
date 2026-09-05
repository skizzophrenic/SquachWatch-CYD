// SquachWatch-CYD — OUTFIT UNLOCKED celebration popup implementation
#include "ui_outfit_unlock.h"
#include "theme.h"
#include "squachy.h"
#include "detection.h"
#include <Arduino.h>

static uint8_t  s_outfitIdx = 0;
static uint32_t s_openedAt  = 0;

// Long enough that a touch still settling from whatever screen was up
// when this opened cannot dismiss it, short enough that a player who
// wants to move on is not held hostage.
static const uint32_t MIN_ON_SCREEN_MS = 900;

// Reveal timings, all measured from s_openedAt.
static const uint32_t RISE_MS = 420;   // panel scales open
static const uint32_t TEXT_MS = 700;   // headline letters land, one by one

void uiOutfitUnlockInit(TFT_eSPI& t, uint8_t outfitIdx) {
    s_outfitIdx = outfitIdx;
    s_openedAt  = millis();
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
    // Open loud: the same shared burst every glitch-aware draw call
    // reads from, at the top intensity, so the popup arrives with a
    // tear rather than fading politely in.
    Theme::triggerGlitchBurst(4);
}

bool uiOutfitUnlockDismissable(uint32_t now) {
    return (now - s_openedAt) >= MIN_ON_SCREEN_MS;
}

// Rainbow ramp for the headline. Hand-rolled rather than pulling in a
// full HSV conversion: six linear segments around the wheel is all a
// rainbow sweep needs, and this stays integer until the final pack.
static uint16_t rainbowAt(TFT_eSPI& t, float h) {
    h -= floorf(h);
    const float x = h * 6.0f;
    const int   i = (int)x;
    const uint8_t f = (uint8_t)((x - (float)i) * 255.0f);
    switch (i) {
        case 0:  return t.color565(255, f, 0);
        case 1:  return t.color565((uint8_t)(255 - f), 255, 0);
        case 2:  return t.color565(0, 255, f);
        case 3:  return t.color565(0, (uint8_t)(255 - f), 255);
        case 4:  return t.color565(f, 0, 255);
        default: return t.color565(255, 0, (uint8_t)(255 - f));
    }
}

// One headline line, drawn per-letter so each glyph gets its own colour
// off the rainbow and its own glitch offset. The chromatic pass (black
// shadow, then cyan left / magenta right) is the same treatment the boot
// splash's subtitle uses, so the two read as the same typographic voice.
//
// `reveal` is 0..1 — letters land left to right as it climbs, which is
// what makes this an entrance rather than a static banner.
static void drawRainbowHeadline(TFT_eSPI& t, int cx, int y, const char* s,
                                uint32_t now, float reveal) {
    const Theme::BangersSize SZ = Theme::BangersSize::MD;
    const int total = Theme::bangersTextWidth(s, SZ);
    int x = cx - total / 2;

    const int  n       = (int)strlen(s);
    const bool glitchy = Theme::glitchActive();
    const float hueT   = (float)now / 1400.0f;

    for (int i = 0; i < n; i++) {
        const char ch[2] = { s[i], 0 };
        const int adv = Theme::bangersTextWidth(ch, SZ);
        if (s[i] == ' ') { x += adv; continue; }

        // Letters arrive in order; the one currently landing drops in
        // from above rather than popping, so the line assembles.
        const float at = (float)i / (float)(n > 1 ? n - 1 : 1);
        float land = (reveal - at * 0.55f) / 0.45f;
        if (land <= 0.0f) { x += adv; continue; }
        if (land > 1.0f) land = 1.0f;
        const int drop = (int)((1.0f - land) * -22.0f);

        int jx = 0, jy = 0;
        if (glitchy) {
            // Deterministic per-letter hash rather than random(): this
            // line is drawn several times per frame at different
            // offsets, and independent dice would tear the passes apart.
            const uint32_t hsh = (uint32_t)(now / 45) * 2654435761u + (uint32_t)i * 40503u;
            jx = (int)((hsh >> 3) % 3) - 1;
            jy = (int)((hsh >> 11) % 3) - 1;
        }
        const int gx = x + jx;
        const int gy = y + drop + jy;

        // Shadow first, then the chromatic fringe, then the rainbow
        // core on top -- the same order the boot splash's subtitle uses.
        Theme::drawBangersText(t, gx + 2, gy + 2, ch, Theme::BLACK, SZ);
        if (glitchy) {
            Theme::drawBangersText(t, gx - 1, gy, ch, Theme::CYAN, SZ);
            Theme::drawBangersText(t, gx + 1, gy, ch, Theme::VAPOR_PINK, SZ);
        }
        Theme::drawBangersText(t, gx, gy, ch,
                               rainbowAt(t, hueT + (float)i * 0.085f), SZ);
        x += adv;
    }
}

void uiOutfitUnlockTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng) {
    const int w = t.width();
    const int h = t.height();
    const uint32_t elapsed = now - s_openedAt;

    // Keep the celebration glitching the whole time it is up, rather
    // than only on the burst that opened it.
    static uint32_t s_nextBurst = 0;
    if (now >= s_nextBurst) {
        Theme::triggerGlitchBurst(2);
        s_nextBurst = now + 2600;
    }

    // The player's own background, dimmed hard: this screen is a
    // portrait of the new costume, so everything behind it is texture.
    Theme::Palette saved = Theme::dimPaletteForOverlay(170);
    Theme::drawActiveBackground(t, now, 0, h, eng);
    Theme::restorePalette(saved);
    Theme::dimRegion(t, 0, 0, w, h, 128);

    // Panel scales open from the middle. Drawn as a border pair rather
    // than a fill so the background keeps moving inside it.
    float rise = (float)elapsed / (float)RISE_MS;
    if (rise > 1.0f) rise = 1.0f;
    // Ease out, and overshoot slightly before settling.
    const float eased = 1.0f - (1.0f - rise) * (1.0f - rise);
    const int panelH = (int)((float)(h - 12) * eased);
    const int panelY = h / 2 - panelH / 2;
    if (panelH < 8) return;

    t.drawRect(4, panelY, w - 8, panelH, Theme::VAPOR_PURPLE);
    t.drawRect(5, panelY + 1, w - 10, panelH - 2, Theme::PURPLE);
    Theme::drawPulsingBorder(t, now, Theme::VAPOR_PINK, Theme::CYAN, 3);
    if (rise < 1.0f) return;               // still opening; nothing inside yet

    // ---- headline --------------------------------------------------------
    float reveal = (float)(elapsed - RISE_MS) / (float)TEXT_MS;
    if (reveal < 0.0f) reveal = 0.0f;
    if (reveal > 1.0f) reveal = 1.0f;

    // "OUTFIT UNLOCKED" on one line where it fits, stacked where it does
    // not — the narrowest rotation this runs on is 240px wide, and the
    // Bangers MD face does not shrink, so a portrait screen needs the
    // two-line form. Measured rather than assumed so a font change
    // cannot silently push it off the edge.
    const int oneLineW = Theme::bangersTextWidth("OUTFIT UNLOCKED",
                                                 Theme::BangersSize::MD);
    const bool stacked = oneLineW > (w - 16);
    const int  headTop = panelY + 10;
    const int  lineH   = 30;
    // Opaque plate behind the type. The tunnel and the fire both put
    // high-contrast detail exactly where the headline sits, and rainbow
    // glyphs over a moving backdrop are the one combination this palette
    // cannot keep legible. Sized off the measured text, not a guess.
    {
        const int plateH = (stacked ? lineH * 2 : lineH) + 12;
        const int plateW = (stacked
                            ? Theme::bangersTextWidth("UNLOCKED", Theme::BangersSize::MD)
                            : oneLineW) + 18;
        t.fillRect((w - plateW) / 2, headTop - 6, plateW, plateH, Theme::BG);
        t.drawRect((w - plateW) / 2, headTop - 6, plateW, plateH,
                   Theme::blend(Theme::BG, Theme::VAPOR_PURPLE, 140));
    }
    if (stacked) {
        drawRainbowHeadline(t, w / 2, headTop,         "OUTFIT",   now, reveal);
        drawRainbowHeadline(t, w / 2, headTop + lineH, "UNLOCKED", now, reveal);
    } else {
        drawRainbowHeadline(t, w / 2, headTop, "OUTFIT UNLOCKED", now, reveal);
    }
    const int headBottom = headTop + (stacked ? lineH * 2 : lineH) + 12;

    // ---- the costume itself ----------------------------------------------
    // Footer holds the outfit name and the dismiss hint.
    const int footerH   = 34;
    const int footerTop = panelY + panelH - footerH;
    const int stageH    = footerTop - headBottom;

    if (stageH > 40) {
        // drawWaving() puts the feet on baseY and the head top at
        // baseY - 58*scale, so the scale that fills the stage is the
        // stage height over that same 58 plus a little breathing room.
        float scale = (float)stageH / 78.0f;
        if (scale > 1.7f) scale = 1.7f;
        if (scale < 0.7f) scale = 0.7f;

        // The whole render path reads the override, so this shows the
        // new costume without switching the player into it. Cleared
        // immediately after -- leaving it set would take over every
        // other screen that draws him.
        Squachy::setOutfitPreview((int8_t)s_outfitIdx);
        Squachy::drawWaving(t, w / 2, footerTop - 4, now, scale, nullptr, false, 0);
        Squachy::setOutfitPreview(-1);
    }

    // ---- name + dismiss hint ---------------------------------------------
    const char* name = Squachy::outfitNameAt(s_outfitIdx);
    t.setTextSize(2);
    t.setTextColor(Theme::VAPOR_YELLOW);
    int nw = t.textWidth(name);
    t.setCursor((w - nw) / 2, footerTop + 2);
    t.print(name);

    // Blinks, so it reads as a prompt rather than a label.
    if (((now / 500) % 2) == 0) {
        const char* hint = "TAP TO CONTINUE";
        t.setTextSize(1);
        t.setTextColor(Theme::CYAN);
        int hw = t.textWidth(hint);
        t.setCursor((w - hw) / 2, footerTop + 22);
        t.print(hint);
    }

    Theme::drawGlitchStatic(t, 6, panelY + 2, w - 6, panelY + panelH - 2);
}
