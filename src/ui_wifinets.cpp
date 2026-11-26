// SquachWatch-CYD -- WIFI NETWORKS. See include/ui_wifinets.h.
#include "ui_wifinets.h"
#include "ota_wifi.h"
#include "theme.h"
#include "privacy.h"
#include <Arduino.h>
#include <stdio.h>
#include <string.h>

namespace {

const int ROW_Y0  = 36;
const int ROW_GAP = 3;
// A size up on the large panels, where rows carry the 16-row body font.
int btnH() { return Theme::dialogBtnH(28); }
int rowH() { return Theme::large() ? 40 : 26; }

int s_selected = -1;

bool in(int x, int y, int bx, int by, int bw, int bh) {
    const int slop = 6;
    return x >= bx - slop && x < bx + bw + slop && y >= by - slop && y < by + bh + slop;
}

// The button bar: one row of four on a wide screen, two rows of two on a
// tall one. Returns the y the rows above must stay clear of.
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
    const int n = (barTop - 6 - ROW_Y0) / (rowH() + ROW_GAP);
    return n < 1 ? 1 : n;
}

void paragraph(TFT_eSPI& t, int y, uint16_t c, const char* s) {
    // Wrapped at the screen width, on the small font, a word at a time.
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

const char* resultWords(OtaWifi::SavedResult r) {
    switch (r) {
        case OtaWifi::SavedResult::JOINED:       return "joined at the last boot";
        case OtaWifi::SavedResult::BAD_PASSWORD: return "wrong password";
        case OtaWifi::SavedResult::NOT_FOUND:    return "not found at the last boot";
        default:                                 return "not tried yet";
    }
}

uint16_t resultColour(OtaWifi::SavedResult r) {
    switch (r) {
        case OtaWifi::SavedResult::JOINED:       return Theme::GREEN;
        case OtaWifi::SavedResult::BAD_PASSWORD: return Theme::AMBER;
        case OtaWifi::SavedResult::NOT_FOUND:    return Theme::W95_SHADOW;
        default:                                 return Theme::CYAN;
    }
}

void signalBars(TFT_eSPI& t, int x, int yBottom, int8_t rssi) {
    const int bars = rssi >= -55 ? 4 : rssi >= -65 ? 3 : rssi >= -75 ? 2 : 1;
    for (int i = 0; i < 4; i++) {
        const int bh = 3 + i * 3;
        t.fillRect(x + i * 4, yBottom - bh, 3, bh, i < bars ? Theme::CYAN : Theme::W95_SHADOW);
    }
}

}  // namespace

// ---- the list ---------------------------------------------------------------

void uiWifiNetsInit(TFT_eSPI& t) {
    s_selected = OtaWifi::savedCount() ? (int)OtaWifi::savedUse() : -1;
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
}

void uiWifiNetsSelect(int row) { s_selected = row; }
int  uiWifiNetsSelected()      { return s_selected; }

void uiWifiNetsTick(TFT_eSPI& t, uint32_t now) {
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
    Theme::drawTitleBar(t, ">> WIFI NETWORKS <<");
    Theme::bodyFontOn(t);
    t.setTextWrap(false);
    const int w = t.width();
    const uint8_t n = OtaWifi::savedCount();
    const Bar b = bar(t, 4);
    if (!n) {
#if defined(SQW_SMALL)
        // Two lines is all there is above the buttons at 135 rows.
        paragraph(t, ROW_Y0 + 4, Theme::WHITE, "No networks saved. ADD picks one from a scan.");
#else
        paragraph(t, ROW_Y0 + 4, Theme::WHITE,
                  "No networks saved. ADD picks one from a scan. The password is checked at the next boot, "
                  "and this list says how that went.");
#endif
    }
    const int fit = rowsThatFit(t, b.y[0]);
    for (int i = 0; i < n && i < fit; i++) {
        const int y = ROW_Y0 + i * (rowH() + ROW_GAP);
        const bool sel = i == s_selected;
        const bool use = i == OtaWifi::savedUse();
        t.fillRect(8, y, w - 16, rowH(), Theme::TASKBAR);
        t.drawRect(8, y, w - 16, rowH(), sel ? Theme::VAPOR_PINK : Theme::VAPOR_PURPLE);
        const char* tag  = use ? "USE" : "";
        const int   tagW = tag[0] ? t.textWidth(tag) + 6 : 0;
        const int maxChars = (w - 16 - 12 - tagW) / t.textWidth("M");
        char name[40];
        char pv[40];
        snprintf(name, sizeof name, "%.*s", maxChars > 32 ? 32 : maxChars, Privacy::name(OtaWifi::savedSsidAt((uint8_t)i), pv, sizeof pv));
        t.setTextColor(Theme::WHITE, Theme::TASKBAR);
        t.setCursor(14, y + 4);
        t.print(name);
        if (tag[0]) {
            t.setTextColor(Theme::CYAN, Theme::TASKBAR);
            t.setCursor(w - 16 - tagW, y + 4);
            t.print(tag);
        }
        const OtaWifi::SavedResult r = OtaWifi::savedResult((uint8_t)i);
        t.setTextColor(resultColour(r), Theme::TASKBAR);
        t.setCursor(14, y + (Theme::large() ? 21 : 15));
        t.print(resultWords(r));
    }
    const bool haveSel = s_selected >= 0 && s_selected < n;
    const bool full    = n >= OtaWifi::SAVED_MAX;
    Theme::drawWin95Button(t, b.x[0], b.y[0], b.w, btnH(), "USE",    !haveSel || s_selected == OtaWifi::savedUse());
    Theme::drawWin95Button(t, b.x[1], b.y[1], b.w, btnH(), "REMOVE", !haveSel);
    Theme::drawWin95Button(t, b.x[2], b.y[2], b.w, btnH(), full ? "FULL" : "ADD", full);
    Theme::drawWin95Button(t, b.x[3], b.y[3], b.w, btnH(), "BACK",   false);
    Theme::bodyFontOff(t);
    Theme::drawToast(t, now);
}

