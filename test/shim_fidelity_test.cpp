// Does the emulator draw what the hardware draws?
//
// This project decides geometry by measuring rendered emulator frames --
// Squachy's scale, the pet's perch, the counter rows, the bubble headroom
// were all settled by looking at sim output. That makes sim/TFT_eSPI.h a
// measuring instrument, and an instrument that disagrees with the hardware
// is worse than no instrument: it produces confident wrong answers.
//
// It had four disagreements, all found at once:
//
//   * drawRoundRect stamped a FULL circle at each corner instead of one
//     quadrant, so three quarters of each landed inside the shape, on top
//     of the fill. Toasters and speech bubbles wore them.
//   * the GLCD cell was treated as 7 rows when the real one is 8, so
//     descenders were clipped and fontHeight() came back a pixel short per
//     text size -- which on the LOG screen is 3 pixels a row, an entire
//     extra row of detections the device never shows.
//   * drawChar painted 5 columns of background where the real one paints 6,
//     leaving a stripe of background showing between every pair of letters.
//   * fillEllipse accepted radius 1, which the real library rejects, so
//     small scaled sprites could show detail the hardware never draws.
//
// Every check below is against a value read out of the vendored TFT_eSPI
// source, not against what looked right.
#include "TFT_eSPI.h"
#include "test_util.h"
#include <cstring>

static const uint16_t FILL = 0xFFFF;
static const uint16_t EDGE = 0x001F;
static const uint16_t BLANK = 0x0000;

int main() {
    suite("drawRoundRect draws corner arcs, not whole circles");
    {
        TFT_eSPI t(200, 120);
        t.fillScreen(BLANK);
        const int x = 20, y = 20, w = 160, h = 80, r = 20;
        t.fillRoundRect(x, y, w, h, r, FILL);
        t.drawRoundRect(x, y, w, h, r, EDGE);

        // Anything of the outline colour deep inside the shape is a stray.
        // "Deep inside" = inboard of both corner centres, where no legitimate
        // arc can reach.
        int stray = 0;
        for (int py = y + r + 1; py < y + h - r - 1; py++)
            for (int px = x + r + 1; px < x + w - r - 1; px++)
                if (t.readPixel(px, py) == EDGE) stray++;
        ck("no outline pixels inside the fill", stray == 0);

        // And the arcs must actually exist, or "zero strays" is trivially
        // satisfied by drawing nothing at all.
        int cornerPixels = 0;
        for (int py = y; py < y + r; py++)
            for (int px = x; px < x + r; px++)
                if (t.readPixel(px, py) == EDGE) cornerPixels++;
        ck("the top-left corner still has an arc", cornerPixels > r / 2);

        // The outside of a corner is beyond the arc and must stay empty.
        ck("the corner is cut away, not filled", t.readPixel(x, y) == BLANK);
        // Mid-edge is a straight run and must be drawn.
        ck("the top edge is drawn", t.readPixel(x + w / 2, y) == EDGE);
        ck("the left edge is drawn", t.readPixel(x, y + h / 2) == EDGE);
    }

    suite("The GLCD cell is 8 rows and 6 columns");
    {
        TFT_eSPI t(64, 32);
        ck("fontHeight() is 8 at size 1", t.fontHeight() == 8);
        t.setTextSize(2);
        ck("...and 16 at size 2", t.fontHeight() == 16);
        t.setTextSize(3);
        ck("...and 24 at size 3", t.fontHeight() == 24);
        t.setTextSize(1);
        // fontdata[1] in the real library is { ..., height 8, baseline 7 }.
        ck("fontHeight(1) agrees", t.fontHeight(1) == 8);
        // Font 2 is not compiled into this firmware (LOAD_FONT2 was dropped
        // to save flash), so the real fontdata[2].height is 0. The shim
        // returning 0 is therefore correct, not a stub -- and it means
        // ui_clear's fontHeight(2) call, which reads as "height at size 2"
        // but asks for "font 2", behaves the same in both places today.
        // It would stop doing so the moment anyone enabled LOAD_FONT2.
        ck("fontHeight(2) is 0 on both sides", t.fontHeight(2) == 0);
    }

    suite("Opaque text fills the whole advance width");
    {
        TFT_eSPI t(64, 32);
        t.fillScreen(EDGE);            // something conspicuous underneath
        t.setTextColor(FILL, BLANK);   // opaque: background is BLANK
        t.setTextSize(1);
        t.setCursor(0, 0);
        t.print("HH");

        // Column 5 of the first cell is the spacer. Real TFT_eSPI writes it
        // in the background colour; leaving it alone showed a 1px bar of
        // whatever was underneath between every pair of characters.
        int showThrough = 0;
        for (int row = 0; row < 8; row++)
            if (t.readPixel(5, row) == EDGE) showThrough++;
        ck("the spacer column is painted, not skipped", showThrough == 0);

        // The 8th row must be painted too -- it is part of the cell.
        int row8 = 0;
        for (int col = 0; col < 6; col++)
            if (t.readPixel(col, 7) == EDGE) row8++;
        ck("row 8 of the cell is painted", row8 == 0);
    }

    suite("Descenders reach the eighth row");
    {
        // Only meaningful if the font actually puts ink there. Ask the table
        // rather than assuming: bit 7 of a column byte is the bottom row.
        bool anyInk = false;
        for (int c = 32; c < 127 && !anyInk; c++)
            for (int col = 0; col < 5; col++)
                if (font[(size_t)c * 5 + col] & 0x80) { anyInk = true; break; }
        ck("the GLCD font uses the bottom row at all", anyInk);

        if (anyInk) {
            // Find a character that uses it and check it renders.
            int glyph = -1;
            for (int c = 32; c < 127 && glyph < 0; c++)
                for (int col = 0; col < 5; col++)
                    if (font[(size_t)c * 5 + col] & 0x80) { glyph = c; break; }
            TFT_eSPI t(32, 32);
            t.fillScreen(BLANK);
            t.setTextColor(FILL);
            t.setTextSize(1);
            t.setCursor(0, 0);
            t.drawChar((uint16_t)glyph, 0, 0, 1);
            int lit = 0;
            for (int col = 0; col < 5; col++)
                if (t.readPixel(col, 7) == FILL) lit++;
            char msg[80];
            snprintf(msg, sizeof(msg), "'%c' draws ink on the bottom row", (char)glyph);
            ck(msg, lit > 0);
        }
    }

    suite("Ellipses reject the radii the hardware rejects");
    {
        TFT_eSPI t(64, 64);
        t.fillScreen(BLANK);
        // Real TFT_eSPI: `if (rx<2) return; if (ry<2) return;`
        t.fillEllipse(32, 32, 1, 10, FILL);
        int drawn = 0;
        for (int y = 0; y < 64; y++)
            for (int x = 0; x < 64; x++)
                if (t.readPixel(x, y) == FILL) drawn++;
        ck("rx of 1 draws nothing, as on hardware", drawn == 0);

        t.fillEllipse(32, 32, 10, 1, FILL);
        drawn = 0;
        for (int y = 0; y < 64; y++)
            for (int x = 0; x < 64; x++)
                if (t.readPixel(x, y) == FILL) drawn++;
        ck("ry of 1 draws nothing either", drawn == 0);

        // ...and 2 still works, so the guard is a boundary and not a wall.
        t.fillEllipse(32, 32, 2, 2, FILL);
        drawn = 0;
        for (int y = 0; y < 64; y++)
            for (int x = 0; x < 64; x++)
                if (t.readPixel(x, y) == FILL) drawn++;
        ck("radius 2 still draws", drawn > 0);
    }

    return report();
}
