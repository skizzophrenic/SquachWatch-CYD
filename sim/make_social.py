#!/usr/bin/env python3
"""Renders the repository's social preview card.

    python3 make_social.py --render-only   # under WSL, after `make`
    python  make_social.py --encode-only   # wherever Pillow is installed

The card is the firmware's own boot screen, not a poster made in a design
tool. It already has everything a preview card needs -- the wordmark, the
brand line, the tagline and Squachy waving over the sunset -- laid out by
the same code that draws it on the device, so this cannot drift away from
what people actually see when they power one on.

GitHub cannot be given this over the API. Upload it by hand at
Settings > General > Social preview. It only needs doing again when the
boot screen changes.
"""
import os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
SHOT = os.path.join(HERE, "out", "social", "boot.png")
PNG  = os.path.join(HERE, "..", "docs", "social-preview.png")

# Far enough in for the wordmark's glitch-in to have settled. Earlier than
# this and the letters are still tearing.
FRAMES = 110

# GitHub renders social previews at 1280x640. The frame is 320x240, so it
# goes up 4x -- an integer, so every device pixel stays square -- and then
# 640 of those 960 rows are kept. This window holds the wordmark with a
# little headroom, both text lines, and Squachy from his crest to his waist.
# Lower and it clips the top of the wordmark; higher and the tagline sits
# awkwardly against the top edge.
ZOOM = 4
WINDOW = (56, 696)


def render():
    if not os.path.exists(os.path.join(HERE, "squachsim")):
        sys.exit("squachsim not built -- run `make` first")
    os.makedirs(os.path.dirname(SHOT), exist_ok=True)
    cmd = "./squachsim boot %s --frames %d" % (SHOT, FRAMES)
    print(cmd)
    if subprocess.call(cmd, shell=True, cwd=HERE,
                       stdout=subprocess.DEVNULL) != 0:
        sys.exit("render failed")


def encode():
    from PIL import Image
    if not os.path.exists(SHOT):
        sys.exit("no frame at %s -- render first" % SHOT)
    src = Image.open(SHOT).convert("RGB")
    big = src.resize((src.width * ZOOM, src.height * ZOOM), Image.NEAREST)
    card = big.crop((0, WINDOW[0], big.width, WINDOW[1]))
    os.makedirs(os.path.dirname(PNG), exist_ok=True)
    card.save(PNG, optimize=True)
    print("%dx%d -> %s (%d KB)"
          % (card.width, card.height, os.path.normpath(PNG),
             os.path.getsize(PNG) // 1024))
    print("Upload at Settings > General > Social preview.")


if __name__ == "__main__":
    if "--encode-only" not in sys.argv:
        render()
    if "--render-only" not in sys.argv:
        encode()
