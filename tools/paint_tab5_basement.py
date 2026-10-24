"""Paint the Tab5 base station's main-screen backdrop: a half-below-ground
basement workshop at night, as two 1280 x 720 PNGs for the card.

    python tools/paint_tab5_basement.py <out dir>

writes basement_light_on.png and basement_light_off.png, which go in
/squachwatch-base/scene/ on the Tab5's card. Painted at 640 x 360 and doubled
pixel for pixel, so it has the same chunky look as the Squachies who visit.

Everything that moves is the firmware's, drawn over this at the places
listed in SCENE below, which src/tab5_base/tab5_base.cpp repeats: the
laptop's screen, the radar's sweep and blips, the
Nixie clock's digits, the soldering iron's smoke, and the visitors.
"""
import math, os, random, sys
import numpy as np
from PIL import Image, ImageDraw

W, H = 640, 360
rnd = random.Random(7)

# Where the firmware draws, in FULL (1280 x 720) pixels.
SCENE = {
    "laptop_screen": (830, 338, 935, 405),
    "radar_centre": (1190, 356), "radar_r": 56,
    "nixie_tubes_x": (552, 596, 660, 704), "nixie_tube_w": 36, "nixie_y": (168, 210),
    "nixie_colon_x": 646,
    "bulb": (420, 190), "pull_zone": (370, 60, 480, 320),
    "chalk": (846, 92, 1252, 268),
    "solder_tip": (744, 402),
    "ground_y": 640,
}

