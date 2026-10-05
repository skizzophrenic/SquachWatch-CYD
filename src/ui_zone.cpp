// SquachWatch-CYD — the time zone card. See ui_zone.h.
#include "ui_zone.h"
#include "clock.h"
#include "draw_band.h"
#include "settings.h"
#include "theme.h"
#include <Arduino.h>
#include <stdio.h>

namespace {
constexpr int CARD_W = 232, CARD_H = 96;
constexpr int BTN_H  = 20;

void cardRect(int screenW, int screenH, int& x, int& y) {
    x = (screenW - CARD_W) / 2;
    // A little above centre, clear of the button bar on a landscape screen.
    y = (screenH - CARD_H) / 2 - 8;
}

void buttonRects(int cx0, int cy0, int& px, int& okx, int& okw, int& nx, int& by) {
    by  = cy0 + CARD_H - BTN_H - 6;
    px  = cx0 + 6;
    nx  = cx0 + CARD_W - 6 - 34;
    okx = px + 34 + 6;
    okw = nx - 6 - okx;
}
}  // namespace

bool uiZoneCardWanted() { return Clock::trusted() && !Settings::timeZoneChosen(); }

void uiZoneCardDraw(TFT_eSPI& t, uint32_t now) {
    (void)now;
    int x, y;
    cardRect(t.width(), t.height(), x, y);
    if (!DrawBand::has(y, y + CARD_H)) return;
    t.fillRoundRect(x, y, CARD_W, CARD_H, 6, Theme::BG);
    t.drawRoundRect(x, y, CARD_W, CARD_H, 6, Theme::VAPOR_PURPLE);
    t.drawRoundRect(x + 1, y + 1, CARD_W - 2, CARD_H - 2, 5, Theme::VAPOR_PURPLE);
    t.setTextSize(1);
    t.setTextWrap(false);
    const char* head = "WHICH TIME ZONE?";
    t.setTextColor(Theme::CYAN, Theme::BG);
    t.setCursor(x + (CARD_W - t.textWidth(head)) / 2, y + 7);
    t.print(head);
    // The zone, large, and the time it makes of right now: the second line
    // is how you know the first is right without knowing the names.
    t.setTextSize(2);
    const char* zn = Settings::timeZoneName();
    t.setTextColor(Theme::VAPOR_PINK, Theme::BG);
    t.setCursor(x + (CARD_W - t.textWidth(zn)) / 2, y + 22);
    t.print(zn);
    t.setTextSize(1);
    char tm[8], date[20], line[32];
    bool pm = false;
    Clock::formatTime(tm, sizeof tm, true, &pm);
    Clock::formatDate(date, sizeof date);
    snprintf(line, sizeof line, "%s %s  %s", tm, pm ? "PM" : "AM", date);
    t.setTextColor(Theme::VAPOR_YELLOW, Theme::BG);
    t.setCursor(x + (CARD_W - t.textWidth(line)) / 2, y + 44);
    t.print(line);
    int px, okx, okw, nx, by;
    buttonRects(x, y, px, okx, okw, nx, by);
    Theme::drawButton(t, px,  by, 34,  BTN_H, "<", false);
    Theme::drawButton(t, okx, by, okw, BTN_H, "THIS IS RIGHT", false);
    Theme::drawButton(t, nx,  by, 34,  BTN_H, ">", false);
}

ZoneHit uiZoneCardHit(int tx, int ty, int screenW, int screenH) {
    int x, y;
    cardRect(screenW, screenH, x, y);
    if (tx < x || tx >= x + CARD_W || ty < y || ty >= y + CARD_H) return ZoneHit::NONE;
    int px, okx, okw, nx, by;
    buttonRects(x, y, px, okx, okw, nx, by);
    if (ty < by - 4) return ZoneHit::CARD;
    if (tx < okx - 3)       return ZoneHit::PREV;
    if (tx >= nx - 3)       return ZoneHit::NEXT;
    return ZoneHit::OK;
}
