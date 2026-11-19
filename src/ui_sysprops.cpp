// SquachWatch-CYD — SYSTEM PROPERTIES. See include/ui_sysprops.h.
#include "ui_sysprops.h"
#include "theme.h"
#include "settings.h"
#include "ota_core.h"
#include "detection.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <stdio.h>
#include <string.h>

namespace {

// The one colour a Win95 window needs that the board's palette has no name
// for: the title bar's navy. Fixed like the silver in theme.h, and for the
// same reason -- the point of a system window is that it was borrowed from
// somewhere else.
const uint16_t NAVY = 0x0010;

// Everything in the window is set in font 2, the sixteen-row face the speech
// bubbles use, not the eight-row one. The first version used the small one
// and was hard to read on the board: at eight rows a line of grey label on
// silver is a smudge. So the window is bigger, the lines are fewer and
// wrapped to fit, and labels are black rather than grey.
// Set by geom(): a compact screen (240x135) gets a thinner title bar, tabs
// and buttons, and the Update tab loses its paragraph and checkbox.
int TITLE_H = 18;
int TAB_H   = 20;
int BTN_H   = 24;
const int LINE    = 17;       // one font-2 line and a pixel of air
const int SLOP    = 5;        // a fingertip is wider than a tab

enum Tab : uint8_t { TAB_UPDATE = 0, TAB_NOTES = 1, TAB_BOARD = 2, TAB_N = 3 };
const char* const TAB_NAME[TAB_N] = { "Update", "Notes", "Board" };

uint8_t s_tab = TAB_UPDATE;

struct Geom {
    int x, y, w, h;          // the window
    int tabY, tabW;          // the strip
    int px, py, pw, ph;      // the sunken panel
    int btnY;                // the button row
};

Geom geom(TFT_eSPI& t) {
    Geom g;
    const int sw = t.width(), sh = t.height();
    const bool c = Theme::compact();
    TITLE_H = c ? 16 : 18; TAB_H = c ? 18 : 20; BTN_H = c ? 20 : 24;
    g.w = sw - 8 < 300 ? sw - 8 : 300;
    g.h = sh - 8 < 232 ? sh - 8 : 232;
    g.x = (sw - g.w) / 2;
    g.y = (sh - g.h) / 2;
    g.tabY = g.y + 3 + TITLE_H + 3;
    g.tabW = (g.w - 8) / TAB_N;
    g.btnY = g.y + g.h - (Theme::compact() ? 4 : 6) - BTN_H;
    g.px = g.x + 4;
    g.py = g.tabY + TAB_H;
    g.pw = g.w - 8;
    g.ph = g.btnY - 5 - g.py;
#if defined(M5_STOPWATCH)
    g.x=44;g.y=76;g.w=378;g.h=360;TAB_H=32;BTN_H=38;
    g.tabY=100;g.tabW=123;g.px=44;g.py=144;g.pw=378;g.ph=190;g.btnY=398;
#endif
    return g;
}

bool in(int x, int y, int bx, int by, int bw, int bh) {
    return x >= bx - SLOP && x < bx + bw + SLOP && y >= by - SLOP && y < by + bh + SLOP;
}

// The buttons are sized to their words in font 2, so drawing and hit-testing
// both come through here and cannot disagree about where a button is.
int boldWidth(TFT_eSPI& t, const char* s);   // below, with bold()
struct Buttons { int updX, updW, laterX, laterW, closeX, closeW; };
Buttons buttons(TFT_eSPI& t, const Geom& g) {
    Theme::bubbleFontOn(t);
    t.setTextSize(1);
    Buttons b;
    const int right = g.x + g.w - 6;
    b.laterW = boldWidth(t, "Later") + 24;      if (b.laterW < 64) b.laterW = 64;
    b.updW   = boldWidth(t, "Update now") + 24;
    b.closeW = boldWidth(t, "Close") + 24;      if (b.closeW < 64) b.closeW = 64;
    b.laterX = right - b.laterW;
    b.updX   = b.laterX - 6 - b.updW;
    b.closeX = right - b.closeW;
    return b;
}

// The checkbox row sits on the panel's floor on the UPDATE tab.
int checkboxY(const Geom& g) { return g.py + g.ph - LINE - 3; }

// A raised Win95 surface: the window itself and the tabs are both this.
void raised(TFT_eSPI& t, int x, int y, int w, int h) {
    t.fillRect(x + 2, y + 2, w - 4, h - 4, Theme::W95_FACE);
    t.drawFastHLine(x, y, w, Theme::W95_HILITE);
    t.drawFastVLine(x, y, h, Theme::W95_HILITE);
    t.drawFastHLine(x, y + h - 1, w, Theme::W95_DKSHADOW);
    t.drawFastVLine(x + w - 1, y, h, Theme::W95_DKSHADOW);
    t.drawFastHLine(x + 1, y + 1, w - 2, Theme::W95_LIGHT);
    t.drawFastVLine(x + 1, y + 1, h - 2, Theme::W95_LIGHT);
    t.drawFastHLine(x + 1, y + h - 2, w - 2, Theme::W95_SHADOW);
    t.drawFastVLine(x + w - 2, y + 1, h - 2, Theme::W95_SHADOW);
}

// ...and a sunken one: the panel the tabs open onto.
void sunken(TFT_eSPI& t, int x, int y, int w, int h) {
    t.fillRect(x + 2, y + 2, w - 4, h - 4, Theme::W95_FACE);
    t.drawFastHLine(x, y, w, Theme::W95_SHADOW);
    t.drawFastVLine(x, y, h, Theme::W95_SHADOW);
    t.drawFastHLine(x, y + h - 1, w, Theme::W95_HILITE);
    t.drawFastVLine(x + w - 1, y, h, Theme::W95_HILITE);
}

void text(TFT_eSPI& t, int x, int y, const char* s, uint16_t c = Theme::W95_DKSHADOW) {
    t.setTextColor(c, Theme::W95_FACE);
    t.setCursor(x, y);
    t.print(s);
}

// Font 2 has no bold cut, so bold is the word drawn twice a pixel apart.
// Both passes transparent: an opaque second pass would paint its own
// background over the first and leave the word merely shifted.
//
// A real bold face (FreeSans Bold, bundled with the display library) and the
// classic blocky face doubled were both rendered against this one side by
// side. The real one was heavier but clipped values in portrait, the blocky
// one did not fit in portrait at all, and this one fits both rotations.
void bold(TFT_eSPI& t, int x, int y, const char* s, uint16_t c = Theme::W95_DKSHADOW) {
    t.setTextColor(c);
    t.setCursor(x, y);     t.print(s);
    t.setCursor(x + 1, y); t.print(s);
}

// How wide `s` is when drawn by bold(): the doubling adds a pixel.
int boldWidth(TFT_eSPI& t, const char* s) { return t.textWidth(s) + 1; }

// The row labels on the UPDATE tab. A darker red than the palette's: bright
// red on this silver reads worse than the black it replaced, and a system
// window keeps its own colours whatever theme the board wears.
const uint16_t LABEL_RED = 0x9800;

// A Win95 button whose word is bold: the bevel from Theme, the word from
// here, centred the same way drawWin95Button centres its own.
void boldButton(TFT_eSPI& t, int x, int y, int w, int h, const char* label) {
    Theme::drawWin95Button(t, x, y, w, h, "", false);
    const int tw = boldWidth(t, label);
    bold(t, x + 2 + (w - 4 - tw) / 2, y + 2 + (h - 4 - t.fontHeight()) / 2, label);
}

// `src`, cut to what fits in `maxW` pixels of the current font. Proportional
// type means a character count cannot say where a line ends.
void fit(TFT_eSPI& t, const char* src, int maxW, char* out, size_t cap) {
    snprintf(out, cap, "%s", src ? src : "");
    size_t n = strlen(out);
    while (n > 0 && t.textWidth(out) > maxW) out[--n] = '\0';
}

// A label and its value, in two columns. The label column is as wide as its
// longest label, measured, so it holds in either rotation.
int labelCol(TFT_eSPI& t) { return boldWidth(t, "Heard from") + 10; }

void row(TFT_eSPI& t, const Geom& g, int y, const char* name, const char* value,
         uint16_t valueCol = Theme::W95_DKSHADOW, bool redLabel = false) {
    const int col = labelCol(t);
    if (redLabel) bold(t, g.px + 8, y, name, LABEL_RED);
    else          text(t, g.px + 8, y, name);
    char v[48];
    fit(t, value, g.pw - 8 - col - 8, v, sizeof v);
    text(t, g.px + 8 + col, y, v, valueCol);
}

// Wrapped lines from `y` down, stopping short of `limit`. Returns where the
// next line would go.
int para(TFT_eSPI& t, int x, int y, int maxW, int limit, const char* s,
         uint16_t c = Theme::W95_DKSHADOW) {
    char lines[6][48];
    const uint8_t n = Theme::wrapText(t, s, maxW, lines, 6);
    for (uint8_t i = 0; i < n && y + 16 <= limit; i++, y += LINE) text(t, x, y, lines[i], c);
    return y;
}

void checkbox(TFT_eSPI& t, int x, int y, bool on, const char* label) {
    const int by = y + 3;
    t.fillRect(x, by, 11, 11, Theme::W95_HILITE);
    t.drawFastHLine(x, by, 11, Theme::W95_SHADOW);
    t.drawFastVLine(x, by, 11, Theme::W95_SHADOW);
    t.drawFastHLine(x + 1, by + 1, 9, Theme::W95_DKSHADOW);
    t.drawFastVLine(x + 1, by + 1, 9, Theme::W95_DKSHADOW);
    if (on) {
        // A tick, drawn rather than typed: the board's fonts have no check.
        for (int d = 0; d < 2; d++) {
            t.drawLine(x + 3, by + 5 + d, x + 5, by + 7 + d, Theme::W95_DKSHADOW);
            t.drawLine(x + 5, by + 7 + d, x + 9, by + 3 + d, Theme::W95_DKSHADOW);
        }
    }
    text(t, x + 17, y, label);
}

void uptimeText(char* out, size_t n, uint32_t now) {
    const uint32_t s = now / 1000;
    if (s < 3600) snprintf(out, n, "%lum %02lus", (unsigned long)(s / 60), (unsigned long)(s % 60));
    else          snprintf(out, n, "%luh %02lum", (unsigned long)(s / 3600), (unsigned long)((s / 60) % 60));
}

// ---- the three panels -------------------------------------------------------

void drawUpdateTab(TFT_eSPI& t, const Geom& g) {
    int y = g.py + 6;
    char buf[48];

    // As the build stamped it: a release is "v1.10.1", a bench build carries
    // the commit and "-dirty" after it, and both are the truth about what is
    // running. Cut to what the panel holds rather than dressed up.
    row(t, g, y, "Running", OtaCore::runningVersion(), Theme::W95_DKSHADOW, true);
    y += LINE;

    const char* name = OtaCore::releaseName();
    if (name[0]) snprintf(buf, sizeof buf, "v%s  %s", OtaCore::availableVersion(), name);
    else         snprintf(buf, sizeof buf, "v%s", OtaCore::availableVersion());
    row(t, g, y, "Available", buf, NAVY, true);
    y += LINE;

    const char* from = OtaCore::availableFrom();
    if (from[0]) snprintf(buf, sizeof buf, "%s's board", from);
    else         snprintf(buf, sizeof buf, "squachwatch.com");
    row(t, g, y, "Heard from", buf, Theme::W95_DKSHADOW, true);
    y += LINE + 5;

    if (Theme::compact()) return;     // no room: the switch is on the SYSTEM page too
    const int cb = checkboxY(g);
    para(t, g.px + 8, y, g.pw - 16, cb - 3,
         "It downloads over WiFi and restarts into it. What you have now "
         "is kept.");

    checkbox(t, g.px + 8, cb, Settings::updateCheck(), "Look for updates every boot");
}

void drawNotesTab(TFT_eSPI& t, const Geom& g) {
    int y = g.py + 6;
    const int limit = g.py + g.ph - 3;
    char buf[48];
    const char* name = OtaCore::releaseName();
    if (name[0]) snprintf(buf, sizeof buf, "v%s  \"%s\"", OtaCore::availableVersion(), name);
    else         snprintf(buf, sizeof buf, "v%s", OtaCore::availableVersion());
    char head[48];
    fit(t, buf, g.pw - 16, head, sizeof head);
    text(t, g.px + 8, y, head, NAVY);
    y += LINE + 4;

    const uint8_t n = OtaCore::newsCount();
    if (!n) {
        // Nothing came with it, and saying so is better than an empty box.
        // A squad member's hello carries a version and nothing else, and a
        // release made before the site started sending its lines has none.
        para(t, g.px + 8, y, g.pw - 16, limit,
             OtaCore::availableFrom()[0]
                 ? "A squad member had this one, and a hello carries no notes. What changed is on the site."
                 : "The site sent no notes with this one. What changed is on the site.");
        return;
    }
    // One bullet a line, each wrapped under its own dash.
    const int dash = t.textWidth("- ");
    for (uint8_t i = 0; i < n && y + 16 <= limit; i++) {
        text(t, g.px + 8, y, "-");
        y = para(t, g.px + 8 + dash, y, g.pw - 16 - dash, limit, OtaCore::newsAt(i));
        y += 2;
    }
}

void drawBoardTab(TFT_eSPI& t, const Geom& g) {
    int y = g.py + 6;
    char buf[48];

    row(t, g, y, "Build", OtaCore::buildName());
    y += LINE;
    const int limit = g.py + g.ph - 3;

    snprintf(buf, sizeof buf, "%s  %s", OtaCore::runningSlot(), OtaCore::runningVersion());
    row(t, g, y, "This slot", buf);
    y += LINE;

    const char* other = OtaCore::otherVersion();
    row(t, g, y, "Other slot", other && other[0] ? other : "nothing to go back to");
    y += LINE;
    if (y + 16 > limit) return;

    uptimeText(buf, sizeof buf, millis());
    row(t, g, y, "Up", buf);
    y += LINE;
    if (y + 16 > limit) return;

    snprintf(buf, sizeof buf, "%lu KB free", (unsigned long)(ESP.getFreeHeap() / 1024));
    row(t, g, y, "Memory", buf);
    y += LINE;
    if (y + 16 > limit) return;

    snprintf(buf, sizeof buf, "%lu KB in one piece",
             (unsigned long)(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) / 1024));
    row(t, g, y, "", buf);
}

}  // namespace

