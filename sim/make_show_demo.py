#!/usr/bin/env python3
"""Renders a release's show-off clip: captioned scenes, one after another.

    python3 make_show_demo.py --render-only [--clip NAME]   # under WSL, after `make`
    python  make_show_demo.py --encode-only [--clip NAME]   # wherever Pillow is installed

Every scene is a run of the one-shot emulator, so every frame is the shipping
code deciding what to draw; the caption under it is the only thing staged.
Output lands in the firmware's docs/ so the release notes can embed it from
the tag -- which is the whole reason this is a script in the repo and not a
one-off: the v1.13.0 clip was rendered from a heredoc, never copied into
docs/, and shipped in no release notes at all.

A scene is (screen, warm-up frames, captured frames, extra args, env, hold
ms, caption). A scene whose captured-frames field is a list is a scroll
sweep: one run per --scroll value, one frame each, which is how a list
scrolling to its stop is shown.

The clock is pinned (SQUACH_EPOCH) so the clip renders the same on any day.
"""
import json, os, shutil, subprocess, sys

HERE  = os.path.dirname(os.path.abspath(__file__))
FONT  = os.path.join(HERE, "Bangers-Regular.ttf")
EPOCH = "1789396740"   # Mon 14 Sep 2026, 14:39 UTC -- the desk clip's moment too
W, H  = 320, 240       # the default panel; CLIP_SIZE overrides it per clip
ZOOM  = 2              # integer only: nearest-neighbour keeps device pixels square
MS    = 66             # per captured frame: two 33 ms steps, about real time
BAND  = 52             # the caption band under the screen, in output pixels
CYAN  = (0, 214, 214)

# ---- the clips ----

