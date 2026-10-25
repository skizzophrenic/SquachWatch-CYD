// SquachWatch-CYD -- text with emoji in it. See include/emoji_draw.h.
#if SQUACH_LORA
#include "emoji_draw.h"
#include <string.h>

namespace Emoji {
namespace {

// The plain run starting at s, up to the next token or n bytes: copied out so
// the font can measure it.
size_t run(const char* s, size_t n, char* buf, size_t cap) {
    size_t k = 0;
    while (k < n && k + 1 < cap && (uint8_t)s[k] != TOKEN) { buf[k] = s[k]; k++; }
    buf[k] = '\0';
    return k;
}

}  // namespace

int width(TFT_eSPI& t, const char* s, size_t n) {
    int w = 0;
    char buf[64];
    for (size_t i = 0; i < n; ) {
        if ((uint8_t)s[i] == TOKEN) {
            if (i + 1 < n && isToken(s + i)) { w += PX + GAP; i += 2; } else i++;
            continue;
        }
        const size_t k = run(s + i, n - i, buf, sizeof buf);
        w += t.textWidth(buf);
        i += k ? k : 1;
    }
    return w;
}

int draw(TFT_eSPI& t, int x, int textY, int emojiY, const char* s, size_t n) {
    char buf[64];
    for (size_t i = 0; i < n; ) {
        if ((uint8_t)s[i] == TOKEN) {
            if (i + 1 < n && isToken(s + i)) {
                const uint16_t* px = PIXELS[tokenIndex(s + i)];
                for (int yy = 0; yy < PX; yy++)
                    for (int xx = 0; xx < PX; xx++) {
                        const uint16_t v = px[yy * PX + xx];
                        if (v != KEY) t.drawPixel(x + xx, emojiY + yy, v);
                    }
                x += PX + GAP;
                i += 2;
            } else i++;
            continue;
        }
        const size_t k = run(s + i, n - i, buf, sizeof buf);
        t.setCursor(x, textY);
        t.print(buf);
        x += t.textWidth(buf);
        i += k ? k : 1;
    }
    return x;
}

}  // namespace Emoji
#endif
