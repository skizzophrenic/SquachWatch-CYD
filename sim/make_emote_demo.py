#!/usr/bin/env python3
"""Renders the release clip for emotes and the crowd -- a visitor, the emote
picker, a pie in the face, rock-paper-scissors sent back, and eight of them
roaming.

    python3 make_emote_demo.py --render-only   # under WSL, after `make`
    python3 make_emote_demo.py --encode-only   # wherever Pillow is installed

Same shape as make_release_demo.py: squachsim-live -- the firmware's real
main.cpp, touch mapping and all -- driven by a script of taps, with the
visitor being the emulator's virtual peer (meshsim.cpp). Real frames into the
real Mesh code, so what the two of them act out is the shipping firmware
deciding it. Only the taps are staged, and each one is ringed.

Coordinates are landscape, 320x240, and they are LAYOUT coordinates: if the
message screen or the picker moves, re-measure them against a frame rather
than nudging until it looks right.
"""
import json, os, shutil, struct, subprocess, sys, zlib

HERE = os.path.dirname(os.path.abspath(__file__))
OUT  = os.path.join(HERE, "out", "emotedemo")
GIF  = os.path.join(HERE, "..", "docs", "squachmesh-emotes.gif")

ZOOM = 2          # integer only: nearest-neighbour has to keep device pixels square
MS   = 60         # per captured frame; captures are two 33ms steps apart, so about real time
BG   = 8          # the same background the two earlier SquachMesh clips used

# Where things are. See the note above.
ICON   = (282, 88)     # the speech bubble beside the visitor
EMOTE  = (246, 12)     # the [ EMOTE ] button on the message screen, beside "?"
PRANK  = (130, 63)     # the third tab of the picker
PIE    = (160, 99)     # the second tile on it
RING_FRAMES = 6

# What the visitor sends back: rock-paper-scissors, setup 5 -- paper against
# scissors, so the board receiving it wins. Both boards roll nothing of their
# own; the setup byte is the whole game, which is the point worth showing.
REPLY = "P emote 3 5"


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
    """A copy of the GUI's saved settings with the background pinned, plus
    whatever lines this scene needs on top."""
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

    # ---- one visitor: the picker, a pie, and rock-paper-scissors back ----
    live = Live(seed("nvs_pair", []))
    live.step(150)                                 # boot, off camera
    if live.state != "CLEAR":
        sys.exit("booted to %s, not CLEAR -- is sim/.nvs past first boot?" % live.state)
    live.send("P outfit 12", "P shade 1", "P name BIGFOOT", "P reply off", "P setup")
    cap(live, 30)                    # he walks in, and they say hello
    # ...and settle, off camera. Tapped at 72 steps the icon was not there yet
    # to be tapped: the whole first segment was a Squachy being poked in the
    # background. pick.sh, which proved these coordinates, waited 480.
    live.step(420)
    cap(live, 6)
    tap(live, *ICON);   cap(live, 8)    # the message screen
    tap(live, *EMOTE);  cap(live, 8)    # the picker, on its first tab
    tap(live, *PRANK);  cap(live, 6)    # PRANK
    tap(live, *PIE);    cap(live, 64)   # a pie, and both of them act it out
    live.send(REPLY)
    cap(live, 70)                    # rock-paper-scissors from him: the reveal, the result
    live.close()

    # ---- and then eight of them ----
    live = Live(seed("nvs_crowd", ["u crowd 8"]))
    live.step(150)
    live.send("P outfit 3", "P nick 2", "P setup", "P squad 8")
    live.step(120)                   # every advert in, the sizes settled -- off camera
    cap(live, 56)                    # roaming
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

    # One global palette: the firmware draws RGB332, so the whole clip is a
    # few hundred colours at most and this stays lossless.
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