CLIPS = {
    # v1.21.0 "All Ears": the watch hears again, the buzz grew up, and the
    # watch rows got a page. Shot on the watch's 240x240 like v1.20.0's; the
    # emulator is not the watch build, so the page itself is the notes' to
    # describe and the captions carry the story.
    "all-ears": [
        ("clear",  60, 36, ["--noseed", "--bg", "7"],  {}, 1600, "HE HEARS AGAIN. EVERY BOOT. WE CHECKED."),
        ("alert",  30, 16, [],                          {}, 1600, "THE BUZZ GREW A BACKBONE. MED BY DEFAULT."),
        ("clear",  60, 36, ["--noseed", "--bg", "10"], {}, 1500, "WORN ALL DAY. NOT ONE DEAF MINUTE."),
    ],
    # v1.20.0 "SquachWatch^2": he moves onto a wrist. Shot on the T-Watch S3's
    # own 240x240 panel (see CLIP_SIZE). The emulator is not the watch build,
    # so the corner clock and the WATCH settings are the notes' to describe.
    "squachwatch-squared": [
        ("clear",  60, 36, ["--noseed", "--bg", "7"],                                  {}, 1500, "SQUACHWATCH. ON A WATCH."),
        ("clear",  60, 36, ["--peer", "3", "--peername", "POOTS", "--noseed", "--bg", "6"], {}, 1600, "SQUAD VISITS STAY PUT NOW"),
        ("clear", 716, 32, ["--showoff", "--noseed", "--bg", "10"],                    {},  900, "240 BY 240. HE FITS. MOSTLY."),
    ],
    # v1.19.1 "Crash Override": the WiFi update that finishes, the label he
    # reads now, and the shark suit that survives a visit.
    "crash-override": [
        ("sysprops", 20, 24, ["--tab", "0"],                                                  {}, 1500, "THE WIFI UPDATE FINISHES NOW. ALL OF IT."),
        ("sysprops", 20, 24, ["--tab", "0"],                                                  {}, 1500, "ON 1.13 TO 1.19? DO THIS ONE BY USB OR BLUETOOTH"),
        ("clear",    60, 36, ["--peer", "14", "--peername", "STOMPY", "--noseed", "--bg", "6"], {}, 1500, "THE SHARK SUIT SURVIVES THE TRIP"),
    ],
    # v1.19.0 "Neighbourhood Watch": the regulars, the nemesis, what he
    # notices, the seven moves, the shark. The moves come from SHOW OFF: the
    # emulator runs it at half tempo, so a step is ~34 frames from frame 204.
    "neighbourhood-watch": [
        ("clear", 716, 32, ["--showoff", "--noseed", "--bg", "6"],  {},  700, "HE HEARD SOMETHING. HE ALWAYS HEARS SOMETHING."),
        ("clear", 750, 30, ["--showoff", "--noseed", "--bg", "6"],  {},  700, "HE TRIPS OVER NOTHING. DON'T MENTION IT."),
        ("clear", 922, 34, ["--showoff", "--noseed", "--bg", "6"],  {},  900, "FLICK HIM. GO ON. HE WALKS BACK. SLOWLY."),
        ("log",    20, 24, [],                                       {}, 1500, "THE RING YOU PASS EVERY DAY IS CALLED VERN NOW"),
        ("dex",    20, 24, ["--pose", "6", "--bg", "4"],             {}, 1500, "THE ONE YOU CATCH MOST IS HIS NEMESIS"),
        ("clear",  60, 36, ["--outfit", "14", "--noseed", "--bg", "10"], {}, 1300, "THE SHARK GOT AN OUTLINE. AND NOSTRILS."),
    ],
    # v1.18.0 "Field Guide": the SQUACHY-DEX, the LOG showing each device once,
    # and the tap that has to be quick.
    "field-guide": [
        ("dex",      20, 30, ["--pose", "0", "--bg", "4"], {}, 1300, "SEVENTEEN CRYPTIDS. SQUACHY HAS A BINDER."),
        ("dex",      20, 30, ["--pose", "6", "--bg", "4"], {}, 1600, "EVERY CATCH GETS A CARD. LORE INCLUDED."),
        ("dex",      20, 30, ["--pose", "5", "--bg", "4"], {}, 1500, "NOT CAUGHT? YOU GET A HINT AND A SILHOUETTE"),
        ("log",      20, 30, [],                            {}, 1300, "THE LOG SHOWS EACH DEVICE ONCE. FINALLY."),
        ("settings", 10, 24, ["--scroll", "0"],             {}, 1300, "AND A SLOW THUMB IS NOT A TAP ANY MORE"),
    ],
    # v1.17.0 "Hold Still": touch calibration, done over. The touchcal screen
    # is the real flow played by a scripted finger (see main_sim.cpp), and
    # --frames there skips into it rather than warming an animation up.
    "hold-still": [
        ("touchcal",   0, 16, [],            {},  900, "SQUACHY WANTS TO KNOW WHERE YOUR FINGER IS"),
        ("touchcal",  16, 62, [],            {},  500, "FIVE TARGETS. NONE HIDING UNDER YOUR CASE"),
        ("touchcal", 104, 44, [],            {},  500, "HOLD ONE SECOND. NOT FOREVER. ONE."),
        ("touchcal", 150, 38, [],            {}, 1200, "THEN IT GRADES ITS OWN HOMEWORK"),
        ("clear",     60, 40, ["--bg", "4"], {}, 1400, "PORTRAIT TOUCH: FINALLY NOT HAUNTED"),
    ],
    # v1.14.0 "Stoop Kid": what changed, in the order it matters to a viewer.
    "stoop-kid": [
        ("clear",    60, 36, ["--bg", "4"],            {},  700, "TEXT DRAWS SIX TIMES FASTER"),
        ("clear",    60, 36, ["--bg", "0"],            {},  700, "15 TO 21 FPS ON THE MAIN SCREEN"),
        ("alert",    30, 16, ["--lastfree"],           {}, 1900, "AUTO SNOOZE: FIVE ALERTS, THEN IT HAS TO COME CLOSER"),
        ("alert",    30, 16, ["--first", "--night"],   {}, 1500, "TWO BANNERS THAT NEVER DREW, DRAWING"),
        ("settings", 10, 12, ["--scroll", "0"],        {}, 1500, "SETTINGS > BEHAVIOR > AUTO SNOOZE"),
        ("log",      20, list(range(0, 13)), [],       {}, 1300, "EVERY LIST STOPS AT THE BOTTOM NOW"),
    ],
    # v1.16.1 "Good Company": the pet was standing on the wrong Squachy, and
    # the 3.5" reached the flasher's picker without reaching its files.
    "good-company": [
        ("clear", 400, 44, ["--peer", "2", "--peername", "GUEST", "--pet", "1"], {},  900, "A VISITOR, AND THE PET THAT FOLLOWED THE WRONG ONE"),
        ("clear", 460, 44, ["--peer", "2", "--peername", "GUEST", "--pet", "1"], {},  900, "IT RIDES ITS OWN SQUACHY'S BOUNCE AGAIN"),
        ("boot",   20, 22, [],                 {}, 1500, "AND THE 3.5 INCH IS ON THE FLASHER FOR REAL"),
    ],
    # v1.16.0 "The Big Screen": the 3.5" ships, and it is the screen itself
    # that is the news -- so the clip is shot on it.
    "the-big-screen": [
        ("boot",     20, 24, [],                        {}, 1200, "THE 3.5 INCH SHIPS, AS A BETA"),
        ("clear",    60, 40, ["--bg", "5"],             {},  900, "IT STOPPED CRASHING: A WRITE 76KB PAST THE BUFFER"),
        ("settings", 10, 14, ["--scroll", "0"],         {}, 1500, "MENUS ARE BUFFERED NOW, AND SEND NOTHING AT ALL"),
        ("alert",    20, 20, [],                        {}, 1500, "AND EVERYTHING USES THE SCREEN IT HAS"),
    ],
    # v1.15.0 "Blast Processing": the frame rate, and the mascot keeping his
    # pace in spite of it. The row-skipping push and the numbers are the
    # board's; the captions carry them.
    "blast-processing": [
        ("clear",    60, 40, ["--bg", "10"],           {},  900, "THIRTY FRAMES A SECOND ON THE 2.8 INCH"),
        ("clear",    60, 40, ["--bg", "3"],            {},  900, "ROWS THAT DID NOT CHANGE ARE NOT SENT"),
        ("settings", 10, 10, ["--scroll", "0"],        {}, 1500, "A MENU SENDS NOTHING AT ALL"),
        ("clear",    60, 46, ["--bg", "4"],            {},  900, "SQUACHY KEEPS HIS OWN PACE: 120 / 70"),
    ],
}

