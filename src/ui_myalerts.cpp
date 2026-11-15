// SquachWatch-CYD -- MY ALERTS. See include/ui_myalerts.h.
#include "ui_myalerts.h"
#include "my_alerts.h"
#include "theme.h"
#include "privacy.h"
#include <Arduino.h>
#include <stdio.h>
#include <string.h>

namespace {

const int ROW_Y0  = 36;
const int ROW_GAP = 2;
int btnH() { return Theme::dialogBtnH(28); }
// One line a row, pattern left and label right: eight of them have to fit
// above the buttons on a 240-row panel.
int rowH(TFT_eSPI& t) { return t.fontHeight() + 8; }

int s_selected = -1;   // a slot, not a row

bool in(int x, int y, int bx, int by, int bw, int bh) {
    const int slop = 6;
    return x >= bx - slop && x < bx + bw + slop && y >= by - slop && y < by + bh + slop;
}

// WIFI NETWORKS' button bar: one row of four on a wide screen, two rows of
// two on a tall one.
struct Bar { int rows, w, x[4], y[4]; };
Bar bar(TFT_eSPI& t, int n) {
    Bar b;
    const int gap = 8;
    const bool wide = t.width() >= 300;
    const int perRow = wide ? n : 2;
    b.rows = (n + perRow - 1) / perRow;
    b.w = (t.width() - 16 - (perRow - 1) * gap) / perRow;
    if (b.w > (Theme::large() ? 140 : 110)) b.w = Theme::large() ? 140 : 110;
    for (int i = 0; i < n; i++) {
        const int r = i / perRow, c = i % perRow;
        const int span = perRow * b.w + (perRow - 1) * gap;
        b.x[i] = (t.width() - span) / 2 + c * (b.w + gap);
        b.y[i] = t.height() - 6 - (b.rows - r) * btnH() - (b.rows - 1 - r) * 6;
    }
    return b;
}

int rowsThatFit(TFT_eSPI& t, int barTop) {
    const int n = (barTop - 4 - ROW_Y0) / (rowH(t) + ROW_GAP);
    return n < 1 ? 1 : n;
}

// The first row on screen: the top, unless the selection would be off the
// bottom, in which case the list slides up to keep it in view.
int firstRow(int fit) {
    const int n = MyAlerts::count();
    if (n <= fit) return 0;
    int selRow = -1;
    for (int r = 0; r < n; r++) if (MyAlerts::nth((uint8_t)r) == s_selected) { selRow = r; break; }
    int first = selRow - fit + 1;
    if (first < 0) first = 0;
    if (first > n - fit) first = n - fit;
    return first;
}

void paragraph(TFT_eSPI& t, int y, uint16_t c, const char* s) {
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
    if (line[0]) { t.setCursor(8, y); t.print(line); }
}

}  // namespace

void uiMyAlertsInit(TFT_eSPI& t) {
    MyAlerts::begin();
    if (!MyAlerts::used((uint8_t)(s_selected < 0 ? 0xFF : s_selected))) s_selected = MyAlerts::nth(0);
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
}

void uiMyAlertsSelect(int slot) { s_selected = slot; }
int  uiMyAlertsSelected()       { return s_selected; }

