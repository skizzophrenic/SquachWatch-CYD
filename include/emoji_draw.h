// SquachWatch-CYD -- drawing text that may hold emoji tokens (emoji_cp.h).
//
// A token is two bytes and one PX-wide sprite; everything else is the current
// font's. These replace t.textWidth() / t.print() wherever message text is
// laid out, so the width a line is wrapped to is the width it is drawn at.
#pragma once
#include <TFT_eSPI.h>
#include "emoji_cp.h"

namespace Emoji {

static const int GAP = 1;                      // clear pixels after each sprite

// Pixels the first n bytes of s take in the font now set.
int width(TFT_eSPI& t, const char* s, size_t n);

// Draws the first n bytes of s with the text baseline/top at (x, textY) -- the
// cursor y the caller already uses -- and sprites with their top at emojiY.
// Colours are the ones set on t. Returns the x after the last thing drawn.
int draw(TFT_eSPI& t, int x, int textY, int emojiY, const char* s, size_t n);

}  // namespace Emoji
