// SquachWatch-CYD -- SHARE LOGS. See include/ui_logshare.h.
#include "ui_logshare.h"
#if SQW_LOG_SHARE
#include "log_share.h"
#include "theme.h"
#include "privacy.h"
#include <Arduino.h>
#include <stdio.h>
#include <string.h>

namespace {

int btnH() { return Theme::dialogBtnH(28); }
int btnW(TFT_eSPI& t) { const int w = Theme::large() ? 140 : 110; return w < t.width() - 16 ? w : t.width() - 16; }
int btnX(TFT_eSPI& t) { return (t.width() - btnW(t)) / 2; }
int btnY(TFT_eSPI& t) { return t.height() - 6 - btnH(); }

// Wrapped at the screen width, a word at a time. Returns the y below it.
int paragraph(TFT_eSPI& t, int y, uint16_t c, const char* s) {
    const int maxW = t.width() - 16;
    char line[72] = "";
    t.setTextColor(c, Theme::BG);
    while (*s) {
        while (*s == ' ') s++;
        const char* e = s;
        while (*e && *e != ' ') e++;
        char word[40];
        snprintf(word, sizeof word, "%.*s", (int)(e - s), s);
        char trial[72];
        snprintf(trial, sizeof trial, "%s%s%s", line, line[0] ? " " : "", word);
        if (line[0] && t.textWidth(trial) > maxW) {
            t.setCursor(8, y); t.print(line); y += t.fontHeight() + 3;
            snprintf(line, sizeof line, "%s", word);
        } else {
            snprintf(line, sizeof line, "%s", trial);
        }
        s = e;
    }
    if (line[0]) { t.setCursor(8, y); t.print(line); y += t.fontHeight() + 3; }
    return y;
}

// A label and a value on one line, the value in its own colour.
int pair(TFT_eSPI& t, int y, const char* label, const char* value, uint16_t c) {
    t.setTextColor(Theme::W95_SHADOW, Theme::BG);
    t.setCursor(8, y);
    t.print(label);
    t.setTextColor(c, Theme::BG);
    t.print(value);
    return y + t.fontHeight() + 4;
}

}  // namespace

void uiLogShareInit(TFT_eSPI& t) {
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
}

void uiLogShareTick(TFT_eSPI& t, uint32_t now) {
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
    Theme::drawTitleBar(t, ">> SHARE LOGS <<");
    Theme::bodyFontOn(t);
    t.setTextWrap(false);
    int y = 40;
    char pv[40], buf[64];
    const char* ssid = Privacy::name(LogShare::network(), pv, sizeof pv);
    switch (LogShare::state()) {
        case LogShare::State::JOINING: {
            const char* dots[] = { "", ".", "..", "..." };
            snprintf(buf, sizeof buf, "Joining %s%s", ssid, dots[(now / 400) % 4]);
            y = paragraph(t, y, Theme::CYAN, buf);
            paragraph(t, y + 6, Theme::WHITE, "Detection is paused while the logs are shared.");
            break;
        }
        case LogShare::State::SERVING: {
            y = paragraph(t, y, Theme::WHITE, "On a computer on the same network, open:");
            snprintf(buf, sizeof buf, "http://%s/", LogShare::address());
            t.setTextColor(Theme::CYAN, Theme::BG);
            t.setCursor(8, y + 2);
            t.print(buf);
            y += t.fontHeight() + 10;
            y = pair(t, y, "USER  ", "squach", Theme::GREEN);
            y = pair(t, y, "CODE  ", LogShare::code(), Theme::GREEN);
            const uint32_t s = LogShare::secondsLeft(now);
            snprintf(buf, sizeof buf, "%lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
            y = pair(t, y + 4, "STOPS IN  ", buf, s < 60 ? Theme::AMBER : Theme::WHITE);
            snprintf(buf, sizeof buf, "%u", (unsigned)LogShare::requests());
            y = pair(t, y, "REQUESTS  ", buf, Theme::WHITE);
            y = pair(t, y, "NETWORK  ", ssid, Theme::WHITE);
            paragraph(t, y + 4, Theme::W95_SHADOW, "Detection is paused. Plain HTTP: share on a network you trust.");
            break;
        }
        case LogShare::State::FAILED:
            y = paragraph(t, y, Theme::AMBER, LogShare::failText());
            paragraph(t, y + 6, Theme::WHITE, "Check WIFI NETWORKS on the SYSTEM page, then try again.");
            break;
        default:
            break;
    }
    const bool failed = LogShare::state() == LogShare::State::FAILED;
    Theme::drawWin95Button(t, btnX(t), btnY(t), btnW(t), btnH(), failed ? "OK" : "STOP", false);
    Theme::bodyFontOff(t);
    Theme::drawToast(t, now);
}

LogShareHit uiLogShareHit(TFT_eSPI& t, int x, int y) {
    const int slop = 6;
    if (x >= btnX(t) - slop && x < btnX(t) + btnW(t) + slop &&
        y >= btnY(t) - slop && y < btnY(t) + btnH() + slop) return LogShareHit::STOP;
    return LogShareHit::NONE;
}
#endif  // SQW_LOG_SHARE