void uiMyAlertsTick(TFT_eSPI& t, uint32_t now) {
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
    Theme::drawTitleBar(t, ">> MY ALERTS <<");
    Theme::bodyFontOn(t);
    t.setTextWrap(false);
    const int w = t.width();
    const uint8_t n = MyAlerts::count();
    const Bar b = bar(t, 4);
    if (!n) {
#if defined(SQW_SMALL)
        paragraph(t, ROW_Y0 + 4, Theme::WHITE, "Alert on a WiFi name. ADD types one: veh* catches veh912.");
#else
        paragraph(t, ROW_Y0 + 4, Theme::WHITE,
                  "Get an alert when a WiFi network's name matches one of yours. ADD types one. "
                  "Plain text catches names that start with it. * stands for anything, ? for one "
                  "character: veh* catches veh912 and veh9?? catches veh901. Up to eight.");
#endif
    }
    const int fit   = rowsThatFit(t, b.y[0]);
    const int first = firstRow(fit);
    const int rh    = rowH(t);
    for (int r = first; r < n && r < first + fit; r++) {
        const int slot = MyAlerts::nth((uint8_t)r);
        const int y = ROW_Y0 + (r - first) * (rh + ROW_GAP);
        const bool sel = slot == s_selected;
        t.fillRect(8, y, w - 16, rh, Theme::TASKBAR);
        t.drawRect(8, y, w - 16, rh, sel ? Theme::VAPOR_PINK : Theme::VAPOR_PURPLE);
        const int cw = t.textWidth("M");
        // The label gets the right-hand side, the pattern what is left.
        char pv[40];
        const char* lab = Privacy::name(MyAlerts::label((uint8_t)slot), pv, sizeof pv);
        const int labW = lab[0] ? t.textWidth(lab) + 8 : 0;
        const int patChars = (w - 16 - 12 - labW) / cw;
        char pat[40];
        char pv2[40];
        snprintf(pat, sizeof pat, "%.*s", patChars > 32 ? 32 : (patChars < 1 ? 1 : patChars),
                 Privacy::name(MyAlerts::pattern((uint8_t)slot), pv2, sizeof pv2));
        t.setTextColor(Theme::WHITE, Theme::TASKBAR);
        t.setCursor(14, y + (rh - t.fontHeight()) / 2);
        t.print(pat);
        if (lab[0]) {
            t.setTextColor(Theme::AMBER, Theme::TASKBAR);
            t.setCursor(w - 14 - t.textWidth(lab), y + (rh - t.fontHeight()) / 2);
            t.print(lab);
        }
    }
    if (n > fit) {
        // More than the screen holds: say so where the list stops.
        char more[24];
        snprintf(more, sizeof more, "%d of %u", first + fit < n ? first + fit : n, (unsigned)n);
        t.setTextColor(Theme::W95_SHADOW, Theme::BG);
        t.setCursor(w - 8 - t.textWidth(more), ROW_Y0 - t.fontHeight() - 2);
        t.print(more);
    }
    const bool haveSel = MyAlerts::used((uint8_t)(s_selected < 0 ? 0xFF : s_selected));
    const bool full    = n >= MyAlerts::MAX;
    Theme::drawWin95Button(t, b.x[0], b.y[0], b.w, btnH(), full ? "FULL" : "ADD", full);
    Theme::drawWin95Button(t, b.x[1], b.y[1], b.w, btnH(), "LABEL",  !haveSel);
    Theme::drawWin95Button(t, b.x[2], b.y[2], b.w, btnH(), "REMOVE", !haveSel);
    Theme::drawWin95Button(t, b.x[3], b.y[3], b.w, btnH(), "BACK",   false);
    Theme::bodyFontOff(t);
    Theme::drawToast(t, now);
}

MyAlertsHit uiMyAlertsHit(TFT_eSPI& t, int x, int y, int* slot) {
    // Measured in the font the rows are drawn in, or the hit rows drift
    // away from the drawn ones on the large panels.
    Theme::bodyFontOn(t);
    const Bar b = bar(t, 4);
    const int fit   = rowsThatFit(t, b.y[0]);
    const int first = firstRow(fit);
    const int rh    = rowH(t);
    Theme::bodyFontOff(t);
    const uint8_t n = MyAlerts::count();
    for (int r = first; r < n && r < first + fit; r++) {
        const int ry = ROW_Y0 + (r - first) * (rh + ROW_GAP);
        if (y >= ry && y < ry + rh + ROW_GAP && x >= 8 && x <= t.width() - 8) {
            if (slot) *slot = MyAlerts::nth((uint8_t)r);
            return MyAlertsHit::ROW;
        }
    }
    if (in(x, y, b.x[0], b.y[0], b.w, btnH())) return MyAlertsHit::ADD;
    if (in(x, y, b.x[1], b.y[1], b.w, btnH())) return MyAlertsHit::LABEL;
    if (in(x, y, b.x[2], b.y[2], b.w, btnH())) return MyAlertsHit::REMOVE;
    if (in(x, y, b.x[3], b.y[3], b.w, btnH())) return MyAlertsHit::BACK;
    return MyAlertsHit::NONE;
}
