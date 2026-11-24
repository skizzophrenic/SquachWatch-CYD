#include "chat_text.h"
#include "test_util.h"

namespace {
constexpr uint16_t INK = 0xffff;
constexpr uint16_t PAPER = 0;

unsigned ink(TFT_eSPI& display, int left, int top, int right, int bottom) {
    unsigned pixels = 0;
    for (int y = top; y < bottom; ++y)
        for (int x = left; x < right; ++x)
            if (display.readPixel(x, y) == INK) ++pixels;
    return pixels;
}

unsigned shadedInk(TFT_eSPI& display, int left, int top, int right, int bottom) {
    unsigned pixels = 0;
    for (int y = top; y < bottom; ++y) {
        for (int x = left; x < right; ++x) {
            const uint16_t pixel = display.readPixel(x, y);
            if (pixel != PAPER && pixel != INK) ++pixels;
        }
    }
    return pixels;
}
}

int main() {
    suite("Traditional Chinese LoRa chat");
    TFT_eSPI display(80, 48);
    ck("Chinese selects the atlas", ChatText::needsFallback("繁體中文"));
    ck("Plain ASCII uses the regular renderer", !ChatText::needsFallback("MediumFast"));

    const int x = ChatText::drawClipped(display, "臺灣中文", 0, 0, 32, INK, PAPER, ChatText::COMPACT_GLYPH);
    ck("Two 10 px glyphs fit at an 11 px pitch", x == 22 && ink(display, 0, 1, x, 11) > 12);
    ck("Preview uses shades between the text and background colors", shadedInk(display, 0, 1, x, 11) > 35);
    ck("Top and bottom padding are clear",
       ink(display, 0, 0, x, 1) == 0 && ink(display, 0, 11, x, 12) == 0 &&
       shadedInk(display, 0, 0, x, 1) == 0 && shadedInk(display, 0, 11, x, 12) == 0);
    ck("Clipping draws no part of the third glyph", ink(display, x, 0, 80, 12) == 0 &&
       shadedInk(display, x, 0, 80, 12) == 0);

    display.fillScreen(PAPER);
    const int lineHeight = ChatText::FULL_GLYPH + 1;
    const int lines = ChatText::drawWrapped(display, "繁體中文測試", 0, 0, 48, lineHeight,
                                           display.height(), INK, PAPER, 0, true);
    ck("Unspaced Chinese wraps on complete code points", lines == 2);
    ck("Both rows contain rendered glyphs",
       ink(display, 0, 0, 48, lineHeight) > 100 &&
       ink(display, 0, lineHeight, 48, 2 * lineHeight) > 100);
    ck("CJK punctuation and mixed Latin text fit",
       ChatText::drawClipped(display, "A臺灣。", 0, 0, 80, INK, PAPER, ChatText::COMPACT_GLYPH) == 39);
    ck("A truncated UTF-8 sequence is handled safely",
       ChatText::drawClipped(display, "\xe8\x87", 0, 0, 80, INK, PAPER, ChatText::FULL_GLYPH) == 32);
    return report();
}
