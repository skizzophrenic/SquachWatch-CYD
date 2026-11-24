#if defined(TDECK_PLUS) || defined(SQW_CHAT_TEXT_TEST)
#include "chat_text.h"

#include <stddef.h>
#include <stdint.h>

extern "C" const uint8_t sqw_tw_chat_font_start[];
extern "C" const uint8_t sqw_tw_chat_font_end[];

namespace ChatText {
namespace {

struct Glyph {
    uint16_t codepoint;
    uint16_t rows[FULL_GLYPH];
};
static_assert(sizeof(Glyph) == 34, "The bitmap layout must match the generator");

struct Character {
    uint32_t codepoint;
    const char* next;
};

Character decode(const char* text) {
    const uint8_t first = static_cast<uint8_t>(*text);
    if (first < 0x80) return {first, text + 1};

    uint32_t codepoint;
    uint8_t bytes;
    if (first >= 0xc2 && first <= 0xdf) {
        codepoint = first & 0x1f;
        bytes = 2;
    } else if (first >= 0xe0 && first <= 0xef) {
        codepoint = first & 0x0f;
        bytes = 3;
    } else if (first >= 0xf0 && first <= 0xf4) {
        codepoint = first & 0x07;
        bytes = 4;
    } else {
        return {0xfffd, text + 1};
    }

    for (uint8_t i = 1; i < bytes; ++i) {
        const uint8_t next = static_cast<uint8_t>(text[i]);
        if ((next & 0xc0) != 0x80) return {0xfffd, text + 1};
        codepoint = (codepoint << 6) | (next & 0x3f);
    }
    if ((bytes == 3 && codepoint < 0x800) || (bytes == 4 && codepoint < 0x10000) ||
        (codepoint >= 0xd800 && codepoint <= 0xdfff) || codepoint > 0x10ffff) {
        return {0xfffd, text + 1};
    }
    return {codepoint, text + bytes};
}

const Glyph* findGlyph(uint32_t codepoint) {
    if (codepoint > 0xffff) return nullptr;
    const auto* glyphs = reinterpret_cast<const Glyph*>(sqw_tw_chat_font_start);
    const uintptr_t size = reinterpret_cast<uintptr_t>(sqw_tw_chat_font_end) -
                           reinterpret_cast<uintptr_t>(sqw_tw_chat_font_start);
    size_t first = 0;
    size_t last = size / sizeof(Glyph);
    while (first < last) {
        const size_t middle = first + (last - first) / 2;
        if (glyphs[middle].codepoint == codepoint) return glyphs + middle;
        if (glyphs[middle].codepoint < codepoint) first = middle + 1;
        else last = middle;
    }
    return nullptr;
}

char latin(uint32_t codepoint) {
    return codepoint < 32 || codepoint == 127 ? ' ' : static_cast<char>(codepoint);
}

int advance(TFT_eSPI& target, uint32_t codepoint, int size) {
    if (codepoint >= 128) return size;
    const char ascii[]{latin(codepoint), '\0'};
    return target.textWidth(ascii);
}

void drawGlyph(TFT_eSPI& target, uint32_t codepoint, int x, int y,
               uint16_t color, int size) {
    const Glyph* glyph = findGlyph(codepoint);
    if (!glyph) {
        target.drawRect(x + 2, y + 2, size - 4, size - 4, color);
        return;
    }
    for (int iy = 0; iy < size; ++iy) {
        const uint16_t row = glyph->rows[iy * FULL_GLYPH / size];
        if (!row) continue;
        int run = -1;
        for (int ix = 0; ix <= size; ++ix) {
            const bool on = ix < size && (row & (0x8000u >> (ix * FULL_GLYPH / size)));
            if (on && run < 0) run = ix;
            if (!on && run >= 0) {
                target.drawFastHLine(x + run, y + iy, ix - run, color);
                run = -1;
            }
        }
    }
}

int drawRange(TFT_eSPI& target, const char* p, const char* end, int x, int y,
              int width, uint16_t color, uint16_t background, int size, int ascent) {
    const int right = x + width;
    target.setTextColor(color, background);
    while (p != end && *p) {
        const Character c = decode(p);
        const int step = advance(target, c.codepoint, size);
        if (x + step > right) break;
        if (c.codepoint < 128) {
            target.setCursor(x, y + ascent + (size == COMPACT_GLYPH ? 2 : 0));
            target.write(static_cast<uint8_t>(latin(c.codepoint)));
        } else {
            drawGlyph(target, c.codepoint, x, y, color, size);
        }
        x += step;
        p = c.next;
    }
    target.setCursor(x, y + ascent);
    return x;
}

} // namespace

bool needsFallback(const char* text) {
    for (; *text; ++text) if (static_cast<uint8_t>(*text) >= 128) return true;
    return false;
}

int drawClipped(TFT_eSPI& target, const char* text, int x, int y, int width,
                uint16_t color, uint16_t background, int glyphSize, int ascent) {
    return drawRange(target, text, nullptr, x, y, width, color, background, glyphSize, ascent);
}

int drawWrapped(TFT_eSPI& target, const char* text, int x, int y, int width,
                int lineHeight, int limitY, uint16_t color, uint16_t background,
                int ascent, bool draw) {
    const char* p = text;
    int lines = 0;
    while (*p) {
        while (*p == ' ') ++p;
        if (!*p) break;
        const char* end = p;
        const char* wordBreak = nullptr;
        int used = 0;
        while (*end && *end != '\n') {
            const Character c = decode(end);
            const int step = advance(target, c.codepoint, FULL_GLYPH);
            if (used + step > width) {
                if (wordBreak && wordBreak > p) end = wordBreak;
                break;
            }
            used += step;
            if (c.codepoint == ' ') wordBreak = end;
            end = c.next;
        }
        if (draw && y + (lines + 1) * lineHeight <= limitY) {
            drawRange(target, p, end, x, y + lines * lineHeight, width, color,
                      background, FULL_GLYPH, ascent);
        }
        ++lines;
        if (*end == '\n') p = end + 1;
        else p = end == p ? decode(p).next : end;
    }
    return lines ? lines : 1;
}

} // namespace ChatText
#endif
