#include "test_util.h"
#include "band_frame.h"
#include <TFT_eSPI.h>
#include <vector>

// A full-frame reference catches lost rows, incorrect viewport offsets and
// a final push that repeats pixels below the physical panel.
static void checkScreen(int w, int h) {
    TFT_eSPI panel(w, h);
    TFT_eSprite reference(&panel);
    reference.setColorDepth(8);
    reference.createSprite(w, h);
    TFT_eSprite frame(&panel);
    frame.setColorDepth(8);
    frame.createSprite(w, FrameLayout::bufferHeight(w, h));
    std::vector<int> rowVisits(h, 0);
    int advances = 0, draws = 0, pushed = 0;
    auto scene = [&](TFT_eSPI& t, bool advance) {
        if (advance) ++advances;
        ++draws;
        for (int y = 0; y < h; ++y) {
            if (!DrawBand::has(y, y + 1)) continue;
            t.drawFastHLine(0, y, w, (uint16_t)((y * 131 + 17) & 0xffff));
        }
        t.setTextColor(TFT_WHITE, TFT_BLACK);
        t.setCursor(4, FrameLayout::bufferHeight(w, h) - 5);
        t.print("crosses the buffer boundary");
        t.setCursor(4, h - 12);
        t.print("bottom controls");
        t.drawRect(0, 0, w, h, TFT_RED);
    };
    DrawBand::all();
    scene(reference, false);
    draws = 0;
    BandFrame::render(frame, w, h, TFT_BLACK, scene, [&](int y, int rows) {
        ck("push stays within the physical screen", y >= 0 && rows > 0 && y + rows <= h);
        pushed += rows;
        for (int r = 0; r < rows; ++r) {
            ++rowVisits[y + r];
            for (int x = 0; x < w; ++x)
                panel.drawPixel(x, y + r, frame.readPixel(x, y + r));
        }
    });
    ck("all screen rows pushed once", pushed == h);
    bool once = true, equal = true;
    for (int y = 0; y < h; ++y) {
        once &= rowVisits[y] == 1;
        for (int x = 0; x < w; ++x)
            equal &= panel.readPixel(x, y) == reference.readPixel(x, y);
    }
    ck("no duplicated or missing rows", once);
    ck("text, border and every pixel match full-frame rendering", equal);
    ck("animation advances exactly once", advances == 1);
    ck("two drawing passes", draws == 2);
    ck("sprite viewport restored", frame.height() == FrameLayout::bufferHeight(w, h));
    ck("drawing range restored", DrawBand::has(0, 1) && DrawBand::has(h, h + 1));
}

int main() {
    suite("Two-thirds buffer remains safe through all rotations");
    const int capacity = FrameLayout::allocationWidth(240) * FrameLayout::allocationHeight(320);
    ck("one fixed pixel allocation", capacity == 51200);
    ck("landscape rows", FrameLayout::bufferHeight(320, 240) == 160);
    ck("portrait rows", FrameLayout::bufferHeight(240, 320) == 213);
    for (int r = 0; r < 8; ++r) {
        const int w = r & 1 ? 320 : 240, h = r & 1 ? 240 : 320;
        ck("rotation never requires a larger allocation", w * FrameLayout::bufferHeight(w, h) <= capacity);
    }
    checkScreen(320, 240);
    checkScreen(240, 320);

    suite("Rows from a previous screen cannot leak between bands");
    TFT_eSPI panel(320, 240);
    TFT_eSprite frame(&panel);
    frame.createSprite(320, 160);
    frame.fillScreen(TFT_RED);
    bool clean = true;
    BandFrame::render(frame, 320, 240, TFT_BLACK, [&](TFT_eSPI& t, bool) {
        // Incremental screens may leave pixels untouched. Each pass begins
        // with a clean background rather than the other band's old content.
        for (int y = DrawBand::top(0); y < DrawBand::bot(240); ++y)
            clean &= t.readPixel(319, y) == TFT_BLACK;
    }, [](int, int) {});
    ck("unpainted pixels start at background", clean);
    return report();
}
