// SquachWatch-CYD — the iBeacon warning. See include/ui_beaconwarn.h.
#include "ui_beaconwarn.h"
#include "theme.h"
#include "detection.h"
#include <Arduino.h>

namespace {

// What it is, how often it will speak up, and that it is reversible -- in
// that order, so the part people underestimate comes before the reassurance.
//
// Every claim here is the code's. "Takes over the screen": an iBeacon match is
// exact, graded HIGH, so it passes any ALERT FILTER setting. "The first time it
// is heard": postBle() logs a new MAC and the ALERT follows. "Again after a
// minute out of range": expireStale() retires an entry after STALE_MS (60 s)
// without a sighting, and the next one reactivates it with a fresh firstSeen,
// which is what the ALERT is keyed on. If any of that changes, so must this.
const char* const PARAS[] = {
    "An iBeacon is a small radio that shops, stadiums, museums and airports fix to walls and shelves. It does not track you by itself: it repeats an ID, and an app already on your phone hears it and reports where you are.",
    "Each beacon takes over the screen the first time it is heard, and again whenever it comes back after a minute out of range.",
    "One shop can have dozens. Walking in can mean dozens of alerts in a row.",
    "Nothing else changes, and you can switch it off again here at any time.",
};
const uint8_t PARA_N = sizeof(PARAS) / sizeof(PARAS[0]);

const int BTN_W = 104, BTN_H = 28, BTN_GAP = 16;

// Shared by drawing and hit testing, so the two cannot drift apart.
void buttonGeom(TFT_eSPI& t, int& enX, int& offX, int& y) {
    const int w = t.width(), h = t.height();
    const int total = BTN_W * 2 + BTN_GAP;
    enX  = (w - total) / 2;
    offX = enX + BTN_W + BTN_GAP;
    y    = h - BTN_H - 10;
}

} // namespace

void uiBeaconWarnInit(TFT_eSPI& t) {
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
}

void uiBeaconWarnTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng) {
    const int w = t.width(), h = t.height();

    // Knocked well back: this is small text that has to be read.
    Theme::Palette saved = Theme::dimPaletteForOverlay(150);
    Theme::drawActiveBackground(t, now, 0, h, eng);
    Theme::restorePalette(saved);
    Theme::dimRegion(t, 0, 0, w, h, 110);

    t.setTextWrap(false);
    t.setTextSize(2);
    t.setTextColor(Theme::VAPOR_PINK, Theme::BG);
    t.setCursor(8, 6);
    t.print("IBEACONS");

    t.setTextSize(1);
    t.setTextColor(Theme::AMBER, Theme::BG);
    t.setCursor(8, 26);
    t.print("THIS CAN BE A LOT OF ALERTS");

    // wrapText fills fixed 48-character rows, so the width is capped at what
    // 47 characters take whatever the panel's width -- see ui_meshwarn.cpp.
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
        y += 4;
    }

    int enX, offX, by;
    buttonGeom(t, enX, offX, by);

    // The question sits with the buttons, so it is the last thing read.
    t.setTextColor(Theme::CYAN, Theme::BG);
    const char* q = "Alert on iBeacons?";
    t.setCursor((w - t.textWidth(q)) / 2, by - 14);
    t.print(q);

    Theme::drawWin95Button(t, enX,  by, BTN_W, BTN_H, "ENABLE",        false);
    Theme::drawWin95Button(t, offX, by, BTN_W, BTN_H, "KEEP DISABLED", false);
}

BeaconWarnHit uiBeaconWarnHitTest(TFT_eSPI& t, int x, int y) {
    int enX, offX, by;
    buttonGeom(t, enX, offX, by);
    const int SLOP = 6;
    if (y < by - SLOP || y > by + BTN_H + SLOP) return BeaconWarnHit::NONE;
    // The gap between them is narrower than two slops, so each side only
    // reaches halfway into it: a tap between the two picks neither.
    const int mid = offX - BTN_GAP / 2;
    if (x >= enX - SLOP && x < mid && x <= enX + BTN_W + SLOP)   return BeaconWarnHit::ENABLE;
    if (x >= mid && x >= offX - SLOP && x <= offX + BTN_W + SLOP) return BeaconWarnHit::KEEP_OFF;
    return BeaconWarnHit::NONE;
}
