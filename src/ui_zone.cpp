// SquachWatch-CYD — the time zone card. See ui_zone.h.
#include "ui_zone.h"
#include "clock.h"
#include "settings.h"
#include "theme.h"
#include <Arduino.h>
#include <stdio.h>

namespace {
// A size up on the large panels: a size-2 question, a size-3 zone, the body
// font for the time, and taller buttons.
int cardW() { return Theme::large() ? 320 : 232; }
int cardH() { return Theme::large() ? 136 : 96; }
int btnH()  { return Theme::large() ? 30 : 20; }

void cardRect(int screenW, int screenH, int& x, int& y) {
    x = (screenW - cardW()) / 2;
    // A little above centre, clear of the button bar on a landscape screen.
    y = (screenH - cardH()) / 2 - 8;
}

void buttonRects(int cx0, int cy0, int& px, int& okx, int& okw, int& nx, int& by) {
    by  = cy0 + cardH() - btnH() - 6;
    px  = cx0 + 6;
    nx  = cx0 + cardW() - 6 - 34;
    okx = px + 34 + 6;
    okw = nx - 6 - okx;
}
}  // namespace

bool uiZoneCardWanted() { return Clock::trusted() && !Settings::timeZoneChosen(); }

void uiZoneCardDraw(TFT_eSPI& t, uint32_t now) {
    (void)now;
    int x, y;
    cardRect(t.width(), t.height(), x, y);
    t.fillRoundRect(x, y, cardW(), cardH(), 6, Theme::BG);
    t.drawRoundRect(x, y, cardW(), cardH(), 6, Theme::VAPOR_PURPLE);
    t.drawRoundRect(x + 1, y + 1, cardW() - 2, cardH() - 2, 5, Theme::VAPOR_PURPLE);
    t.setTextSize(Theme::uiTextSize(t, 1));
    t.setTextWrap(false);
    const char* head = "WHICH TIME ZONE?";
    t.setTextColor(Theme::CYAN, Theme::BG);
    t.setCursor(x + (cardW() - t.textWidth(head)) / 2, y + 7);
    t.print(head);
    int ly = y + 7 + t.fontHeight() + 7;          // 22 in the system font
    // The zone, large, and the time it makes of right now: the second line
    // is how you know the first is right without knowing the names.
    t.setTextSize(Theme::upSize(2));
    const char* zn = Settings::timeZoneName();
    t.setTextColor(Theme::VAPOR_PINK, Theme::BG);
    t.setCursor(x + (cardW() - t.textWidth(zn)) / 2, ly);
    t.print(zn);
    ly += t.fontHeight() + 6;                       // 44 in the system font
    Theme::bodyFontOn(t);
    char tm[8], date[20], line[32];
    bool pm = false;
    Clock::formatTime(tm, sizeof tm, true, &pm);
    Clock::formatDate(date, sizeof date);
    snprintf(line, sizeof line, "%s %s  %s", tm, pm ? "PM" : "AM", date);
    t.setTextColor(Theme::VAPOR_YELLOW, Theme::BG);
    t.setCursor(x + (cardW() - t.textWidth(line)) / 2, ly);
    t.print(line);
    Theme::bodyFontOff(t);
    int px, okx, okw, nx, by;
    buttonRects(x, y, px, okx, okw, nx, by);
    Theme::drawButton(t, px,  by, 34,  btnH(), "<", false);
    Theme::drawButton(t, okx, by, okw, btnH(), "THIS IS RIGHT", false);
    Theme::drawButton(t, nx,  by, 34,  btnH(), ">", false);
}

ZoneHit uiZoneCardHit(int tx, int ty, int screenW, int screenH) {
    int x, y;
    cardRect(screenW, screenH, x, y);
    if (tx < x || tx >= x + cardW() || ty < y || ty >= y + cardH()) return ZoneHit::NONE;
    int px, okx, okw, nx, by;
    buttonRects(x, y, px, okx, okw, nx, by);
    if (ty < by - 4) return ZoneHit::CARD;
    if (tx < okx - 3)       return ZoneHit::PREV;
    if (tx >= nx - 3)       return ZoneHit::NEXT;
    return ZoneHit::OK;
}

uint8_t uiZoneCardStops(int screenW, int screenH, int16_t* xs, int16_t* ys, int16_t* ws, int16_t* hs) {
    int x, y;
    cardRect(screenW, screenH, x, y);
    int px, okx, okw, nx, by;
    buttonRects(x, y, px, okx, okw, nx, by);
    const int bx[3] = { px, okx, nx }, bw[3] = { 34, okw, 34 };
    for (int i = 0; i < 3; i++) {
        xs[i] = (int16_t)bx[i]; ys[i] = (int16_t)by;
        ws[i] = (int16_t)bw[i]; hs[i] = (int16_t)btnH();
    }
    return 3;
}