def paint():
    img = Image.new("RGB", (W, H), (20, 20, 24))
    emit = Image.new("L", (W, H), 0)              # 255 = makes its own light
    d, e = ImageDraw.Draw(img), ImageDraw.Draw(emit)

    # ---- the ceiling: joists, a beam, a copper pipe ----
    d.rectangle([0, 0, W, 35], fill=(46, 34, 26))
    for x in range(-10, W, 48):
        d.rectangle([x, 0, x + 14, 26], fill=(70, 50, 34))
        d.line([x, 26, x + 14, 26], fill=(40, 28, 20))
    d.rectangle([0, 26, W, 35], fill=(88, 62, 40))
    d.line([0, 26, W, 26], fill=(110, 80, 52))
    d.line([0, 35, W, 35], fill=(36, 26, 18))
    d.rectangle([0, 14, W, 17], fill=(170, 96, 52))           # copper pipe
    d.line([0, 14, W, 14], fill=(220, 140, 80))
    for x in range(30, W, 120):
        d.rectangle([x, 12, x + 3, 19], fill=(120, 120, 120))  # straps

    # ---- the wall: concrete block ----
    d.rectangle([0, 36, W, 250], fill=(84, 84, 88))
    for row, y in enumerate(range(36, 250, 18)):
        off = 0 if row % 2 == 0 else 22
        for x in range(-44 + off, W, 44):
            t = rnd.randint(-7, 7)
            d.rectangle([x + 1, y + 1, x + 42, y + 16], fill=(84 + t, 84 + t, 89 + t))
            for _ in range(3):
                px, py = rnd.randint(x + 3, x + 40), rnd.randint(y + 3, y + 14)
                d.point((px, py), fill=(70 + t, 70 + t, 74 + t))
    # a damp stain low on the wall, it's a basement
    for i in range(40):
        x, y = rnd.randint(150, 240), rnd.randint(225, 248)
        d.point((x, y), fill=(66, 68, 70))

    # ---- the floor: concrete, a rug where visitors stand ----
    d.rectangle([0, 250, W, 324], fill=(64, 60, 58))
    d.rectangle([0, 250, W, 253], fill=(44, 40, 38))          # baseboard shadow
    for i in range(500):
        x, y = rnd.randint(0, W - 1), rnd.randint(254, 323)
        v = rnd.randint(52, 74); d.point((x, y), fill=(v, v - 3, v - 5))
    d.rounded_rectangle([30, 292, 600, 322], radius=4, fill=(96, 34, 30))
    d.rounded_rectangle([36, 296, 594, 318], radius=3, outline=(170, 120, 60))
    for x in range(50, 585, 18):
        d.polygon([(x, 307), (x + 6, 301), (x + 12, 307), (x + 6, 313)], fill=(150, 90, 40))
    d.rectangle([0, 324, W, H], fill=(16, 16, 20))

    # ---- the star chart over the telescope ----
    d.rectangle([20, 46, 142, 126], fill=(22, 30, 64))
    d.rectangle([20, 46, 142, 126], outline=(200, 190, 150))
    stars = [(34, 60), (52, 70), (70, 64), (88, 80), (104, 72), (122, 92), (60, 104), (82, 112), (40, 90), (128, 58)]
    for a, b in [(0, 1), (1, 2), (2, 3), (3, 4), (4, 5), (3, 7), (7, 6), (6, 8)]:
        d.line([stars[a], stars[b]], fill=(90, 110, 170))
    for s in stars:
        d.point(s, fill=(240, 240, 200)); d.point((s[0] + 1, s[1]), fill=(160, 160, 140))
    for _ in range(40):
        d.point((rnd.randint(23, 139), rnd.randint(49, 123)), fill=(110, 120, 160))
    for c in [(18, 44), (136, 44), (18, 122), (136, 122)]:
        d.rectangle([c[0], c[1], c[0] + 7, c[1] + 4], fill=(210, 200, 160))   # tape

    # ---- the little window, up at ground level ----
    d.rectangle([244, 28, 396, 80], fill=(112, 84, 56))
    d.rectangle([248, 32, 392, 74], fill=(12, 16, 42))
    for y in range(32, 75):                                   # sky, darker up top
        k = (y - 32) / 42
        d.line([248, y, 392, y], fill=(int(10 + 18 * k), int(14 + 24 * k), int(40 + 30 * k)))
    for _ in range(26):
        d.point((rnd.randint(250, 390), rnd.randint(34, 60)), fill=(230, 230, 255))
    d.ellipse([352, 37, 366, 51], fill=(236, 234, 206))        # the moon
    d.ellipse([356, 41, 359, 44], fill=(206, 204, 180)); d.point((361, 47), fill=(206, 204, 180))
    # outside: the ground at eye level, grass against the sky
    d.polygon([(248, 74), (248, 64)] + [(x, 64 - (3 + (x * 7) % 5 if x % 3 else 0)) for x in range(250, 392, 2)]
              + [(392, 64), (392, 74)], fill=(14, 30, 18))
    d.rectangle([248, 68, 392, 74], fill=(26, 20, 14))
    e.rectangle([248, 32, 392, 74], fill=255)
    d.rectangle([318, 32, 321, 74], fill=(112, 84, 56))         # the mullion
    d.rectangle([240, 76, 400, 80], fill=(140, 110, 74))        # sill
    d.line([240, 76, 400, 76], fill=(170, 136, 96))

    # ---- the shelf, and the Nixie clock's case on it ----
    d.rectangle([232, 113, 408, 117], fill=(120, 84, 50))
    d.line([232, 113, 408, 113], fill=(160, 116, 72))
    for bx in (248, 392):
        d.polygon([(bx - 4, 117), (bx + 4, 117), (bx, 130)], fill=(60, 60, 64))
    d.rectangle([266, 104, 374, 113], fill=(70, 40, 24))        # walnut base
    d.line([266, 104, 374, 104], fill=(110, 66, 40))
    for i, tx in enumerate(SCENE["nixie_tubes_x"]):
        x = tx // 2
        d.rounded_rectangle([x, 82, x + 17, 104], radius=6, fill=(26, 24, 26), outline=(120, 110, 100))
        d.line([x + 3, 86, x + 3, 100], fill=(150, 150, 150))     # highlight on the glass
        for gy in range(86, 102, 3):                               # the mesh
            d.line([x + 5, gy, x + 14, gy], fill=(44, 40, 40))
        d.rectangle([x + 2, 102, x + 15, 104], fill=(60, 60, 60))
    d.rectangle([316, 98, 328, 104], fill=(30, 28, 28))

    # ---- the hanging bulb and its pull string ----
    bx, by = SCENE["bulb"][0] // 2, SCENE["bulb"][1] // 2
    d.line([bx, 17, bx, by - 14], fill=(30, 30, 30), width=1)
    d.rectangle([bx - 4, by - 14, bx + 4, by - 6], fill=(170, 140, 70))
    d.line([bx + 3, by - 8, bx + 3, 150], fill=(210, 200, 170))
    d.ellipse([bx + 1, 150, bx + 5, 155], fill=(210, 180, 120))

    # ---- the chalkboard: the firmware writes the news on it ----
    d.rectangle([413, 40, 634, 142], fill=(122, 84, 50))
    d.rectangle([417, 44, 630, 138], fill=(30, 46, 38))
    chalk = Image.new("L", (W, H), 0); c = ImageDraw.Draw(chalk)
    for (x, y, t) in [(422, 48, "E=mc2"), (560, 50, "f(x)=sin x"), (430, 120, "dB = 10 log P"),
                      (540, 112, "x = -b/2a"), (590, 76, "sum 1/n")]:
        c.text((x, y), t, fill=60)
    for x in range(430, 620, 2):
        c.point((x, 100 + int(6 * math.sin(x / 9))), fill=50)
    img.paste((110, 128, 118), mask=chalk)
    d.rectangle([417, 138, 630, 142], fill=(150, 108, 66))     # the tray
    d.rectangle([450, 136, 460, 138], fill=(236, 236, 230)); d.rectangle([470, 136, 476, 138], fill=(240, 200, 120))

    # ---- the pegboard behind the bench ----
    d.rectangle([286, 150, 404, 204], fill=(150, 116, 80))
    for y in range(154, 202, 6):
        for x in range(290, 402, 6):
            d.point((x, y), fill=(96, 70, 48))
    d.line([300, 156, 300, 186], fill=(40, 40, 160), width=3); d.line([300, 186, 300, 196], fill=(180, 180, 180))   # screwdriver
    d.line([312, 158, 318, 190], fill=(200, 40, 40), width=3); d.line([324, 158, 318, 190], fill=(200, 40, 40), width=3)  # pliers
    d.ellipse([356, 160, 380, 184], outline=(200, 60, 40), width=3)   # a coil of wire
    d.rectangle([386, 158, 398, 166], fill=(60, 60, 64))              # tape measure

    # ---- the workbench ----
    d.rectangle([270, 213, 640, 221], fill=(146, 104, 62))
    d.line([270, 213, 640, 213], fill=(184, 140, 90))
    d.rectangle([270, 221, 640, 230], fill=(110, 76, 44))
    for lx in (276, 626):
        d.rectangle([lx, 230, lx + 8, 304], fill=(96, 66, 38))
    d.rectangle([276, 268, 634, 272], fill=(110, 76, 44))
    d.rectangle([300, 248, 340, 268], fill=(150, 112, 70)); d.line([300, 256, 340, 256], fill=(120, 88, 54))   # boxes
    d.rectangle([350, 252, 392, 268], fill=(180, 40, 36)); d.rectangle([362, 248, 380, 252], fill=(60, 60, 60))   # toolbox
    d.rectangle([560, 244, 610, 268], fill=(150, 112, 70))

    # desk lamp
    d.ellipse([290, 207, 312, 214], fill=(30, 50, 40))
    d.line([301, 210, 314, 160], fill=(40, 70, 56), width=3)
    d.line([314, 160, 340, 150], fill=(40, 70, 56), width=3)
    d.ellipse([311, 157, 317, 163], fill=(80, 80, 80))
    d.polygon([(330, 146), (350, 146), (358, 166), (324, 166)], fill=(40, 90, 70))
    d.ellipse([334, 162, 348, 169], fill=(255, 244, 200)); e.ellipse([334, 162, 348, 169], fill=255)

    # soldering station: sponge, coil stand, the iron
    d.rectangle([362, 206, 398, 213], fill=(70, 70, 74))
    d.rectangle([366, 202, 380, 206], fill=(220, 190, 60))
    for i in range(6):
        d.ellipse([386 + i * 2, 196 - i * 4, 392 + i * 2, 202 - i * 4], outline=(170, 170, 170))
    d.line([404, 170, 390, 186], fill=(40, 90, 200), width=4)
    d.line([390, 186, 372, 201], fill=(190, 190, 190), width=2)

    # the laptop: its screen is the firmware's
    d.rectangle([412, 166, 470, 205], fill=(40, 40, 46))
    d.rectangle([412, 166, 470, 205], outline=(90, 90, 98))
    d.rectangle([415, 169, 467, 202], fill=(6, 16, 12)); e.rectangle([415, 169, 467, 202], fill=255)
    d.point((441, 167), fill=(20, 20, 20))                    # the camera
    d.polygon([(406, 205), (476, 205), (482, 213), (400, 213)], fill=(150, 150, 158))
    d.line([406, 205, 476, 205], fill=(190, 190, 198))
    for ky in (207, 209):
        for kx in range(410, 474, 4):
            d.rectangle([kx, ky, kx + 2, ky], fill=(70, 70, 76))
    d.rectangle([432, 211, 450, 212], fill=(120, 120, 128))   # trackpad
    d.point((474, 211), fill=(60, 255, 60)); e.point((474, 211), fill=255)
    # a spool of solder
    d.rectangle([486, 200, 494, 213], fill=(60, 50, 40)); d.rectangle([487, 202, 493, 211], fill=(190, 190, 196))
    # multimeter
    d.rounded_rectangle([498, 184, 520, 213], radius=3, fill=(230, 190, 30))
    d.rectangle([501, 187, 517, 195], fill=(150, 190, 150)); e.rectangle([501, 187, 517, 195], fill=180)
    d.text((502, 186), "3.3", fill=(20, 40, 20))
    d.ellipse([504, 198, 514, 208], fill=(40, 40, 40)); d.line([509, 203, 512, 199], fill=(220, 220, 220))
    # a mug
    d.rectangle([526, 200, 538, 213], fill=(230, 230, 226)); d.arc([534, 203, 542, 210], 270, 90, fill=(230, 230, 226), width=2)
    d.rectangle([528, 204, 536, 207], fill=(60, 140, 60))

    # the radar set
    rx, ry, rr = SCENE["radar_centre"][0] // 2, SCENE["radar_centre"][1] // 2, SCENE["radar_r"] // 2
    d.rounded_rectangle([550, 144, 638, 213], radius=4, fill=(58, 66, 54))
    d.rounded_rectangle([550, 144, 638, 213], radius=4, outline=(90, 100, 84))
    d.ellipse([rx - rr - 3, ry - rr - 3, rx + rr + 3, ry + rr + 3], fill=(24, 26, 24))
    d.ellipse([rx - rr, ry - rr, rx + rr, ry + rr], fill=(4, 26, 10))
    for k in (1, 2, 3):
        r = rr * k // 3
        d.ellipse([rx - r, ry - r, rx + r, ry + r], outline=(16, 70, 30))
    d.line([rx - rr, ry, rx + rr, ry], fill=(16, 70, 30)); d.line([rx, ry - rr, rx, ry + rr], fill=(16, 70, 30))
    e.ellipse([rx - rr, ry - rr, rx + rr, ry + rr], fill=255)
    d.ellipse([624, 196, 632, 204], fill=(30, 30, 30)); d.ellipse([556, 196, 564, 204], fill=(30, 30, 30))

    # a stack of books against the wall
    for i, col in enumerate([(150, 40, 40), (40, 90, 150), (180, 150, 60)]):
        d.rectangle([606, 300 - i * 6, 638 - i * 3, 306 - i * 6], fill=col)

    return img, emit

