#pragma once

#include "TFT_eSPI.h"

namespace ChatText {

constexpr int COMPACT_GLYPH = 12;
constexpr int FULL_GLYPH = 16;

bool needsFallback(const char* text);

// Draw only complete code points that fit. Returns the pixel position after
// the last character. For the bubble font, ascent positions its Latin baseline.
int drawClipped(TFT_eSPI& target, const char* text, int x, int y, int width,
                uint16_t color, uint16_t background, int glyphSize, int ascent = 0);

// Word-wrap Latin text and break unspaced CJK at glyph boundaries.
int drawWrapped(TFT_eSPI& target, const char* text, int x, int y, int width,
                int lineHeight, int limitY, uint16_t color, uint16_t background,
                int ascent, bool draw);

} // namespace ChatText
