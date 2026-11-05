// SquachWatch-CYD — the SquachMesh menu. See include/ui_meshmenu.h.
#include "ui_meshmenu.h"

#if SQUACH_MESH

#include "theme.h"
#include "settings.h"
#include "squachy.h"
#include "detection.h"
#include "meshtalk.h"
#include <Arduino.h>
#include <string.h>
#include <stdio.h>

namespace {

// Under the heading. Asked at draw time: the heading grows on the large
// panels, and that is only known once setLarge() has run at boot.
int topMargin() { return Theme::LIST_TOP + Theme::listHeadingH(); }
const uint8_t ROW_N = 8;     // BACK is pinned to the bottom edge now, not a row

// Row height from live font metrics, shared by drawing and hit-testing so
// the two cannot drift -- the same reason every other row list in this
// codebase computes it rather than hard-coding it.
void geom(TFT_eSPI& t, int& top, int& rowH) {
    top = topMargin();
    t.setTextSize(Theme::uiMenuTextSize(t));
    rowH = t.fontHeight() + Theme::listRowPad();
    // Eight rows at that height run under BACK on a landscape panel, so there
    // they share what room there is instead.
    const int room = t.height() - Theme::pinnedBackH(t.width()) - 2 - top;
    // ...unless FONTS > MENUS made the letters too tall to share it: then
    // they keep their height and the list scrolls, as on a compact screen.
    if (ROW_N * rowH > room && !Theme::compact() && room / ROW_N >= t.fontHeight() + 4) rowH = room / ROW_N;
}

// On a compact screen the rows keep their height and the list scrolls
// instead: g_scroll is the first row shown.
int g_scroll = 0;
int visibleRows(TFT_eSPI& t, int top, int rowH) {
    const int room = t.height() - Theme::pinnedBackH(t.width()) - 2 - top;
    const int v = room / rowH;
    return v > (int)ROW_N ? (int)ROW_N : (v < 1 ? 1 : v);
}

void row(TFT_eSPI& t, int w, int y, int hgt, const char* label,
         const char* value, uint16_t valueCol) {
    if (y < 0) return;      // scrolled out of sight
    // A solid panel under the row, as the settings screen has. Without it
    // the labels sat straight on the dimmed backdrop, and the synthwave sun
    // came through the gaps in every word.
    Theme::drawListRowPanel(t, w, y, hgt);
    // FONTS > MENUS: a row the bigger size would run off the screen steps
    // back down, label and value together.
    Theme::menuFontOn(t, false);
    while (t.textWidth(label) + (value ? t.textWidth(value) + 8 : 0) > w - 16 && Theme::menuFontSmaller(t)) {}
    t.setTextWrap(false);
    t.setTextColor(Theme::VAPOR_PINK, Theme::BG);
    // On the large panels the taller heading leaves 31-row rows in landscape,
    // and centring on the row put the letters' backing over the panel's
    // bottom edge; centred on the panel (drawListRowPanel's inset) they clear.
    const int ty = Theme::large() ? y + 1 + (hgt - 4 - t.fontHeight()) / 2
                                  : y + (hgt - t.fontHeight()) / 2;
    t.setCursor(8, ty);
    t.print(label);
    if (value) {
        t.setTextColor(valueCol, Theme::BG);
        const int vw = t.textWidth(value);
        t.setCursor(w - 8 - vw, ty);
        t.print(value);
    }
    t.setTextFont(1);
}

} // namespace

void uiMeshMenuScroll(int delta) { g_scroll += delta; if (g_scroll < 0) g_scroll = 0; }

void uiMeshMenuInit(TFT_eSPI& t) {
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
}

void uiMeshMenuTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng, bool advance) {
    const int w = t.width(), h = t.height();

    // Same knocked-back backdrop the other sub-screens use, so this reads as
    // part of the device rather than as a dialog bolted on.
    Theme::Palette saved = Theme::dimPaletteForOverlay(150);
    Theme::drawActiveBackground(t, now, 0, h, eng, advance);
    Theme::restorePalette(saved);
    Theme::dimRegion(t, 0, 0, w, h, 128);

    Theme::drawListHeading(t, "SQUACHMESH", Theme::VAPOR_PINK);

    int top, rowH;
    geom(t, top, rowH);
    const int vis = visibleRows(t, top, rowH);
    if (g_scroll > (int)ROW_N - vis) g_scroll = (int)ROW_N - vis;
    if (g_scroll < 0) g_scroll = 0;
    auto rowY = [&](int k) { return (k < g_scroll || k >= g_scroll + vis) ? -1 : top + (k - g_scroll) * rowH; };

    const char* nm = Squachy::customName();
    if (!nm) nm = Squachy::nickname();

    // Transmit is coloured by state rather than just labelled, because it is
    // the row with a consequence: green reads as "you are visible to anybody
    // scanning", which is worth being able to see at a glance rather than
    // read.
    row(t, w, rowY(0), rowH, "DETECT",   Settings::meshDetectLabel(),
        Settings::meshDetect() ? Theme::GREEN : Theme::W95_SHADOW);
    row(t, w, rowY(1), rowH, "TRANSMIT", Settings::meshTransmitLabel(),
        Settings::meshTransmit() ? Theme::AMBER : Theme::W95_SHADOW);
    // MESSAGES says ERR rather than OFF when the crypto self-test failed at
    // boot: a switch that cannot be turned on should not look like one that
    // merely isn't.
    const bool st = MeshTalk::selfTestOk();
    row(t, w, rowY(2), rowH, "MESSAGES",
        !st ? "ERR" : (Settings::messagesOn() ? "ON" : "OFF"),
        !st ? Theme::RED : (Settings::messagesOn() ? Theme::GREEN : Theme::W95_SHADOW));
    // How many of them may be on the main screen at once. A tap steps it
    // here: it had a page of its own holding only this, and the desk's count
    // lives on the DESK MODE page.
    // Serious catches shared with the squad. Grey when it is on but cannot
    // do anything: no messages, or no phrase to seal them with.
    row(t, w, rowY(3), rowH, "HEADS-UP", Settings::meshHeadsUp() ? "ON" : "OFF",
        Settings::meshHeadsUp() && MeshTalk::ready() ? Theme::GREEN : Theme::W95_SHADOW);
    row(t, w, rowY(4), rowH, "CROWD", Settings::meshCrowdLabel(),
        Settings::meshCrowd() > 1 ? Theme::GREEN : Settings::meshCrowd() == 0 ? Theme::AMBER : Theme::W95_SHADOW);
    // Everybody who has ever held the phrase, here or not.
    char sq[12];
    const uint8_t members = MeshTalk::rosterCount();
    if (members) snprintf(sq, sizeof sq, "%u >", (unsigned)members);
    else         snprintf(sq, sizeof sq, "NONE >");
    row(t, w, rowY(5), rowH, "SQUAD", sq, members ? Theme::GREEN : Theme::W95_SHADOW);
    // Never the phrase itself. This screen is looked at over shoulders.
    row(t, w, rowY(6), rowH, "PHRASE",
        MeshTalk::havePhrase() ? "SET >" : "NONE >",
        MeshTalk::havePhrase() ? Theme::VAPOR_YELLOW : Theme::W95_SHADOW);
#if !defined(STICKS3)   // naming him means a keyboard: not on two buttons
    row(t, w, rowY(7), rowH, "NAME",     nm, Theme::VAPOR_YELLOW);
#endif

    // One line saying what the two switches actually mean together, because
    // "DETECT off, TRANSMIT on" is not self-evidently "they can see you but
    // you cannot see them".
    const bool d = Settings::meshDetect(), x = Settings::meshTransmit();
    const char* note = d && x ? "You see them, they see you."
                     : d      ? "You see them. They cannot see you."
                     : x      ? "They see you. You cannot see them."
                              : "Off. Nothing sent, nothing shown.";
    Theme::bodyFontOn(t);
    t.setTextColor(Theme::W95_LIGHT, Theme::BG);
    // Seven rows leave no room under them in landscape, so there the one
    // line that matters most sits beside the heading instead: a warning
    // about messages if there is one, else what the two switches mean.
    const bool below = top + vis * rowH + 6 + 2 * t.fontHeight() + 3 <= h - Theme::pinnedBackH(t.width()) - 2;
    if (below) {
        t.setCursor(8, top + vis * rowH + 6);
        t.print(note);
    }

    // And one for messages, which need both halves: DETECT to hear one,
    // TRANSMIT to answer.
    const char* mnote =
        !MeshTalk::selfTestOk()      ? "Messages off: crypto self-test failed."
      : !Settings::messagesOn()      ? nullptr
      : !MeshTalk::havePhrase()      ? "Messages need a phrase to share first."
      : !Settings::meshDetect()      ? "Messages need DETECT on to be heard."
      : !Settings::meshTransmit()    ? "Messages: you can read, not reply."
                                     : "Messages: reading and replying.";
    if (below && mnote) {
        t.setCursor(8, top + vis * rowH + 6 + t.fontHeight() + 3);
        t.print(mnote);
    }
    if (!below) {
        const bool warn = mnote && strcmp(mnote, "Messages: reading and replying.") != 0;
        const char* one = warn ? mnote : note;
        t.setTextColor(warn ? Theme::AMBER : Theme::W95_LIGHT, Theme::BG);
        t.setCursor(w - 8 - t.textWidth(one), Theme::listHeadingY(t));
        t.print(one);
    }
    Theme::bodyFontOff(t);
    Theme::drawPinnedBack(t, "[ BACK ]");
}

MeshMenuRow uiMeshMenuHitTest(TFT_eSPI& t, int x, int y, int screenW, int screenH) {
    (void)screenW; (void)screenH;
    int top, rowH;
    geom(t, top, rowH);
    if (x < 0 || y < top) return MeshMenuRow::NONE;
    const int vis = visibleRows(t, top, rowH);
    if ((y - top) / rowH >= vis) return MeshMenuRow::NONE;
    const int idx = (y - top) / rowH + g_scroll;
    if (idx < 0 || idx >= (int)ROW_N) return MeshMenuRow::NONE;
#if defined(STICKS3)
    if ((MeshMenuRow)idx == MeshMenuRow::NAME) return MeshMenuRow::NONE;
#endif
    return (MeshMenuRow)idx;
}

#endif // SQUACH_MESH
