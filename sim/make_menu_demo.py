#!/usr/bin/env python3
"""Renders the release clip for the settings menu -- a group folding away, the
SYSTEM page opening, and BACK sitting on the bottom edge the whole time.

    python3 make_menu_demo.py --render-only   # under WSL, after `make live`
    python3 make_menu_demo.py --encode-only   # wherever Pillow is installed

Same machinery as make_emote_demo.py: squachsim-live is the firmware's real
main.cpp and ui_settings.cpp, driven by a script of taps, each one ringed. Only
the taps are staged -- every pixel is the shipping build deciding what to draw.

Coordinates are landscape, 320x240, and they are LAYOUT coordinates taken from
ui_settings.cpp rather than nudged until they looked right:
    TOP_MARGIN 32, header 14px (size-1 font + 6), row 24px (size-2 font + 8),
    pinned BACK strip 26px along the bottom.
So on the main list: BEHAVIOR heading spans y 32..46, and the rows below it
start at 46 and step 24 apart.
"""
import json, os, shutil, struct, subprocess, sys, zlib

HERE = os.path.dirname(os.path.abspath(__file__))
OUT  = os.path.join(HERE, "out", "menudemo")
GIF  = os.path.join(HERE, "..", "docs", "settings-menu.gif")

ZOOM = 2          # integer only: nearest-neighbour keeps device pixels square
MS   = 60         # per captured frame; captures are two 33ms steps apart
BG   = 8          # the background the other release clips used

GEAR     = (12, 8)      # opens Settings from CLEAR
BEHAVIOR = (100, 39)    # the BEHAVIOR heading -- tapping it folds the group
SQUACHY_HDR_FOLDED = (100, 53)   # where SQUACHY's heading lands once BEHAVIOR is shut
BACK     = (160, 227)   # the pinned strip along the bottom
RING_FRAMES = 6


def png(path, w, h, rgb):
    raw = b"".join(b"\0" + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))
    def chunk(t, d):
        c = struct.pack(">I", len(d)) + t + d
        return c + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n"
                + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


class Live:
    """squachsim-live over its pipe: commands in, one RGB888 frame per S."""
    def __init__(self, nvs):
        self.p = subprocess.Popen([os.path.join(HERE, "squachsim-live")], cwd=HERE,
                                  stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  stderr=subprocess.DEVNULL,
                                  env=dict(os.environ, SQUACHSIM_NVS=nvs))
        self.state = "?"

    def send(self, *cmds):
        self.p.stdin.write("".join(c + "\n" for c in cmds).encode())
        self.p.stdin.flush()

    def step(self, n):
        self.send("S %d" % n)
        hdr = self.p.stdout.readline().decode("ascii", "replace").rstrip("\n").split(None, 5)
        if len(hdr) < 5 or hdr[0] != "FRM":
            sys.exit("bad frame header: %r" % hdr)
        w, h, nb = int(hdr[1]), int(hdr[2]), int(hdr[3])
        self.state = hdr[4]
        buf = b""
        while len(buf) < nb:
            chunk = self.p.stdout.read(nb - len(buf))
            if not chunk:
                sys.exit("emulator exited mid-frame")
            buf += chunk
        return w, h, buf

    def close(self):
        self.send("Q")
        self.p.wait(timeout=5)


def seed(name, extra):
    nvs = os.path.join(OUT, name)
    shutil.copytree(os.path.join(HERE, ".nvs"), nvs)
    sp = os.path.join(nvs, "settings.nvs")
    lines = [l for l in open(sp).read().splitlines() if not l.startswith("u bg ")]
    open(sp, "w").write("\n".join(lines + ["u bg %d" % BG] + extra) + "\n")
    return nvs


def render():
    if not os.path.exists(os.path.join(HERE, "squachsim-live")):
        sys.exit("squachsim-live not built -- run `make live` first")
    shutil.rmtree(OUT, ignore_errors=True)
    os.makedirs(OUT)

    frames = []

    def add(w, h, buf, ms, ring=None):
        name = "%04d.png" % len(frames)
        png(os.path.join(OUT, name), w, h, buf)
        frames.append({"file": name, "ms": ms, "ring": ring})

    def cap(live, count, every=2):
        for _ in range(count):
            add(*live.step(every), MS)

    def tap(live, x, y):
        for f in frames[-(RING_FRAMES - 1):]:
            f["ring"] = [x, y]
        live.send("D %d %d" % (x, y))
        add(*live.step(1), MS, [x, y])
        live.send("U")
        add(*live.step(1), MS)

    live = Live(seed("nvs_menu", []))
    live.step(150)                                  # boot, off camera
    if live.state != "CLEAR":
        sys.exit("booted to %s, not CLEAR -- is sim/.nvs past first boot?" % live.state)

    cap(live, 10)                                   # CLEAR, a beat
    tap(live, *GEAR);     cap(live, 14)             # the list, BACK already pinned
    tap(live, *BEHAVIOR); cap(live, 22)             # BEHAVIOR folds: four rows gone
    tap(live, *SQUACHY_HDR_FOLDED); cap(live, 22)   # and SQUACHY folds too
    tap(live, *BEHAVIOR); cap(live, 18)             # BEHAVIOR back again
    tap(live, *BACK);     cap(live, 12)             # out, from the bottom edge

    live.close()
    json.dump(frames, open(os.path.join(OUT, "manifest.json"), "w"), indent=0)
    print("%d frames -> %s" % (len(frames), OUT))


def encode():
    from PIL import Image, ImageDraw
    mp = os.path.join(OUT, "manifest.json")
    if not os.path.exists(mp):
        sys.exit("no frames -- run --render-only under WSL first")
    man = json.load(open(mp))

    ims = []
    for f in man:
        im = Image.open(os.path.join(OUT, f["file"])).convert("RGB")
        if f["ring"]:
            x, y = f["ring"]
            d = ImageDraw.Draw(im)
            d.ellipse((x - 10, y - 10, x + 10, y + 10), outline=(255, 255, 255), width=2)
            d.ellipse((x - 3, y - 3, x + 3, y + 3), fill=(255, 255, 255))
        ims.append(im)

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
                   duration=[f["ms"] for f in man], loop=0, optimize=True, disposal=1)
    total = sum(f["ms"] for f in man) / 1000.0
    print("%d frames, %.1fs, %d colours, %dx%d -> %s (%d KB)"
          % (len(frames), total, len(cols), frames[0].width, frames[0].height,
             os.path.normpath(GIF), os.path.getsize(GIF) // 1024))


if __name__ == "__main__":
    if "--encode-only" not in sys.argv:
        render()
    if "--render-only" not in sys.argv:
        encode()
