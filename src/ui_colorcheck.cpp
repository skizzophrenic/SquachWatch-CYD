// SquachWatch-CYD — first-boot color-order sanity check implementation
#include "ui_colorcheck.h"
#include "theme.h"
#include "settings.h"
#include "squachy.h"
#include <Arduino.h>

// Pure RGB565 primaries, deliberately NOT Theme::RED/GREEN (those are
// stylized neon shades, not necessarily pure channels) -- the whole
// point of this screen is testing whether the panel renders a pure red
// as red, so it needs the real thing.
static const uint16_t PURE_RED   = 0xF800;
static const uint16_t PURE_GREEN = 0x07E0;
static const uint16_t PURE_BLUE  = 0x001F;

static const char* const CAPTION = "Wrong colors? Tap below till RED/GREEN/BLUE match.";
static const uint8_t CAPTION_MAX_LINES = 3;

// Shared by drawing and hit-testing so they can't drift apart -- no
// live TFT_eSPI& needed here (unlike most other screens' geometry
// helpers) since every row uses a fixed text size rather than metrics
// read back from the font, so there's nothing to query.
static void computeGeom(int w, int h,
                         int& squachyBaseY, float& squachyScale,
                         int& captionTop, int& captionMaxW,
                         int& wordsTop, int& wordRowH,
                         int& invX, int& invY, int& invW, int& invH,
                         int& ordX, int& ordY, int& ordW, int& ordH,
                         int& doneX, int& doneY, int& doneW, int& doneH) {
    (void)h;
    squachyScale = 0.6f;
    squachyBaseY = 50;
    captionTop = squachyBaseY + 14;
    captionMaxW = w - 24;
    wordsTop  = captionTop + CAPTION_MAX_LINES * 10 + 4;
    wordRowH  = 24;

    int btnY = wordsTop + wordRowH * 3 + 6;
    int btnH = 20;
    const int margin = 12, gap = 8;
    int btnW = (w - 2 * margin - gap) / 2;
    invX = margin;           invY = btnY; invW = btnW; invH = btnH;
    ordX = invX + btnW + gap; ordY = btnY; ordW = btnW; ordH = btnH;

    doneY = btnY + btnH + 6;
    doneX = margin;
    doneW = w - 2 * margin;
    doneH = btnH;
}

void uiColorCheckInit(TFT_eSPI& t) {
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
}

void uiColorCheckTick(TFT_eSPI& t, uint32_t now) {
    int w = t.width(), h = t.height();
    t.fillRect(0, 0, w, h, Theme::BG);

    int squachyBaseY, captionTop, captionMaxW, wordsTop, wordRowH;
    float squachyScale;
    int invX, invY, invW, invH, ordX, ordY, ordW, ordH, doneX, doneY, doneW, doneH;
    computeGeom(w, h, squachyBaseY, squachyScale, captionTop, captionMaxW, wordsTop, wordRowH,
                invX, invY, invW, invH, ordX, ordY, ordW, ordH, doneX, doneY, doneW, doneH);

    // Lightweight cameo (same one the boot splash itself uses just
    // before this screen appears) rather than the full tick()
    // idle/quip state machine -- this is a one-shot explanation, not
    // an idle screen he should be chattering on. No bubble here (line
    // left null) -- his normal speech bubble caps at 2 short lines,
    // too little room for this caption, so it's drawn as its own
    // wrapped block below him instead, with a real line budget.
    Squachy::drawWaving(t, w / 2, squachyBaseY, now, squachyScale, nullptr);

    t.setTextSize(1);
    t.setTextWrap(false);
    t.setTextColor(Theme::WHITE, Theme::BG);
    char capLines[CAPTION_MAX_LINES][48];
    uint8_t capN = Theme::wrapText(t, CAPTION, captionMaxW, capLines, CAPTION_MAX_LINES);
    int cy = captionTop;
    for (uint8_t i = 0; i < capN; i++) {
        int lw = t.textWidth(capLines[i]);
        t.setCursor((w - lw) / 2, cy);
        t.print(capLines[i]);
        cy += 10;
    }

    t.setTextSize(3);
    const char* words[3] = { "RED", "GREEN", "BLUE" };
    uint16_t cols[3] = { PURE_RED, PURE_GREEN, PURE_BLUE };
    int y = wordsTop;
    for (int i = 0; i < 3; i++) {
        t.setTextColor(cols[i], Theme::BG);
        int tw = t.textWidth(words[i]);
        t.setCursor((w - tw) / 2, y);
        t.print(words[i]);
        y += wordRowH;
    }

    char invLabel[16];
    snprintf(invLabel, sizeof(invLabel), "INVERT: %s", Settings::inverted() ? "ON" : "OFF");
    char ordLabel[20];
    snprintf(ordLabel, sizeof(ordLabel), "ORDER: %s", Settings::rgbSwapped() ? "SWAP" : "NORM");
    Theme::drawButton(t, invX, invY, invW, invH, invLabel, false);
    Theme::drawButton(t, ordX, ordY, ordW, ordH, ordLabel, false);
    Theme::drawButton(t, doneX, doneY, doneW, doneH, "[ LOOKS GOOD ]", false);
}

ColorCheckTap uiColorCheckHitTest(int x, int y, int screenW, int screenH) {
    int squachyBaseY, captionTop, captionMaxW, wordsTop, wordRowH;
    float squachyScale;
    int invX, invY, invW, invH, ordX, ordY, ordW, ordH, doneX, doneY, doneW, doneH;
    computeGeom(screenW, screenH, squachyBaseY, squachyScale, captionTop, captionMaxW, wordsTop, wordRowH,
                invX, invY, invW, invH, ordX, ordY, ordW, ordH, doneX, doneY, doneW, doneH);

    if (x >= invX && x <= invX + invW && y >= invY && y <= invY + invH) return ColorCheckTap::INVERT;
    if (x >= ordX && x <= ordX + ordW && y >= ordY && y <= ordY + ordH) return ColorCheckTap::ORDER;
    if (x >= doneX && x <= doneX + doneW && y >= doneY && y <= doneY + doneH) return ColorCheckTap::DONE;
    return ColorCheckTap::NONE;
}