void uiSysPropsInit(TFT_eSPI& t) {
    s_tab = TAB_UPDATE;
    OtaCore::refreshOther();     // the BOARD tab reads the other slot
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
}

void uiSysPropsTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng, bool advance) {
    const int w = t.width(), h = t.height();

    // The room carries on behind it, dimmed: the window is in front of the
    // board, not instead of it.
    Theme::Palette saved = Theme::dimPaletteForOverlay(150);
    Theme::drawActiveBackground(t, now, 0, h, eng, advance);
    Theme::restorePalette(saved);
    Theme::dimRegion(t, 0, 0, w, h, 130);

    const Geom g = geom(t);
    const Buttons b = buttons(t, g);   // also switches to font 2
    t.setTextWrap(false);

#if defined(M5_STOPWATCH)
    t.setTextFont(1);t.setTextSize(2);t.setTextColor(Theme::CYAN);
    t.setCursor((w-t.textWidth("SYSTEM PROPERTIES"))/2,62);t.print("SYSTEM PROPERTIES");
    for(uint8_t i=0;i<TAB_N;++i)
        Theme::drawButton(t,g.x+4+i*g.tabW,g.tabY,g.tabW-4,TAB_H,TAB_NAME[i],i==s_tab);
    t.fillRoundRect(g.px,g.py,g.pw,g.ph,24,Theme::W95_FACE);
    Theme::bubbleFontOn(t);t.setTextSize(1);
    if(s_tab==TAB_UPDATE) drawUpdateTab(t,g);
    else if(s_tab==TAB_NOTES) drawNotesTab(t,g);
    else drawBoardTab(t,g);
    if(s_tab==TAB_UPDATE) {
        Theme::drawButton(t,b.updX,g.btnY,b.updW,BTN_H,"UPDATE",false);
        Theme::drawButton(t,b.laterX,g.btnY,b.laterW,BTN_H,"LATER",false);
    } else Theme::drawButton(t,b.closeX,g.btnY,b.closeW,BTN_H,"CLOSE",false);
    Theme::bubbleFontOff(t);return;
#endif
    raised(t, g.x, g.y, g.w, g.h);

    // Title bar, with the close box at its right end.
    t.fillRect(g.x + 3, g.y + 3, g.w - 6, TITLE_H, NAVY);
    t.setTextColor(Theme::WHITE, NAVY);
    t.setCursor(g.x + 8, g.y + 3 + (TITLE_H - 16) / 2);
    t.print("System Properties");
    const int cx = g.x + g.w - 6 - 16;
    Theme::bubbleFontOff(t);                        // the box holds no word
    Theme::drawWin95Button(t, cx, g.y + 5, 16, TITLE_H - 4, "", false);
    t.drawLine(cx + 5, g.y + 8, cx + 10, g.y + 13, Theme::W95_DKSHADOW);
    t.drawLine(cx + 10, g.y + 8, cx + 5, g.y + 13, Theme::W95_DKSHADOW);
    t.drawLine(cx + 6, g.y + 8, cx + 11, g.y + 13, Theme::W95_DKSHADOW);
    t.drawLine(cx + 11, g.y + 8, cx + 6, g.y + 13, Theme::W95_DKSHADOW);
    Theme::bubbleFontOn(t);

    // The tabs. The open one is a row taller and joins the panel below it,
    // which is the whole trick of a tab strip.
    for (uint8_t i = 0; i < TAB_N; i++) {
        const int tx = g.x + 4 + i * g.tabW;
        const bool on = (i == s_tab);
        raised(t, tx, on ? g.tabY - 2 : g.tabY, g.tabW, on ? TAB_H + 4 : TAB_H);
        bold(t, tx + (g.tabW - boldWidth(t, TAB_NAME[i])) / 2, g.tabY + (on ? 1 : 3),
             TAB_NAME[i], on ? NAVY : Theme::W95_DKSHADOW);
    }

    sunken(t, g.px, g.py, g.pw, g.ph);
    // The open tab's bottom edge is the panel's top edge: paint over the
    // seam so the two read as one surface.
    t.fillRect(g.x + 5 + s_tab * g.tabW, g.py, g.tabW - 2, 2, Theme::W95_FACE);

    if      (s_tab == TAB_UPDATE) drawUpdateTab(t, g);
    else if (s_tab == TAB_NOTES)  drawNotesTab(t, g);
    else                          drawBoardTab(t, g);

    // The buttons. UPDATE NOW only where it belongs -- a button that acts on
    // the other tabs' content would be a button that means different things
    // in different places.
    if (s_tab == TAB_UPDATE) {
        boldButton(t, b.laterX, g.btnY, b.laterW, BTN_H, "Later");
        boldButton(t, b.updX, g.btnY, b.updW, BTN_H, "Update now");
        // The default button, the one a keyboard would have focused.
        t.drawRect(b.updX - 2, g.btnY - 2, b.updW + 4, BTN_H + 4, Theme::W95_DKSHADOW);
    } else {
        boldButton(t, b.closeX, g.btnY, b.closeW, BTN_H, "Close");
    }
    // Every other screen assumes the small face.
    Theme::bubbleFontOff(t);
}