WifiNetsHit uiWifiNetsHit(TFT_eSPI& t, int x, int y, int* row) {
    const Bar b = bar(t, 4);
    const int fit = rowsThatFit(t, b.y[0]);
    const uint8_t n = OtaWifi::savedCount();
    for (int i = 0; i < n && i < fit; i++) {
        const int ry = ROW_Y0 + i * (rowH() + ROW_GAP);
        if (y >= ry && y < ry + rowH() + ROW_GAP && x >= 8 && x <= t.width() - 8) {
            if (row) *row = i;
            return WifiNetsHit::ROW;
        }
    }
    if (in(x, y, b.x[0], b.y[0], b.w, btnH())) return WifiNetsHit::USE;
    if (in(x, y, b.x[1], b.y[1], b.w, btnH())) return WifiNetsHit::REMOVE;
    if (in(x, y, b.x[2], b.y[2], b.w, btnH())) return WifiNetsHit::ADD;
    if (in(x, y, b.x[3], b.y[3], b.w, btnH())) return WifiNetsHit::BACK;
    return WifiNetsHit::NONE;
}

// ---- adding one: the scan -----------------------------------------------------

void uiWifiAddInit(TFT_eSPI& t) {
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
}

void uiWifiAddTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng) {
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
    Theme::drawTitleBar(t, ">> ADD A NETWORK <<");
    Theme::bodyFontOn(t);
    t.setTextWrap(false);
    const int w = t.width();
    const Bar b = bar(t, 2);
    const int fit = rowsThatFit(t, b.y[0]);
    if (!eng.rawWifiScanDone()) {
        const char* dots[] = { "SCANNING", "SCANNING.", "SCANNING..", "SCANNING..." };
        t.setTextColor(Theme::CYAN, Theme::BG);
        t.setCursor(8, ROW_Y0 + 4);
        t.print(dots[(now / 400) % 4]);
    } else if (!eng.rawWifiCount()) {
        paragraph(t, ROW_Y0 + 4, Theme::WHITE, "No networks found. Move closer to the router and press RESCAN.");
    }
    const uint8_t n = eng.rawWifiCount();
    for (int i = 0; i < n && i < fit; i++) {
        const int y = ROW_Y0 + i * (rowH() + ROW_GAP);
        const char* ssid = eng.rawWifiSsid((uint8_t)i);
        const bool saved = OtaWifi::savedIndexOf(ssid) >= 0;
        const bool open  = eng.rawWifiOpen((uint8_t)i);
        t.fillRect(8, y, w - 16, rowH(), Theme::TASKBAR);
        t.drawRect(8, y, w - 16, rowH(), Theme::VAPOR_PURPLE);
        const char* tag  = saved ? "SAVED" : (open ? "OPEN" : "");
        const int   tagW = tag[0] ? t.textWidth(tag) + 6 : 0;
        const int maxChars = (w - 16 - 12 - 20 - tagW) / t.textWidth("M");
        char name[40];
        char pv[40];
        snprintf(name, sizeof name, "%.*s", maxChars > 32 ? 32 : maxChars, ssid[0] ? Privacy::name(ssid, pv, sizeof pv) : "(hidden)");
        t.setTextColor(Theme::WHITE, Theme::TASKBAR);
        t.setCursor(14, y + (rowH() - t.fontHeight()) / 2);
        t.print(name);
        if (tag[0]) {
            t.setTextColor(saved ? Theme::CYAN : Theme::AMBER, Theme::TASKBAR);
            t.setCursor(w - 16 - 20 - tagW, y + (rowH() - t.fontHeight()) / 2);
            t.print(tag);
        }
        signalBars(t, w - 16 - 16, y + rowH() - 5, eng.rawWifiRssi((uint8_t)i));
    }
    Theme::drawWin95Button(t, b.x[0], b.y[0], b.w, btnH(), "RESCAN", false);
    Theme::drawWin95Button(t, b.x[1], b.y[1], b.w, btnH(), "BACK",   false);
    Theme::bodyFontOff(t);
    Theme::drawToast(t, now);
}

WifiAddHit uiWifiAddHit(TFT_eSPI& t, int x, int y, const DetectionEngine& eng, int* row) {
    const Bar b = bar(t, 2);
    const int fit = rowsThatFit(t, b.y[0]);
    const uint8_t n = eng.rawWifiScanDone() ? eng.rawWifiCount() : 0;
    for (int i = 0; i < n && i < fit; i++) {
        const int ry = ROW_Y0 + i * (rowH() + ROW_GAP);
        if (y >= ry && y < ry + rowH() + ROW_GAP && x >= 8 && x <= t.width() - 8) {
            if (row) *row = i;
            return WifiAddHit::ROW;
        }
    }
    if (in(x, y, b.x[0], b.y[0], b.w, btnH())) return WifiAddHit::RESCAN;
    if (in(x, y, b.x[1], b.y[1], b.w, btnH())) return WifiAddHit::BACK;
    return WifiAddHit::NONE;
}
