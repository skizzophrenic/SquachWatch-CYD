#!/usr/bin/env python3
"""Renders the outfit gallery in the firmware's README.

    python3 make_gallery.py --render-only   # under WSL, after `make`
    python  make_gallery.py --encode-only   # wherever Pillow is installed

Same split as make_demo.py, and for the same reason: the emulator builds
under WSL and Pillow generally is not installed there.

Every costume in the game, drawn by the firmware rather than posed by hand.
That is worth doing precisely because almost no firmware project can: it
needs a renderer that runs without the hardware, which is what sim/ is.

The point is that this cannot go stale quietly. Add a costume and re-run
this, and it appears. Change how one looks and it changes here too. A
hand-assembled sheet of screenshots starts lying the first time somebody
touches drawBody() and never says so.
"""
import os, subprocess, sys, glob, shutil

HERE = os.path.dirname(os.path.abspath(__file__))
OUT  = os.path.join(HERE, "out", "gallery")
PNG  = os.path.join(HERE, "..", "docs", "outfits.png")

# Read straight out of the firmware rather than duplicated here, so a
# renamed or newly added costume cannot silently disagree with its picture.
SRC = os.path.join(HERE, "..", "src", "squachy.cpp")

# SNOWFALL, for contrast rather than for looks. A dark backdrop was the
# obvious pick and it is wrong: SHADOW is a black silhouette and CAPTAIN's
# tricorn is black, so against TERMINAL LOG both of them vanished and the
# two panels showed a pair of floating sunglasses. This has a night sky
# behind his head and a white snow band under his feet, so the dark
# costumes read against the snow and the bright ones read against the sky.
BG    = 7
# Warm-up has to clear 6000ms, and this is the reason. --outfit calls
# Squachy::unlockAllOutfits(), which is also the easter egg that fires the
# rare "party mode" shimmer for six seconds -- and that shimmer overrides
# the fur colour of every costume. Render inside that window and twelve of
# the fourteen come out in the same cyan-and-pink, with only their
# accessories differing. At 33ms a frame, 220 frames is 7.3 seconds.
FRAMES = 220
COLS  = 5
ZOOM  = 2
CROP  = (108, 22, 212, 152)   # his footprint on a 320x240 frame


def outfit_names():
    txt = open(SRC, encoding="utf-8", errors="replace").read()
    start = txt.index("static const OutfitDef OUTFITS[]")
    body  = txt[start:txt.index("};", start)]
    names = []
    for line in body.splitlines():
        line = line.strip()
        if line.startswith("{ \""):
            names.append(line.split('"')[1])
    return names


def render(names):
    if not os.path.exists(os.path.join(HERE, "squachsim")):
        sys.exit("squachsim not built -- run `make` first")
    shutil.rmtree(OUT, ignore_errors=True)
    os.makedirs(OUT, exist_ok=True)
    for i, _ in enumerate(names):
        cmd = ("./squachsim clear %s/o%02d.png --bg %d --outfit %d --frames %d"
               % (OUT, i, BG, i, FRAMES))
        print(cmd)
        if subprocess.call(cmd, shell=True, cwd=HERE,
                           stdout=subprocess.DEVNULL) != 0:
            sys.exit("render failed at outfit %d" % i)


def encode(names):
    from PIL import Image, ImageDraw
    shots = sorted(glob.glob(os.path.join(OUT, "o*.png")))
    if len(shots) != len(names):
        sys.exit("have %d frames for %d outfits -- render first"
                 % (len(shots), len(names)))

    cw, ch = (CROP[2] - CROP[0]) * ZOOM, (CROP[3] - CROP[1]) * ZOOM
    label_h = 16
    pad = 6
    rows = (len(names) + COLS - 1) // COLS
    W = COLS * (cw + pad) + pad
    H = rows * (ch + label_h + pad) + pad

    sheet = Image.new("RGB", (W, H), (10, 8, 16))
    d = ImageDraw.Draw(sheet)
    for i, (shot, name) in enumerate(zip(shots, names)):
        im = Image.open(shot).convert("RGB").crop(CROP)
        im = im.resize((cw, ch), Image.NEAREST)
        x = pad + (i % COLS) * (cw + pad)
        y = pad + (i // COLS) * (ch + label_h + pad)
        sheet.paste(im, (x, y))
        # NONE is the only one that is not a costume, so it is labelled for
        # what it actually is rather than by its enum name.
        text = "no outfit" if name == "NONE" else name.lower()
        d.text((x + 2, y + ch + 3), text, fill=(210, 190, 235))

    os.makedirs(os.path.dirname(PNG), exist_ok=True)
    sheet.save(PNG, optimize=True)
    print("%d outfits, %dx%d -> %s (%d KB)"
          % (len(names), W, H, os.path.normpath(PNG),
             os.path.getsize(PNG) // 1024))


if __name__ == "__main__":
    names = outfit_names()
    if "--encode-only" not in sys.argv:
        render(names)
    if "--render-only" not in sys.argv:
        encode(names)
