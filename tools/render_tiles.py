"""Render OpenStreetMap ways (Overpass JSON, `out geom`) into 256 px XYZ tiles.

Dark style for the SquachWatch Tab5 base station. Tiles land at
<out>/<z>/<x>/<y>.png, the standard slippy-map layout. Map data
(c) OpenStreetMap contributors, ODbL -- the map screen shows that credit.

Usage: render_tiles.py area.json OUTDIR ZMIN ZMAX SOUTH WEST NORTH EAST

The four numbers are the box to draw, in degrees -- the Overpass query's own
box, or a smaller one inside it to draw the closest zooms only near home.
Tiles with nothing on them are not written: the Tab5 shows its background
for a tile the card does not have. Every core is used.
"""
import json, math, os, sys
from multiprocessing import Pool
from collections import defaultdict
from PIL import Image, ImageDraw

SS = 2                      # supersample, for smoother lines
T = 256

BG      = (24, 28, 36)
WATER   = (24, 58, 98)
COAST   = (60, 110, 170)
PARK    = (28, 60, 40)
ROAD = {   # class: (colour, width in px at z16; thinner as you zoom out)
    "motorway": ((230, 140, 60), 7), "trunk": ((230, 160, 70), 6),
    "primary": ((220, 190, 110), 5.5), "secondary": ((200, 200, 160), 5),
    "tertiary": ((180, 185, 190), 4.5), "unclassified": ((150, 155, 165), 3.5),
    "residential": ((140, 145, 155), 3.5), "living_street": ((130, 135, 145), 3),
    "service": ((95, 100, 110), 2),
}
ORDER = ["service", "living_street", "residential", "unclassified", "tertiary",
         "secondary", "primary", "trunk", "motorway"]
MIN_ZOOM = {"service": 15, "living_street": 14, "residential": 13, "unclassified": 13}

def merc(lat, lon, z):
    n = 2.0 ** z * T
    x = (lon + 180.0) / 360.0 * n
    s = math.sin(math.radians(lat))
    y = (0.5 - math.log((1 + s) / (1 - s)) / (4 * math.pi)) * n
    return x, y

WAYS = []

def load(src):
    ways = []
    for e in json.load(open(src, encoding="utf-8"))["elements"]:
        g = e.get("geometry")
        if not g or len(g) < 2:
            continue
        t = e.get("tags", {})
        if "highway" in t:
            cls = t["highway"].replace("_link", "")
            if cls not in ROAD:
                continue
            kind = ("road", cls)
        elif t.get("natural") == "water":
            kind = ("water", None)
        elif t.get("natural") == "coastline":
            kind = ("coast", None)
        elif t.get("leisure") == "park":
            kind = ("park", None)
        else:
            continue
        lats = [p["lat"] for p in g]; lons = [p["lon"] for p in g]
        ways.append((kind, [(p["lat"], p["lon"]) for p in g], (min(lats), min(lons), max(lats), max(lons))))
    return ways

def init(src):
    global WAYS
    WAYS = load(src)

def draw_tile(job):
    z, tx, ty, ids, out = job
    scale = 2.0 ** (z - 16)
    img = Image.new("RGB", (T * SS, T * SS), BG)
    dr = ImageDraw.Draw(img)
    def px(pts):
        return [((merc(la, lo, z)[0] - tx * T) * SS, (merc(la, lo, z)[1] - ty * T) * SS) for la, lo in pts]
    for i in ids:                       # areas first
        kind, pts, _ = WAYS[i]
        if kind[0] in ("water", "park") and len(pts) >= 3:
            dr.polygon(px(pts), fill=WATER if kind[0] == "water" else PARK)
    for i in ids:
        kind, pts, _ = WAYS[i]
        if kind[0] == "coast":
            dr.line(px(pts), fill=COAST, width=max(1, int(3 * SS * max(scale, 0.5))), joint="curve")
    for cls in ORDER:                   # small roads under big ones
        col, w = ROAD[cls]
        width = max(1, int(round(w * SS * max(scale, 0.25))))
        for i in ids:
            kind, pts, _ = WAYS[i]
            if kind == ("road", cls):
                dr.line(px(pts), fill=col, width=width, joint="curve")
    img = img.resize((T, T), Image.LANCZOS)
    if img.getextrema() == tuple((c, c) for c in BG):
        return 0                        # nothing drawn: leave it to the background
    img = img.quantize(colors=64)
    d = os.path.join(out, str(z), str(tx))
    os.makedirs(d, exist_ok=True)
    img.save(os.path.join(d, "%d.png" % ty), optimize=True)
    return 1

def main():
    src, out, zmin, zmax = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
    # The area to draw, not the ways' own extent: a coastline way can run far
    # outside it.
    lat0, lon0, lat1, lon1 = (float(v) for v in sys.argv[5:9])
    ways = load(src)
    print(len(ways), "ways to draw", flush=True)
    total = 0
    with Pool(initializer=init, initargs=(src,)) as pool:
        for z in range(zmin, zmax + 1):
            # Bucket each way into the tiles its bounding box touches.
            buckets = defaultdict(list)
            for i, (kind, pts, (a, b, c, d)) in enumerate(ways):
                if kind[0] == "road" and z < MIN_ZOOM.get(kind[1], 0):
                    continue
                x0, y0 = merc(c, b, z); x1, y1 = merc(a, d, z)
                for tx in range(int(x0 // T), int(x1 // T) + 1):
                    for ty in range(int(y0 // T), int(y1 // T) + 1):
                        buckets[(tx, ty)].append(i)
            xa, ya = merc(lat1, lon0, z); xb, yb = merc(lat0, lon1, z)
            jobs = [(z, tx, ty, buckets[(tx, ty)], out)
                    for tx in range(int(xa // T), int(xb // T) + 1)
                    for ty in range(int(ya // T), int(yb // T) + 1)
                    if (tx, ty) in buckets]
            n = sum(pool.imap_unordered(draw_tile, jobs, chunksize=16))
            total += n
            print("zoom %d: %d tiles" % (z, n), flush=True)
    print("done:", total, "tiles")

if __name__ == "__main__":
    main()
