// SquachWatch-CYD — OUTFIT UNLOCKED celebration popup implementation
#include "ui_outfit_unlock.h"
#include "theme.h"
#include "squachy.h"
#include "detection.h"
#include <Arduino.h>

static uint8_t  s_outfitIdx = 0;
static uint32_t s_openedAt  = 0;
// Which card this is. Everything structural below is shared; only the
// headline, what stands on the stage, and the footer line differ.
static bool     s_petCard   = false;

// Long enough that a touch still settling from whatever screen was up
// when this opened cannot dismiss it, short enough that a player who
// wants to move on is not held hostage.
static const uint32_t MIN_ON_SCREEN_MS = 900;

// Reveal timings, all measured from s_openedAt.
static const uint32_t RISE_MS = 420;   // panel scales open
static const uint32_t TEXT_MS = 700;   // headline letters land, one by one

static void openCard(TFT_eSPI& t) {
    s_openedAt = millis();
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
    // Open loud: the same shared burst every glitch-aware draw call
    // reads from, at the top intensity, so the popup arrives with a
    // tear rather than fading politely in.
    Theme::triggerGlitchBurst(4);
}

void uiOutfitUnlockInit(TFT_eSPI& t, uint8_t outfitIdx) {
    s_outfitIdx = outfitIdx;
    s_petCard   = false;
    openCard(t);
}

void uiPetUnlockInit(TFT_eSPI& t) {
    s_petCard = true;
    openCard(t);
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
    const char* headOne = s_petCard ? "PET UNLOCKED" : "OUTFIT UNLOCKED";
    const char* headTwo = s_petCard ? "PET"          : "OUTFIT";
    const int oneLineW = Theme::bangersTextWidth(headOne, Theme::BangersSize::MD);
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
        drawRainbowHeadline(t, w / 2, headTop,         headTwo,    now, reveal);
        drawRainbowHeadline(t, w / 2, headTop + lineH, "UNLOCKED", now, reveal);
    } else {
        drawRainbowHeadline(t, w / 2, headTop, headOne, now, reveal);
    }
    const int headBottom = headTop + (stacked ? lineH * 2 : lineH) + 12;

    // ---- the costume itself ----------------------------------------------
    // Footer holds the outfit name and the dismiss hint.
    const int footerH   = Theme::large() ? 42 : 34;   // the hint is the body font
    const int footerTop = panelY + panelH - footerH;
    const int stageH    = footerTop - headBottom;

    if (stageH > 40 && s_petCard) {
        // BOTH of them, either side of him. The unlock hands over two
        // companions, not one, and a card showing a single figure would be
        // the same half-answer the bubble line already gives -- somebody
        // would earn this and never learn the other one existed.
        //
        // Feet on one baseline, him smaller than on the outfit card so the
        // three read as a group rather than as a portrait with clutter.
        // Six clear of the footer: the lil guy at pet size is forty pixels
        // tall and was standing on the S of SETTINGS.
        const int baseY = footerTop - 7;
        // The large panels let him grow with the stage: 1.7 was sized for
        // the 2.8", and on the 3.5" it left him small in a big card.
        const float cap = Theme::large() ? 2.6f : 1.7f;
        float scale = (float)stageH / 68.0f;
        if (scale > cap) scale = cap;
        if (scale < 0.6f) scale = 0.6f;
        Squachy::drawWaving(t, w / 2, baseY, now, scale, nullptr, false, 0);
        // The yeti on his right, the lil guy on his left, far enough out to
        // clear his arms at this scale.
        //
        // The yeti is drawn at the ONE size the ski hill draws him -- he does
        // not scale, which is the whole reason he was free to add -- so he is
        // dropped a few pixels: his sprite's baseY sits his feet a little
        // higher than drawWaving() puts Squachy's, and on one shared floor
        // that reads as him hovering.
        const int gap = (int)(52.0f * scale);
        Theme::drawYeti(t, w / 2 + gap, baseY + 5, now, Theme::YetiPose::STAND);
        // Scale 4 is the size the pet itself is drawn at, so the lil guy on
        // the card is the lil guy you are about to get rather than a smaller
        // cousin of him.
        Theme::drawLilGuy(t, w / 2 - gap, baseY, now, 4, true);
    } else if (stageH > 40) {
        // drawWaving() puts the feet on baseY and the head top at
        // baseY - 58*scale, so the scale that fills the stage is the
        // stage height over that same 58 plus a little breathing room.
        // Fill the stage on the 3.5", with room over his head for a hat:
        // at the 2.8"'s 78 the unicorn's horn ran up into the headline.
        const float cap = Theme::large() ? 4.0f : 1.7f;
        float scale = (float)stageH / (Theme::large() ? 92.0f : 78.0f);
        if (scale > cap) scale = cap;
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
    // On the outfit card this is the costume's name. On the pet card it is
    // WHERE TO FIND THEM, because the row it points at did not exist until
    // this moment -- SettingsRow::PET is skipped entirely while the pet is
    // locked, so earning it makes a new row appear with nothing to say so.
    const char* name = s_petCard ? "SETTINGS > PET"
                                 : Squachy::outfitNameAt(s_outfitIdx);
    t.setTextSize(2);
    t.setTextColor(Theme::VAPOR_YELLOW);
    int nw = t.textWidth(name);
    t.setCursor((w - nw) / 2, footerTop + 2);
    t.print(name);

    // Blinks, so it reads as a prompt rather than a label.
    if (((now / 500) % 2) == 0) {
#if defined(SQW_SMALL)
        const char* hint = "PRESS TO CONTINUE";   // no touch panel: a button
#else
        const char* hint = "TAP TO CONTINUE";
#endif
        Theme::bodyFontOn(t);
        t.setTextColor(Theme::CYAN);
        int hw = t.textWidth(hint);
        t.setCursor((w - hw) / 2, footerTop + 22);
        t.print(hint);
        Theme::bodyFontOff(t);
    }

    Theme::drawGlitchStatic(t, 6, panelY + 2, w - 6, panelY + panelH - 2);
}
