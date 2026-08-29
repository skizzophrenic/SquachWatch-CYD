// SquachWatch-CYD — Squachy's diary screen implementation
#include "ui_diary.h"
#include "theme.h"
#include "squachy.h"
#include <Arduino.h>

// Stamped in by extra_script.py from `git describe` at build time — see
// platformio.ini. Falls back if that step is somehow skipped.
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "unknown"
#endif

void uiDiaryInit(TFT_eSPI& t) {
    // fillScreen() relies on TFT_eSPI's base-class width/height, which
    // TFT_eSprite::createSprite() never updates — it leaves stale
    // remnants of whatever screen was drawn before when t is a sprite.
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
}

static void formatDuration(uint32_t ms, char* buf, size_t n) {
    uint32_t sec  = ms / 1000;
    uint32_t days = sec / 86400; sec %= 86400;
    uint32_t hrs  = sec / 3600;  sec %= 3600;
    uint32_t mins = sec / 60;
    if (days > 0)      snprintf(buf, n, "%lud %luh", (unsigned long)days, (unsigned long)hrs);
    else if (hrs > 0)  snprintf(buf, n, "%luh %lum", (unsigned long)hrs, (unsigned long)mins);
    else if (mins > 0) snprintf(buf, n, "%lum", (unsigned long)mins);
    else               snprintf(buf, n, "<1m");
}

static void drawStat(TFT_eSPI& t, int w, int y, int h, const char* label, const char* value) {
    t.setTextSize(1);
    t.setTextColor(Theme::CYAN, Theme::BG);
    t.setCursor(8, y + (h - t.fontHeight(1)) / 2);
    t.print(label);
    t.setTextColor(Theme::WHITE, Theme::BG);
    int vw = t.textWidth(value);
    t.setCursor(w - 8 - vw, y + (h - t.fontHeight(1)) / 2);
    t.print(value);
    t.drawFastHLine(4, y + h - 1, w - 8, Theme::PURPLE);
}

void uiDiaryTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng) {
    int w = t.width(), h = t.height();

    char title[32];
    snprintf(title, sizeof(title), ">> %s'S DIARY <<", Squachy::nickname());
    Theme::drawTitleBar(t, title);

    const int top  = 16;
    const int rowH = 24;
    char buf[24];

    snprintf(buf, sizeof(buf), "%lu", (unsigned long)eng.lifetimeTotal());
    drawStat(t, w, top + 0 * rowH, rowH, "LIFETIME CATCHES", buf);

    snprintf(buf, sizeof(buf), "%lu", (unsigned long)Squachy::bootCount());
    drawStat(t, w, top + 1 * rowH, rowH, "BOOTS", buf);

    snprintf(buf, sizeof(buf), "%lu", (unsigned long)Squachy::petCount());
    drawStat(t, w, top + 2 * rowH, rowH, "TIMES PETTED", buf);

    formatDuration(Squachy::currentClearStreakMs(), buf, sizeof(buf));
    drawStat(t, w, top + 3 * rowH, rowH, "CURRENT CLEAR STREAK", buf);

    formatDuration(Squachy::bestClearStreakMs(), buf, sizeof(buf));
    drawStat(t, w, top + 4 * rowH, rowH, "BEST CLEAR STREAK", buf);

    snprintf(buf, sizeof(buf), "%lu", (unsigned long)Squachy::bestSessionCount());
    drawStat(t, w, top + 5 * rowH, rowH, "BEST SESSION CATCH", buf);

    DetectionType ft = Squachy::firstDetectionType();
    drawStat(t, w, top + 6 * rowH, rowH, "FIRST EVER CATCH",
             ft == DetectionType::UNKNOWN ? "none yet" : detectionTypeName(ft));

    drawStat(t, w, top + 7 * rowH, rowH, "FIRMWARE", FIRMWARE_VERSION);

    // Hint, pulsing gently so it doesn't just look like inert label text.
    float pulse = 0.4f + 0.3f * sinf((float)(now % 1600) / 1600.0f * 6.2831853f);
    uint16_t col = Theme::blend(Theme::BG, Theme::VAPOR_BLUE, (uint16_t)(pulse * 255.0f));
    t.setTextSize(1);
    t.setTextColor(col, Theme::BG);
    const char* hint = "tap anywhere to go back";
    int hw = t.textWidth(hint);
    t.setCursor((w - hw) / 2, top + 8 * rowH + 12);
    t.print(hint);
}