uint8_t uiSysPropsStops(TFT_eSPI& t, int16_t* xs, int16_t* ys, uint8_t cap) {
    const Geom g = geom(t);
    const Buttons b = buttons(t, g);
    Theme::bubbleFontOff(t);
    uint8_t n = 0;
    auto add = [&](int x, int y) { if (n < cap) { xs[n] = (int16_t)x; ys[n] = (int16_t)y; n++; } };
    if (s_tab == TAB_UPDATE) {
        add(b.updX + b.updW / 2, g.btnY + BTN_H / 2);
        add(b.laterX + b.laterW / 2, g.btnY + BTN_H / 2);
    } else {
        add(b.closeX + b.closeW / 2, g.btnY + BTN_H / 2);
    }
    for (uint8_t i = 0; i < TAB_N; i++) add(g.x + 4 + i * g.tabW + g.tabW / 2, g.tabY + TAB_H / 2);
#if defined(M5_STOPWATCH)
    if(s_tab==TAB_UPDATE) add(g.px+g.pw/2,checkboxY(g)+8);
#endif
    return n;
}

SysPropsHit uiSysPropsTouch(TFT_eSPI& t, int x, int y) {
    const Geom g = geom(t);
    const Buttons b = buttons(t, g);
    Theme::bubbleFontOff(t);

    // Outside the window: the same as LATER. A modal you cannot walk away
    // from is a modal that traps a board whose touch is badly calibrated.
    if (x < g.x || x >= g.x + g.w || y < g.y || y >= g.y + g.h) return SysPropsHit::CLOSE;

    const int cx = g.x + g.w - 6 - 16;
#if !defined(M5_STOPWATCH)
    if (in(x, y, cx, g.y + 5, 16, TITLE_H - 4)) return SysPropsHit::CLOSE;
#endif

    for (uint8_t i = 0; i < TAB_N; i++)
        if (in(x, y, g.x + 4 + i * g.tabW, g.tabY - 2, g.tabW, TAB_H + 4)) { s_tab = i; return SysPropsHit::NONE; }

    if (s_tab == TAB_UPDATE) {
        if (in(x, y, b.laterX, g.btnY, b.laterW, BTN_H)) return SysPropsHit::CLOSE;
        if (in(x, y, b.updX, g.btnY, b.updW, BTN_H))     return SysPropsHit::UPDATE_NOW;
        // The checkbox, and its words: a label you can tap is the difference
        // between a setting people find and one they do not.
        if (!Theme::compact() && in(x, y, g.px + 8, checkboxY(g), g.pw - 16, 16)) { Settings::toggleUpdateCheck(); return SysPropsHit::NONE; }
    } else if (in(x, y, b.closeX, g.btnY, b.closeW, BTN_H)) {
        return SysPropsHit::CLOSE;
    }
    return SysPropsHit::NONE;
}
