#!/usr/bin/env python3
"""Renders the SquachMesh preview clip -- two devices, two Squachys, one
conversation.

Same two-step shape as make_demo.py next door, and for the same reason:

    python3 make_mesh_demo.py --render-only   # under WSL, after `make`
    python  make_mesh_demo.py --encode-only   # wherever Pillow is installed

It drives the real emulator, so this advertises the firmware rather than a
recording somebody staged once. The visitor is a synthetic peer (--peer /
--peername) standing in for a second board, but every frame of what he does
on screen -- the walk in, the turn-taking, the leaning, the laugh -- is the
shipping code deciding it.

WHY THE OFFSETS BELOW ARE NUMBERS AND NOT NAMES
A visit is a state machine on a clock, and there is no way to ask the
emulator for "the frame where the guest laughs". These offsets were measured
off a rendered sequence by finding the speech bubbles (a long horizontal run
of VAPOR_PINK is a bubble border) and reading the phase boundaries off that.

So they WILL drift the moment the line timings change -- Squachy::lineMs is
length-based, and adding one word to one line moves everything after it. If a
segment starts looking like nothing in particular, re-measure; do not nudge
these by hand until it looks right, because "looks right" and "is the moment
it claims to be" stop agreeing very quickly.
"""
import os, subprocess, sys, glob, shutil

HERE = os.path.dirname(os.path.abspath(__file__))
OUT  = os.path.join(HERE, "out", "meshdemo")
GIF  = os.path.join(HERE, "..", "docs", "squachmesh.gif")

ZOOM = 2      # integer only: nearest-neighbour has to keep device pixels square
MS   = 50     # 20fps against the emulator's 33ms step, so slightly slow motion
LEN  = 24     # frames per segment

# One background for all of it, unlike make_demo.py's montage. That clip is a
# tour of four unrelated things; this one is a single scene, and cutting the
# sky between shots would read as cutting to a different device.
COMMON = "--peer 3 --peername BIGFOOT --noseed --bg 8"

# Frames are 33ms apart, so frame N is roughly N*33 ms into the visit.
SEGMENTS = [
    # 0.99s: most of the way through the 1800ms walk-in, arriving and settling.
    ("a_arrive", 30),
    # 11.0s: the guest answering, and the host cracking up at it -- the host
    # has no bubble on this beat, so his reaction is the only thing of his on
    # screen, which is the whole reason he gets one.
    ("b_reply", 334),
    # 13.5s: the host's topper, and the guest laughing at that. The other half
    # of the same joke, from the other side.
    ("c_topper", 410),
]


def render():
    if not os.path.exists(os.path.join(HERE, "squachsim")):
        sys.exit("squachsim not built -- run `make` first")
    shutil.rmtree(OUT, ignore_errors=True)
    os.makedirs(OUT, exist_ok=True)
    for name, frame in SEGMENTS:
        cmd = "./squachsim clear %s/%s.png %s --frames %d --sequence %d" % (
            OUT, name, COMMON, frame, LEN)
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
        sys.exit("no frames -- run --render-only under WSL first")
    ims = [Image.open(f).convert("RGB") for f in fs]

    # ONE global palette for the whole clip. The firmware draws into an 8-bit
    # RGB332 buffer, so the entire animation is only ~130 distinct colours,
    # well inside GIF's 256 -- which makes this lossless AND smaller than
    # per-frame quantisation. Same reasoning as make_demo.py.
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