def light(img, emit, on):
    a = np.asarray(img).astype(np.float32) / 255.0
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
    def blob(cx, cy, r, col, p=2.0):
        f = np.clip(1 - np.hypot(xx - cx, yy - cy) / r, 0, 1) ** p
        return f[..., None] * np.array(col, np.float32)
    if on:
        L = np.full((H, W, 3), (0.42, 0.40, 0.38), np.float32)
        L += blob(210, 95, 420, (1.0, 0.86, 0.6), 1.3)
    else:
        L = np.full((H, W, 3), (0.12, 0.13, 0.20), np.float32)
        # moonlight through the window, down onto the wall and floor
        mask = Image.new("L", (W, H), 0)
        ImageDraw.Draw(mask).polygon([(250, 76), (392, 76), (300, 324), (120, 324)], fill=255)
        m = np.asarray(mask).astype(np.float32)[..., None] / 255.0
        fade = np.clip(1 - (yy - 76) / 300, 0.25, 1)[..., None]      # strongest near the glass
        L += m * fade * np.array((0.07, 0.09, 0.17), np.float32)
    # the desk lamp: a pool on the bench and up the pegboard, always on
    cone = Image.new("L", (W, H), 0)
    ImageDraw.Draw(cone).polygon([(324, 166), (358, 166), (424, 213), (296, 213)], fill=255)
    cm = np.asarray(cone).astype(np.float32)[..., None] / 255.0
    L += cm * blob(341, 190, 120, (1.3, 1.05, 0.65), 1.0) * 1.2
    L += blob(341, 200, 160, (0.9, 0.7, 0.4), 1.6)
    # glows from the radar and the Nixie clock
    L += blob(595, 178, 60, (0.05, 0.35, 0.12), 2.0)
    L += blob(441, 186, 70, (0.06, 0.22, 0.16), 2.0)        # the laptop's screen
    L += blob(322, 94, 70, (0.55, 0.22, 0.04), 2.0)
    out = a * L
    em = np.asarray(emit).astype(np.float32)[..., None] / 255.0
    out = out * (1 - em) + a * em
    if on:   # the bulb itself
        b = np.clip(1 - np.hypot(xx - 210, yy - 95) / 9, 0, 1)[..., None]
        out = np.maximum(out, b * 3 * np.array((1.0, 0.95, 0.8), np.float32))
        out += blob(210, 95, 30, (0.6, 0.5, 0.3), 2.0)
    else:
        d = np.hypot(xx - 210, yy - 95) <= 8
        out[d] = (0.30, 0.30, 0.32)
    out = np.clip(out, 0, 1)
    im = Image.fromarray((out * 255).astype(np.uint8), "RGB")
    return im.resize((W * 2, H * 2), Image.NEAREST)

def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    img, emit = paint()
    for on in (True, False):
        p = os.path.join(out, "basement_light_%s.png" % ("on" if on else "off"))
        light(img, emit, on).save(p, optimize=True)
        print(p, os.path.getsize(p))

if __name__ == "__main__":
    main()
