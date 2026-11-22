// SquachWatch-CYD — the SQUAD UPDATE prompt. See ui_nudge.h.
#if SQUACH_MESH
#include "ui_nudge.h"
#include "theme.h"
#include "settings.h"
#include "ota_core.h"
#include <Arduino.h>
#include <stdio.h>
#include <string.h>

namespace {
int btnH() { return Theme::dialogBtnH(28); }
const int SLOP  = 6;

char     s_from[13] = "";
char     s_ver[16]  = "";
uint32_t s_until    = 0;

struct Btns { int y, w, nowX, skipX; };

Btns btns(TFT_eSPI& t) {
    Btns b;
    b.w = Theme::large() ? 140 : 110;
    const int gap = 14;
    b.nowX  = (t.width() - (b.w * 2 + gap)) / 2;
    b.skipX = b.nowX + b.w + gap;
    b.y     = t.height() - btnH() -
#if defined(M5_STOPWATCH)
48;
#else
8;
#endif
    return b;
}

bool in(int x, int y, int bx, int by, int bw, int bh) {
    return x >= bx - SLOP && x <= bx + bw + SLOP && y >= by - SLOP && y <= by + bh + SLOP;
}

void centred(TFT_eSPI& t, int y, uint16_t c, const char* s) {
    t.setTextColor(c, Theme::BG);
    t.setCursor((t.width() - t.textWidth(s)) / 2, y);
    t.print(s);
}
} // namespace

void uiNudgeInit(TFT_eSPI& t, const char* from, const uint8_t ver[3], uint16_t seconds, uint32_t now) {
    snprintf(s_from, sizeof s_from, "%s", from && from[0] ? from : "SOMEONE");
    snprintf(s_ver, sizeof s_ver, "v%u.%u.%u", ver[0], ver[1], ver[2]);
    s_until = now + (uint32_t)seconds * 1000u;
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
}

int uiNudgeSecondsLeft(uint32_t now) {
    const int32_t ms = (int32_t)(s_until - now);
    return ms <= 0 ? 0 : (int)((ms + 999) / 1000);
}

void uiNudgeTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng) {
    (void)eng;
    const int w = t.width(), h = t.height();
    t.fillRect(0, 0, w, h, Theme::BG);
    Theme::drawListHeading(t, "SQUAD UPDATE", Theme::VAPOR_PINK);

    Theme::bodyFontOn(t);
    int y = Theme::LIST_TOP + Theme::listHeadingH() + 10;
    char line[48];
    snprintf(line, sizeof line, "%s asked the squad to update", s_from);
    centred(t, y, Theme::WHITE, line);
    y += Theme::bodyY(12);
    snprintf(line, sizeof line, "to %s. This board runs %s.", s_ver, OtaCore::runningVersion());
    centred(t, y, Theme::WHITE, line);
    y += Theme::bodyY(12);
    centred(t, y, Theme::W95_LIGHT, "It joins WiFi, installs, and restarts.");
    y += Theme::bodyY(12);
    centred(t, y, Theme::W95_LIGHT, "Nothing changes if that fails.");

    // The count, big, in the middle of what is left.
    const Btns b = btns(t);
    const int left = uiNudgeSecondsLeft(now);
    t.setTextFont(1); t.setTextSize(Theme::upSize(2));
    snprintf(line, sizeof line, left > 0 ? "UPDATING IN %d" : "UPDATING...", left);
    const int cy = y + Theme::bodyY(12) + (b.y - (y + Theme::bodyY(12)) - t.fontHeight()) / 2;
    centred(t, cy, Theme::CYAN, line);
    t.setTextSize(1);

    Theme::drawWin95Button(t, b.nowX,  b.y, b.w, btnH(), "NOW",  false);
    Theme::drawWin95Button(t, b.skipX, b.y, b.w, btnH(), "SKIP", false);
}

NudgeHit uiNudgeHit(TFT_eSPI& t, int x, int y) {
    const Btns b = btns(t);
    if (in(x, y, b.nowX,  b.y, b.w, btnH())) return NudgeHit::NOW;
    if (in(x, y, b.skipX, b.y, b.w, btnH())) return NudgeHit::SKIP;
    return NudgeHit::NONE;
}
#endif // SQUACH_MESH
