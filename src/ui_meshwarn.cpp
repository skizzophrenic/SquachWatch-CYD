// SquachWatch-CYD — the SquachMesh consent gate. See include/ui_meshwarn.h.
#include "ui_meshwarn.h"

#if SQUACH_MESH

#include "theme.h"
#include "settings.h"
#include "detection.h"
#include <Arduino.h>

namespace {

// Written as paragraphs rather than one block so the wrap can breathe, and
// so the order is an argument: what goes out, what anybody can do with it,
// and which half of the feature is harmless.
//
// Every number here is real. 1.5 seconds is ADV_MS in detection.cpp; the
// address is the stable public one (see the note in include/squachmesh.h --
// it stays stable because ignoring a specific peer needs something stable to
// key on); the name and appearance are literally what the payload carries.
// If any of those change, this text is wrong and has to change with them.
//
// Messages added one line, and the rest got shorter to make room for it. A
// message's words are encrypted; the fact of sending one is not -- the frame
// rides in a scan response anyone can see for thirty seconds (SEND_MS in
// meshtalk.cpp). That is a real disclosure and it goes on this screen, not in
// a menu note somebody may never read.
const char* const PARAS[] = {
    "TRANSMIT broadcasts a Bluetooth advert every 1.5 seconds, carrying a fixed address that never changes and your Squachy's name and outfit.",
    "Anyone nearby with a scanner can log that address with a time and a place. Because it never changes, those sightings join up into a record of where you have been.",
    "The same trick this device warns you about.",
    "Messages are encrypted. That you sent one is not.",
    "DETECT only listens. It broadcasts nothing.",
};
const uint8_t PARA_N = sizeof(PARAS) / sizeof(PARAS[0]);

const int BTN_W = 92, BTN_H = 28, BTN_GAP = 18;

// Buttons sit a fixed distance off the bottom, and both drawing and hit
// testing come through here so they cannot drift apart -- the same reason
// every other two-part control in this codebase shares its geometry.
void buttonGeom(TFT_eSPI& t, int& yesX, int& noX, int& y) {
    const int w = t.width(), h = t.height();
    const int total = BTN_W * 2 + BTN_GAP;
    yesX = (w - total) / 2;
    noX  = yesX + BTN_W + BTN_GAP;
    y    = h - BTN_H - 10;
}

} // namespace

void uiMeshWarnInit(TFT_eSPI& t) {
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
}

void uiMeshWarnTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng) {
    const int w = t.width(), h = t.height();

    // Knocked back further than the menu behind it (110 against 128). This
    // screen is all small text and it has to be read, not glanced at.
    Theme::Palette saved = Theme::dimPaletteForOverlay(150);
    Theme::drawActiveBackground(t, now, 0, h, eng);
    Theme::restorePalette(saved);
    Theme::dimRegion(t, 0, 0, w, h, 110);

    t.setTextWrap(false);
    t.setTextSize(2);
    t.setTextColor(Theme::VAPOR_PINK, Theme::BG);
    t.setCursor(8, 6);
    t.print("SQUACHMESH");

    t.setTextSize(1);
    t.setTextColor(Theme::AMBER, Theme::BG);
    t.setCursor(8, 26);
    t.print("THIS MAKES YOU TRACKABLE");

    // Theme::wrapText fills fixed 48-char rows, so the wrap width is capped
    // at what 47 characters occupy regardless of how wide the panel is. At
    // 320px the margins alone would allow 50, and the two extra characters
    // would be written past the end of the row.
    const int charW = t.textWidth("M");
    int maxW = w - 16;
    if (maxW > 47 * charW) maxW = 47 * charW;

    const int lineH = t.fontHeight() + 1;
    int y = 40;
    t.setTextColor(Theme::WHITE, Theme::BG);
    for (uint8_t p = 0; p < PARA_N; p++) {
        char lines[8][48];
        const uint8_t n = Theme::wrapText(t, PARAS[p], maxW, lines, 8);
        for (uint8_t i = 0; i < n; i++) {
            t.setCursor(8, y);
            t.print(lines[i]);
            y += lineH;
        }
        y += 4;                      // a gap between paragraphs, not a line
    }

    int yesX, noX, by;
    buttonGeom(t, yesX, noX, by);

    // The question sits with the buttons rather than at the top, so whatever
    // is being answered is the last thing read before answering it.
    t.setTextColor(Theme::CYAN, Theme::BG);
    const char* q = "Turn on SquachMesh?";
    t.setCursor((w - t.textWidth(q)) / 2, by - 14);
    t.print(q);

    // NO is not the quiet one. A consent dialog whose decline is styled as
    // the lesser option is doing the opposite of what it is for, so both are
    // ordinary system buttons and neither is preselected.
    Theme::drawWin95Button(t, yesX, by, BTN_W, BTN_H, "YES", false);
    Theme::drawWin95Button(t, noX,  by, BTN_W, BTN_H, "NO",  false);
}

MeshWarnHit uiMeshWarnHitTest(TFT_eSPI& t, int x, int y) {
    int yesX, noX, by;
    buttonGeom(t, yesX, noX, by);
    const int SLOP = 6;
    if (y < by - SLOP || y > by + BTN_H + SLOP) return MeshWarnHit::NONE;
    if (x >= yesX - SLOP && x <= yesX + BTN_W + SLOP) return MeshWarnHit::YES;
    if (x >= noX  - SLOP && x <= noX  + BTN_W + SLOP) return MeshWarnHit::NO;
    return MeshWarnHit::NONE;
}

#endif // SQUACH_MESH
