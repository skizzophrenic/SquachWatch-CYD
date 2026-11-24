#!/usr/bin/env python3
"""Make one Bangers glyph set for include/bangers_font.h.

    python tools/gen_bangers.py SM 22          # prints the C for size SM at 22 pt

The existing LG (34 pt) and MD (30 pt) sets were made the same way: PIL
renders each glyph from sim/Bangers-Regular.ttf anchored at the ascender,
thresholded at half, packed 1 bit a pixel, MSB first, each row padded to a
byte. Glyph fields: char, width, height, x offset, y offset from the top of
the ascender box, advance.
"""
import sys
from PIL import Image, ImageDraw, ImageFont

CHARS = " !-'0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"
NAMES = {" ": "SPC", "!": "BANG", "-": "HYPH", "'": "APOS", ":": "COLON", ".": "DOT", "/": "SLASH"}
# SM also carries what the info card's tiles and names need: 08:12, 2.8, A/B.
EXTRA = {"SM": ":./"}

def main():
    tag, pt = sys.argv[1], int(sys.argv[2])
    chars = CHARS + EXTRA.get(tag, "")
    font = ImageFont.truetype("sim/Bangers-Regular.ttf", pt)
    ascent, descent = font.getmetrics()
    out, rows = [f"// size {tag}: PIL pt={pt}, ascent={ascent}, descent={descent}"], []
    for ch in chars:
        adv = int(round(font.getlength(ch)))
        if ch == " ":
            rows.append(f"    {{ ' ', 0, 0, 0, 0, {adv}, nullptr }},")
            continue
        x0, y0, x1, y1 = font.getbbox(ch, anchor="la")
        w, h = x1 - x0, y1 - y0
        img = Image.new("L", (w, h), 0)
        ImageDraw.Draw(img).text((-x0, -y0), ch, font=font, fill=255, anchor="la")
        bits = []
        for y in range(h):
            for bx in range(0, w, 8):
                b = 0
                for k in range(8):
                    x = bx + k
                    if x < w and img.getpixel((x, y)) >= 128:
                        b |= 0x80 >> k
                bits.append(b)
        name = NAMES.get(ch, ch)
        out.append(f"static const uint8_t g_{tag}_{name}_bits[] = {{ " + ", ".join(f"0x{b:02X}" for b in bits) + " };")
        cq = "\\'" if ch == "'" else ch
        rows.append(f"    {{ '{cq}', {w}, {h}, {x0}, {y0}, {adv}, g_{tag}_{name}_bits }},")
    out.append(f"static const Glyph {tag}_GLYPHS[] = {{")
    out += rows
    out.append("};")
    out.append(f"static const uint8_t {tag}_GLYPH_COUNT = {len(chars)};")
    out.append(f"static const uint8_t {tag}_ASCENT = {ascent};")
    print("\n".join(out))

if __name__ == "__main__":
    main()
