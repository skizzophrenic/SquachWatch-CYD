// SquachWatch-CYD — the SquachMesh menu. See include/ui_meshmenu.h.
#include "ui_meshmenu.h"

#if SQUACH_MESH

#include "theme.h"
#include "settings.h"
#include "squachy.h"
#include "detection.h"
#include <Arduino.h>

namespace {

const int TOP_MARGIN = 34;   // room for the heading
const uint8_t ROW_N = 4;

// Row height from live font metrics, shared by drawing and hit-testing so
// the two cannot drift -- the same reason every other row list in this
// codebase computes it rather than hard-coding it.
void geom(TFT_eSPI& t, int& top, int& rowH) {
    top = TOP_MARGIN;
    t.setTextSize(2);
    rowH = t.fontHeight() + 10;
}

void row(TFT_eSPI& t, int w, int y, int hgt, const char* label,
         const char* value, uint16_t valueCol) {
    t.setTextSize(2);
    t.setTextWrap(false);
    t.setTextColor(Theme::CYAN, Theme::BG);
    t.setCursor(8, y + (hgt - t.fontHeight()) / 2);
    t.print(label);
    if (!value) return;
    t.setTextColor(valueCol, Theme::BG);
    const int vw = t.textWidth(value);
    t.setCursor(w - 8 - vw, y + (hgt - t.fontHeight()) / 2);
    t.print(value);
}

} // namespace

void uiMeshMenuInit(TFT_eSPI& t) {
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
}

void uiMeshMenuTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng) {
    const int w = t.width(), h = t.height();

    // Same knocked-back backdrop the other sub-screens use, so this reads as
    // part of the device rather than as a dialog bolted on.
    Theme::Palette saved = Theme::dimPaletteForOverlay(150);
    Theme::drawActiveBackground(t, now, 0, h, eng);
    Theme::restorePalette(saved);
    Theme::dimRegion(t, 0, 0, w, h, 128);

    t.setTextSize(2);
    t.setTextWrap(false);
    t.setTextColor(Theme::VAPOR_PINK, Theme::BG);
    t.setCursor(8, 8);
    t.print("SQUACHMESH");

    int top, rowH;
    geom(t, top, rowH);

    const char* nm = Squachy::customName();
    if (!nm) nm = Squachy::nickname();

    // Transmit is coloured by state rather than just labelled, because it is
    // the row with a consequence: green reads as "you are visible to anybody
    // scanning", which is worth being able to see at a glance rather than
    // read.
    row(t, w, top + 0 * rowH, rowH, "DETECT",   Settings::meshDetectLabel(),
        Settings::meshDetect() ? Theme::GREEN : Theme::W95_SHADOW);
    row(t, w, top + 1 * rowH, rowH, "TRANSMIT", Settings::meshTransmitLabel(),
        Settings::meshTransmit() ? Theme::AMBER : Theme::W95_SHADOW);
    row(t, w, top + 2 * rowH, rowH, "NAME",     nm, Theme::VAPOR_YELLOW);
    row(t, w, top + 3 * rowH, rowH, "[ BACK ]", nullptr, Theme::CYAN);

    // One line saying what the two switches actually mean together, because
    // "DETECT off, TRANSMIT on" is not self-evidently "they can see you but
    // you cannot see them".
    const bool d = Settings::meshDetect(), x = Settings::meshTransmit();
    const char* note = d && x ? "You see them, they see you."
                     : d      ? "You see them. They cannot see you."
                     : x      ? "They see you. You cannot see them."
                              : "Off. Nothing sent, nothing shown.";
    t.setTextSize(1);
    t.setTextColor(Theme::W95_LIGHT, Theme::BG);
    t.setCursor(8, top + ROW_N * rowH + 6);
    t.print(note);
}

MeshMenuRow uiMeshMenuHitTest(TFT_eSPI& t, int x, int y, int screenW, int screenH) {
    (void)screenW; (void)screenH;
    int top, rowH;
    geom(t, top, rowH);
    if (x < 0 || y < top) return MeshMenuRow::NONE;
    const int idx = (y - top) / rowH;
    if (idx < 0 || idx >= (int)ROW_N) return MeshMenuRow::NONE;
    return (MeshMenuRow)idx;
}

#endif // SQUACH_MESH