# A clip can be shot on another panel. The emulator takes --size WxH and every
# screen composes itself for whatever it is given, so a release about the 3.5"
# can be SHOWN on the 3.5" instead of described on a 2.8". Zoom comes down to
# 1 there: 480x320 doubled is a 960-pixel GIF, which is more than the notes
# embed at anyway.
CLIP_SIZE = {
    "all-ears":            (240, 240, 2),
    "squachwatch-squared": (240, 240, 2),
    "the-big-screen": (480, 320, 1),
    "good-company":   (480, 320, 1),
}

def clip_geom(clip):
    w, h, z = CLIP_SIZE.get(clip, (W, H, ZOOM))
    return w, h, z

def out_dir(clip):
    return os.path.join(HERE, "out", "show-" + clip)

def gif_path(clip):
    return os.path.join(HERE, "..", "docs", clip + ".gif")

# ---- render ----

def run(cmd, env):
    e = dict(os.environ, SQUACH_EPOCH=EPOCH)
    e.update(env)
    if subprocess.call(cmd, cwd=HERE, env=e, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL) != 0:
        sys.exit("render failed: " + " ".join(cmd))

def render(clip):
    cw, ch, _ = clip_geom(clip)
    sim = os.path.join(HERE, "squachsim")
    if not os.path.exists(sim):
        sys.exit("build the emulator first: make -j8 squachsim")
    out = out_dir(clip)
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)
    man = []
    for i, (screen, warm, n, extra, env, hold, caption) in enumerate(CLIPS[clip]):
        if isinstance(n, list):
            # A scroll sweep: one frame per value.
            for k, sc in enumerate(n):
                raw = os.path.join(out, "scene%d_%d.raw" % (i, k))
                run([sim, screen, os.path.join(out, "x.png"), "--frames", str(warm),
                     "--sequence", "1", "--raw", raw, "--scroll", str(sc),
                     "--size", "%dx%d" % (cw, ch)] + extra, env)
                if os.path.getsize(raw) != cw * ch * 3:
                    sys.exit("scene %d/%d: bad frame size" % (i, k))
                last = (k == len(n) - 1)
                man.append({"raw": os.path.basename(raw), "index": 0,
                            "ms": (MS * 2) if not last else MS + hold, "caption": caption})
            continue
        raw = os.path.join(out, "scene%d.raw" % i)
        run([sim, screen, os.path.join(out, "x.png"), "--frames", str(warm),
             "--sequence", str(n), "--raw", raw,
             "--size", "%dx%d" % (cw, ch)] + extra, env)
        if os.path.getsize(raw) != cw * ch * 3 * n:
            sys.exit("scene %d: %d bytes, expected %d" % (i, os.path.getsize(raw), cw * ch * 3 * n))
        for k in range(n):
            man.append({"raw": os.path.basename(raw), "index": k,
                        "ms": MS if k < n - 1 else MS + hold, "caption": caption})
    json.dump(man, open(os.path.join(out, "manifest.json"), "w"))
    print("%d frames rendered into %s" % (len(man), out))

