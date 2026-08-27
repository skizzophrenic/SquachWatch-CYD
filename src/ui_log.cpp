// SquachWatch-CYD — log screen implementation
#include "ui_log.h"
#include "theme.h"
#include <Arduino.h>

static int g_scroll = 0;

void uiLogInit(TFT_eSPI& t) {
    g_scroll = 0;
    // fillScreen() relies on TFT_eSPI's base-class width/height, which
    // TFT_eSprite::createSprite() never updates — it leaves stale
    // remnants of whatever screen was drawn before when t is a sprite.
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
}

void uiLogScroll(int delta) {
    g_scroll += delta;
    if (g_scroll < 0) g_scroll = 0;
}

void uiLogTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng, int scrollOffset) {
    int w = t.width();
    int h = t.height();

    Theme::ButtonBarGeom bar = Theme::computeButtonBar(w, h);
    const int bodyTop    = 16;
    const int bodyBottom = bar.y - 4;
    const int bodyH      = bodyBottom - bodyTop;

    // Title bar
    char title[32];
    snprintf(title, sizeof(title), ">> LOG  (%u) <<", (unsigned)eng.logCount());
    Theme::drawTitleBar(t, title);

    // Body
    t.fillRect(0, bodyTop, w, bodyH, Theme::BG);

    uint8_t count = eng.logCount();

    if (count == 0) {
        // Make the empty state impossible to mistake for a broken screen.
        float pulse = 0.55f + 0.45f * sinf((float)(now % 2000) / 2000.0f * 6.2831853f);
        uint16_t col = Theme::blend(Theme::GREEN, Theme::CYAN, (uint16_t)(pulse * 200.0f));
        t.setTextSize(3);
        t.setTextColor(col, Theme::BG);
        const char* msg = "LOG EMPTY";
        int mw = t.textWidth(msg);
        t.setCursor((w - mw) / 2, bodyTop + bodyH / 3);
        t.print(msg);

        t.setTextSize(1);
        t.setTextColor(Theme::CYAN, Theme::BG);
        const char* sub = "no detections yet";
        int sw = t.textWidth(sub);
        t.setCursor((w - sw) / 2, bodyTop + bodyH / 3 + 35);
        t.print(sub);

        Theme::drawButtonBar(t, ButtonId::LOG);
        return;
    }

    int rowH = 22;
    int y = bodyTop;
    int idx = g_scroll;
    int max = (bodyH / rowH);

    for (int i = 0; i < max && idx < count; i++, idx++) {
        const Detection* d = eng.logAt(idx);
        if (!d) break;
        // Row separator
        t.drawFastHLine(0, y + rowH - 1, w, Theme::PURPLE);

        // Type label (colored)
        t.setTextSize(2);
        t.setTextColor(Theme::colorFor(d->type), Theme::BG);
        t.setCursor(4, y + 3);
        t.print(detectionTypeName(d->type));

        // MAC + RSSI line
        t.setTextSize(1);
        t.setTextColor(Theme::WHITE, Theme::BG);
        char mac[24];
        snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
                 d->mac[0], d->mac[1], d->mac[2],
                 d->mac[3], d->mac[4], d->mac[5]);
        t.setCursor(4, y + 14);
        t.print(mac);

        // RSSI
        t.setTextColor(Theme::CYAN, Theme::BG);
        t.setCursor(96, y + 14);
        t.printf("%ddBm", d->rssi);

        // Hits
        t.setTextColor(Theme::VAPOR_PURPLE, Theme::BG);
        t.setCursor(150, y + 14);
        t.printf("x%u", d->hits);

        // Timestamp (right edge)
        uint32_t ms = d->firstSeen;
        uint32_t sec = ms / 1000;
        char ts[12];
        snprintf(ts, sizeof(ts), "%02lu:%02lu", (unsigned long)(sec / 60), (unsigned long)(sec % 60));
        int tw = t.textWidth(ts);
        t.setTextColor(Theme::VAPOR_PINK, Theme::BG);
        t.setCursor(w - tw - 4, y + 3);
        t.print(ts);

        y += rowH;
    }

    // Bottom soft buttons
    Theme::drawButtonBar(t, ButtonId::LOG);
}
