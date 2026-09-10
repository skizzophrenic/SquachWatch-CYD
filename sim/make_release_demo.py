#!/usr/bin/env python3
"""Renders the release clip for SquachMesh messages -- a visitor, a message,
an answer, and the phrase picker.

    python3 make_release_demo.py --render-only   # under WSL, after `make`
    python  make_release_demo.py --encode-only   # wherever Pillow is installed

Unlike make_mesh_demo.py, this drives squachsim-live -- the firmware's real
main.cpp, touch mapping and all -- through a script of taps, the way the GUI's
Live tab does. The visitor is the emulator's virtual peer (meshsim.cpp): real
adverts and real message frames into the real Mesh code, so the red bubble,
the confirmation and the reply are the shipping firmware deciding what to
draw. Only the taps are staged, and the clip rings each one so a viewer can
see where the finger went.

Coordinates are landscape, 320x240, and they are LAYOUT coordinates: if the
message icon or the message screen moves, re-measure them against a frame
rather than nudging until it looks right.
"""
import json, os, shutil, struct, subprocess, sys, zlib

HERE = os.path.dirname(os.path.abspath(__file__))
OUT  = os.path.join(HERE, "out", "releasedemo")
GIF  = os.path.join(HERE, "..", "docs", "squachmesh-messages.gif")

ZOOM = 2          # integer only: nearest-neighbour has to keep device pixels square
MS   = 60         # per captured frame; captures are two 33ms steps apart, so about real time
BG   = 8          # the v1.5.23 clip's background, so the two read as one series

# Where things are. See the note above.
ICON     = (284, 94)    # the speech bubble beside the visitor (inside its padded target)
LINE     = (240, 64)    # "Where are you?": right column, first row
SEND     = (282, 221)
LETTER_P = (69, 142)    # the phrase picker's P, in the landscape 7-column grid
RING_FRAMES = 6         # how long a tap's ring stays up


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


def render():
    for b in ("squachsim", "squachsim-live"):
        if not os.path.exists(os.path.join(HERE, b)):
            sys.exit(b + " not built -- run `make` first")
    shutil.rmtree(OUT, ignore_errors=True)
    os.makedirs(OUT)

    # The GUI's saved settings -- past first boot, SquachMesh consented -- with
    # the background pinned, in a copy so the GUI's own are left alone.
    nvs = os.path.join(OUT, "nvs")
    shutil.copytree(os.path.join(HERE, ".nvs"), nvs)
    sp = os.path.join(nvs, "settings.nvs")
    lines = [l for l in open(sp).read().splitlines() if not l.startswith("u bg ")]
    open(sp, "w").write("\n".join(lines + ["u bg %d" % BG]) + "\n")

    frames = []

    def add(w, h, buf, ms, ring=None):
        name = "%04d.png" % len(frames)
        png(os.path.join(OUT, name), w, h, buf)
        frames.append({"file": name, "ms": ms, "ring": ring})

    def cap(count, every=2):
        for _ in range(count):
            add(*live.step(every), MS)

    def tap(x, y):
        # The ring goes on the frames leading up to the press and the press
        # itself -- never after. Most taps here change the screen, and a ring
        # that outlived its tap sat on whatever the NEXT screen had at that
        # spot: the bubble tap landed on a message line, SEND landed on CLR.
        for f in frames[-(RING_FRAMES - 1):]:
            f["ring"] = [x, y]
        # Press and release each get a frame of their own: the firmware tells
        # a touch that has just gone down from one that has just come up.
        live.send("D %d %d" % (x, y))
        add(*live.step(1), MS, [x, y])
        live.send("U")
        add(*live.step(1), MS)

    live = Live(nvs)
    live.step(150)                                 # boot, off camera
    if live.state != "CLEAR":
        sys.exit("booted to %s, not CLEAR -- is sim/.nvs past first boot?" % live.state)
    live.send("P outfit 12", "P shade 1", "P name BIGFOOT",
              "P reply on", "P phrase same", "P setup")
    cap(42)                    # he walks in, and they say hello
    live.send("P say 3")       # "Something's nearby."
    cap(48)                    # it arrives with his next advert, in red, with the "!"
    tap(*ICON);  cap(12)       # into the message screen
    tap(*LINE);  cap(22)       # chosen -- and it asks first
    tap(*SEND);  cap(16)       # out it goes; the dots take turns while it is on the air
    cap(26, every=4)           # the other board reading it, fast-forwarded
    cap(40)                    # and its answer, in red
    live.close()

    # The phrase picker comes from the one-shot renderer: reaching it live is
    # six taps through menus, which would be most of the clip.
    for mode in (3, 2):
        cmd = ["./squachsim", "phrase", os.path.join(OUT, "phrase%d.png" % mode),
               "--phrase-mode", str(mode), "--bg", str(BG), "--msgs"]
        if subprocess.call(cmd, cwd=HERE, stdout=subprocess.DEVNULL,
                           stderr=subprocess.DEVNULL) != 0:
            sys.exit("render failed: phrase %d" % mode)
    frames += [{"file": "phrase3.png", "ms": 1300, "ring": None},
               {"file": "phrase3.png", "ms": 450,  "ring": list(LETTER_P)},
               {"file": "phrase2.png", "ms": 1800, "ring": None}]

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
            # A ring where the finger went, drawn at device scale so it zooms
            # with everything else.
            x, y = f["ring"]
            d = ImageDraw.Draw(im)
            d.ellipse((x - 10, y - 10, x + 10, y + 10), outline=(255, 255, 255), width=2)
            d.ellipse((x - 3, y - 3, x + 3, y + 3), fill=(255, 255, 255))
        ims.append(im)

    # One global palette, as make_mesh_demo.py: the firmware draws RGB332, so
    # the whole clip is a few hundred colours at most and this stays lossless.
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
