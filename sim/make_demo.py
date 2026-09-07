#!/usr/bin/env python3
"""Renders the animation at the top of the firmware's README.

Two steps, because the renderer and the encoder want different hosts:

    python3 make_demo.py --render-only   # under WSL, after `make`
    python  make_demo.py --encode-only   # wherever Pillow is installed

`python3 make_demo.py` does both, which works if Pillow is available under
WSL. It usually is not, and installing it there just to encode a GIF is not
worth it -- the frames land in out/demo/ either way, so the encode can pick
them up from the other side.

It drives the real emulator -- the same C++ that runs on the board -- and
encodes what comes out, so the README advertises the firmware rather than a
recording somebody staged once and forgot. The output lands directly in the
firmware's own docs/ directory, one level up.

This CAN run in CI now that the emulator lives in the firmware repo rather
than beside it: the renderer needs nothing but g++ and sources that are
already here. Until something schedules it, re-run it by hand when the look
changes, or the demo quietly drifts behind what it demonstrates.
"""
import os, subprocess, sys, glob, shutil

HERE = os.path.dirname(os.path.abspath(__file__))
OUT  = os.path.join(HERE, "out", "demo")
GIF  = os.path.join(HERE, "..", "docs", "demo.gif")

ZOOM = 2     # integer only: nearest-neighbour has to keep device pixels square
MS   = 66    # ~15fps, close to what the panel actually manages
LEN  = 24    # frames per segment

# On ZOOM and LEN, because the obvious trade is the wrong one. Rendering at
# 1x and asking the README to display it at 640 does not work: GitHub strips
# style attributes, so image-rendering:pixelated is unavailable and the
# browser scales it smoothly, which is the one thing pixel art cannot take.
#
# Length is the lever instead, and it is a better one than resolution.
# Measured on this exact footage:
#
#     180 frames   1530 KB at 2x     603 KB at 1x
#      96 frames    821 KB at 2x     324 KB at 1x
#      64 frames    565 KB at 2x     224 KB at 1x
#
# A four second clip at full size is SMALLER than a twelve second one at
# half size, and it needs no scaling. So this stays at 2x and stays short.
# That matters because the trailer has to be re-rendered often to stay
# honest: every one of the last twenty commits touched the drawing code.

# Each new outfit appears over the background that unlocks it, which is the
# only place either of them makes sense. The warm-up counts differ so the
# segments are not all caught at the same point in Squachy's idle cycle.
SEGMENTS = [
    ("a_snow",   "--bg 7  --outfit 13 --frames 140"),   # SNOWFALL  + SNOW PARKA
    ("b_star",   "--bg 1  --outfit 12 --frames 200"),   # STARFIELD + VOID EYE
    ("c_synth",  "--bg 10 --outfit 0  --frames 260"),   # SYNTHWAVE
    ("d_gibson", "--bg 8  --outfit 0  --frames 320"),   # THE GIBSON
]


def render():
    if not os.path.exists(os.path.join(HERE, "squachsim")):
        sys.exit("squachsim not built -- run `make` first")
    shutil.rmtree(OUT, ignore_errors=True)
    os.makedirs(OUT, exist_ok=True)
    for name, args in SEGMENTS:
        cmd = "./squachsim clear %s/%s.png %s --sequence %d" % (OUT, name, args, LEN)
        print(cmd)
        if subprocess.call(cmd, shell=True, cwd=HERE,
                           stdout=subprocess.DEVNULL) != 0:
            sys.exit("render failed: " + name)


def encode():
    from PIL import Image
    fs = []
    for name, _ in SEGMENTS:
        fs += sorted(glob.glob(os.path.join(OUT, name + "_*.png")))
    if not fs:
        sys.exit("no frames")
    ims = [Image.open(f).convert("RGB") for f in fs]

    # ONE global palette for the whole clip, not a local table per frame. The
    # firmware draws into an 8-bit RGB332 buffer, so the entire animation is
    # only ~130 distinct colours, well inside GIF's 256. That makes this
    # lossless AND about a third the size of per-frame quantisation.
    cols = set()
    for im in ims:
        cols |= set(im.getdata())
    cols = sorted(cols)
    if len(cols) > 256:
        sys.exit("%d colours -- that is not an RGB332 frame buffer" % len(cols))

    pal = []
    for c in cols:
        pal += list(c)
    pal += [0, 0, 0] * (256 - len(cols))
    ref = Image.new("P", (1, 1))
    ref.putpalette(pal)

    frames = []
    for im in ims:
        q = im.quantize(palette=ref, dither=Image.NONE)
        if ZOOM > 1:
            q = q.resize((im.width * ZOOM, im.height * ZOOM), Image.NEAREST)
        frames.append(q)

    os.makedirs(os.path.dirname(GIF), exist_ok=True)
    frames[0].save(GIF, save_all=True, append_images=frames[1:],
                   duration=MS, loop=0, optimize=True, disposal=1)
    print("%d frames, %d colours, %dx%d -> %s (%d KB)"
          % (len(frames), len(cols), frames[0].width, frames[0].height,
             os.path.normpath(GIF), os.path.getsize(GIF) // 1024))


if __name__ == "__main__":
    if "--encode-only" not in sys.argv:
        render()
    if "--render-only" not in sys.argv:
        encode()
