"""Build the 16-pixel Taiwan chat bitmap from Noto Sans CJK TC Regular.

Run with Pillow and fonttools. Pass the OTF from Noto CJK Sans2.004.
"""
import argparse
import struct
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont
from fontTools.ttLib import TTFont

SIZE = 16
RANGES = (
    (0x2000, 0x206F), (0x3000, 0x303F),  # punctuation
    (0x3100, 0x312F), (0x31A0, 0x31BF),  # Bopomofo
    (0x3400, 0x4DBF), (0x4E00, 0x9FFF),  # BMP Han
    (0xF900, 0xFAFF), (0xFF00, 0xFFEF),  # compatibility and full-width text
)
RECORD = struct.Struct("<H" + "H" * SIZE)


def generate(font_path, output_path):
    with TTFont(font_path, lazy=True) as source:
        cmap = source.getBestCmap()
    font = ImageFont.truetype(str(font_path), SIZE)
    payload = bytearray()
    for first, last in RANGES:
        for code in range(first, last + 1):
            if code not in cmap:
                continue
            glyph = Image.new("L", (SIZE, SIZE))
            char = chr(code)
            left, top, right, bottom = font.getbbox(char, anchor="ls")
            x = (SIZE - (right - left)) // 2 - left if right - left < SIZE else -left
            baseline = min(14, SIZE - bottom)
            if baseline + top < 0:
                baseline = -top
            ImageDraw.Draw(glyph).text((x, baseline), char, font=font, fill=255, anchor="ls")
            rows = []
            for y in range(SIZE):
                row = 0
                for x in range(SIZE):
                    if glyph.getpixel((x, y)) >= 96:
                        row |= 1 << (SIZE - 1 - x)
                rows.append(row)
            payload.extend(RECORD.pack(code, *rows))
    output_path.write_bytes(payload)
    print(f"Wrote {len(payload) // RECORD.size} glyphs, {len(payload)} bytes")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("font", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    generate(args.font, args.output)