# ---- encode ----

def caption_band(text, width):
    """The band under the screen: the caption in Bangers, leaning forward,
    the way the v1.13.0 clip had it."""
    from PIL import Image, ImageDraw, ImageFont
    band = Image.new("RGB", (width, BAND), (0, 0, 0))
    if not text:
        return band
    size = 30
    f = ImageFont.truetype(FONT, size)
    probe = ImageDraw.Draw(band)
    while size > 14:
        f = ImageFont.truetype(FONT, size)
        a, b, c, d = probe.textbbox((0, 0), text, font=f)
        if c - a <= width - 40:
            break
        size -= 2
    tw, th = c - a, d - b
    # Render upright on a transparent layer, then shear it for the lean.
    layer = Image.new("RGBA", (tw + 24, BAND), (0, 0, 0, 0))
    ImageDraw.Draw(layer).text((12 - a, (BAND - th) // 2 - b), text, font=f, fill=CYAN + (255,))
    shear = 0.18
    layer = layer.transform(layer.size, Image.AFFINE, (1, shear, -shear * BAND / 2, 0, 1, 0),
                            resample=Image.BICUBIC)
    band.paste(layer, ((width - layer.width) // 2, 0), layer)
    return band

def encode(clip):
    from PIL import Image
    cw, ch, zoom = clip_geom(clip)
    out = out_dir(clip)
    mp = os.path.join(out, "manifest.json")
    if not os.path.exists(mp):
        sys.exit("no frames -- run --render-only under WSL first")
    man = json.load(open(mp))
    raws, ims = {}, []
    bands = {}
    for f in man:
        if f["raw"] not in raws:
            raws[f["raw"]] = open(os.path.join(out, f["raw"]), "rb").read()
        b = raws[f["raw"]]
        off = f["index"] * cw * ch * 3
        im = Image.frombytes("RGB", (cw, ch), b[off:off + cw * ch * 3])
        if zoom > 1:
            im = im.resize((cw * zoom, ch * zoom), Image.NEAREST)
        if f["caption"] not in bands:
            bands[f["caption"]] = caption_band(f["caption"], im.width)
        page = Image.new("RGB", (im.width, im.height + BAND), (0, 0, 0))
        page.paste(im, (0, 0))
        page.paste(bands[f["caption"]], (0, im.height))
        ims.append(page)
    # One palette for the whole clip, so nothing shimmers between scenes.
    sheet = Image.new("RGB", (ims[0].width, ims[0].height * len(ims)))
    for i, im in enumerate(ims):
        sheet.paste(im, (0, ims[0].height * i))
    ref = sheet.quantize(colors=256, dither=Image.NONE)
    frames = [im.quantize(palette=ref, dither=Image.NONE) for im in ims]
    gif = gif_path(clip)
    os.makedirs(os.path.dirname(gif), exist_ok=True)
    frames[0].save(gif, save_all=True, append_images=frames[1:],
                   duration=[f["ms"] for f in man], loop=0, optimize=True, disposal=1)
    total = sum(f["ms"] for f in man) / 1000.0
    print("%d frames, %.1fs, %dx%d -> %s (%d KB)"
          % (len(frames), total, frames[0].width, frames[0].height,
             os.path.normpath(gif), os.path.getsize(gif) // 1024))

if __name__ == "__main__":
    clip = "stoop-kid"
    if "--clip" in sys.argv:
        clip = sys.argv[sys.argv.index("--clip") + 1]
    if clip not in CLIPS:
        sys.exit("no clip called %s; one of: %s" % (clip, ", ".join(CLIPS)))
    if "--render-only" in sys.argv:   render(clip)
    elif "--encode-only" in sys.argv: encode(clip)
    else:                             render(clip); encode(clip)
