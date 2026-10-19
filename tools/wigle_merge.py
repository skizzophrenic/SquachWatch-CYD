#!/usr/bin/env python3
"""SquachWatch-CYD: places the Waveshare 2.06's wardrive rows using a phone's GPX track.

The watch has no GPS. While WARDRIVE is on it writes every network and Bluetooth
device it hears to /wigle-YYYYMMDD-HHMM.csv on its SD card, with the watch's UTC
time. Rows written while the phone's GPS link (web-flasher/wardrive/) was
connected already have coordinates and are kept as they are. The rest have 0,0
and are placed from a GPX track recorded on the phone over the same stretch of
time (the GPS link page's "Save GPX track", or any GPX logger app):

    python3 tools/wigle_merge.py track.gpx wigle-20261016-1830.csv

With no GPX the 0,0 rows are just left out, so the file uploads cleanly:

    python3 tools/wigle_merge.py wigle-20261016-1830.csv

Each row gets the phone's position at that second, interpolated between the
two track points either side of it. A row is left out when it falls outside
the track, or when the phone had no fix for longer than --max-gap seconds
around that moment. The result is a WigleWifi-1.6 file that uploads to
wigle.net as it is (Uploads -> Upload File).

Several GPX files and several watch files can be given in any order. The rows
all go into one output file, written to wigle-merged-<first watch file's
time>.csv unless -o names another.

If the watch's clock was off, --offset corrects it. The value is how many
seconds the watch was AHEAD of true time, so it is negative if the watch was
behind. A wrong clock shows as networks placed along the road a little before
or after where they really are.

Uses the standard library only.
"""
import argparse
import bisect
import csv
import io
import math
import os
import sys
import xml.etree.ElementTree as ET
from datetime import datetime, timezone


def parse_time(s):
    s = s.strip()
    if s.endswith("Z"):
        s = s[:-1] + "+00:00"
    # Python before 3.11 accepts only 0, 3 or 6 digits of fractional seconds.
    if "." in s:
        head, rest = s.split(".", 1)
        frac = ""
        i = 0
        while i < len(rest) and rest[i].isdigit():
            frac += rest[i]
            i += 1
        s = head + "." + (frac + "000000")[:6] + rest[i:]
    t = datetime.fromisoformat(s)
    if t.tzinfo is None:
        t = t.replace(tzinfo=timezone.utc)   # GPX times are UTC
    return t.timestamp()


def read_gpx(path):
    """[(epoch, lat, lon, ele or None, hdop or None)] from every trkpt/rtept that has a time."""
    pts = []
    for _, el in ET.iterparse(path):
        tag = el.tag.rsplit("}", 1)[-1]
        if tag not in ("trkpt", "rtept"):
            continue
        t = ele = hdop = None
        for c in el:
            ct = c.tag.rsplit("}", 1)[-1]
            if ct == "time" and c.text:
                t = parse_time(c.text)
            elif ct == "ele" and c.text:
                try: ele = float(c.text)
                except ValueError: pass
            elif ct == "hdop" and c.text:
                try: hdop = float(c.text)
                except ValueError: pass
        if t is not None:
            pts.append((t, float(el.get("lat")), float(el.get("lon")), ele, hdop))
        el.clear()
    return pts


def metres(la1, lo1, la2, lo2):
    k = 111319.0
    dy = (la2 - la1) * k
    dx = (lo2 - lo1) * k * math.cos(math.radians(la1))
    return math.hypot(dx, dy)


class Track:
    def __init__(self, pts):
        pts.sort(key=lambda p: p[0])
        # Duplicate timestamps would divide by zero below; the first one is kept.
        self.pts = []
        for p in pts:
            if not self.pts or p[0] > self.pts[-1][0]:
                self.pts.append(p)
        self.t = [p[0] for p in self.pts]

    def at(self, t, max_gap):
        """(lat, lon, ele, acc) at time t, or None."""
        if not self.pts or t < self.t[0] or t > self.t[-1]:
            return None
        i = bisect.bisect_left(self.t, t)
        if self.t[i] == t:
            a = b = self.pts[i]
        else:
            a, b = self.pts[i - 1], self.pts[i]
        if b[0] - a[0] > max_gap:
            return None
        f = 0.0 if b[0] == a[0] else (t - a[0]) / (b[0] - a[0])
        lat = a[1] + (b[1] - a[1]) * f
        lon = a[2] + (b[2] - a[2]) * f
        ele = None
        if a[3] is not None and b[3] is not None:
            ele = a[3] + (b[3] - a[3]) * f
        # The phone's own error, and how far it moved between the two points
        # (a long gap at speed is less certain than a short one).
        hd = [h for h in (a[4], b[4]) if h is not None]
        acc = max(hd) * 5.0 if hd else 10.0
        acc = max(acc, min(metres(a[1], a[2], b[1], b[2]) / 2.0, 50.0))
        return lat, lon, ele, acc


def read_watch(path):
    """(the WigleWifi pre-header line, [row dicts]) from one watch file."""
    with open(path, newline="", encoding="utf-8", errors="replace") as f:
        first = f.readline()
        if not first.startswith("WigleWifi-"):
            raise SystemExit(f"{path}: not a WiGLE file (its first line is not WigleWifi-...)")
        rows = list(csv.DictReader(f))
    return first.rstrip("\r\n"), rows


COLUMNS = ["MAC", "SSID", "AuthMode", "FirstSeen", "Channel", "Frequency", "RSSI",
           "CurrentLatitude", "CurrentLongitude", "AltitudeMeters", "AccuracyMeters",
           "RCOIs", "MfgrId", "Type"]


def main():
    ap = argparse.ArgumentParser(description="Place SquachWatch wardrive rows using a phone's GPX track.")
    ap.add_argument("files", nargs="+", help="watch wigle-*.csv file(s) and, optionally, GPX track(s), in any order")
    ap.add_argument("-o", "--output", help="the merged file (default wigle-merged-<time>.csv)")
    ap.add_argument("--offset", type=float, default=0.0,
                    help="seconds the watch's clock was AHEAD of true time (negative if behind)")
    ap.add_argument("--max-gap", type=float, default=30.0,
                    help="leave out rows where the track has no point for this many seconds (default 30)")
    a = ap.parse_args()

    gpx = [p for p in a.files if p.lower().endswith(".gpx")]
    watch = [p for p in a.files if not p.lower().endswith(".gpx")]
    if not watch:
        ap.error("give at least one watch wigle-*.csv file")

    pts = []
    for p in gpx:
        got = read_gpx(p)
        print(f"{p}: {len(got)} track points")
        pts += got
    track = Track(pts)
    if gpx and not track.pts:
        raise SystemExit("no timed track points in the GPX file(s)")
    fmt = lambda e: datetime.fromtimestamp(e, timezone.utc).strftime("%Y-%m-%d %H:%M:%S")
    if track.pts:
        print(f"track: {fmt(track.t[0])} to {fmt(track.t[-1])} UTC")
    else:
        print("no GPX: keeping the rows the phone link placed, leaving out the rest")

    pre = None
    out_rows = []
    total = outside = kept = 0
    for p in watch:
        line, rows = read_watch(p)
        pre = pre or line
        first = last = None
        for r in rows:
            total += 1
            try:
                t = datetime.strptime(r["FirstSeen"], "%Y-%m-%d %H:%M:%S").replace(tzinfo=timezone.utc).timestamp()
            except (KeyError, ValueError):
                outside += 1
                continue
            first = t if first is None else min(first, t)
            last = t if last is None else max(last, t)
            # Placed on the watch already (the phone's GPS over Bluetooth):
            # kept as it is.
            try:
                placed = float(r.get("CurrentLatitude") or 0) != 0 or float(r.get("CurrentLongitude") or 0) != 0
            except ValueError:
                placed = False
            if placed:
                kept += 1
                out_rows.append(r)
                continue
            pos = track.at(t - a.offset, a.max_gap)
            if pos is None:
                outside += 1
                continue
            lat, lon, ele, acc = pos
            r = dict(r)
            r["FirstSeen"] = fmt(t - a.offset)
            r["CurrentLatitude"] = f"{lat:.7f}"
            r["CurrentLongitude"] = f"{lon:.7f}"
            r["AltitudeMeters"] = f"{ele:.0f}" if ele is not None else r.get("AltitudeMeters", "0")
            r["AccuracyMeters"] = f"{acc:.0f}"
            out_rows.append(r)
        if first is not None:
            print(f"{p}: {len(rows)} rows, {fmt(first)} to {fmt(last)} UTC (watch clock)")
        else:
            print(f"{p}: no rows")

    out = a.output
    if not out:
        base = os.path.basename(watch[0])
        stamp = base[len("wigle-"):-len(".csv")] if base.startswith("wigle-") and base.endswith(".csv") else "out"
        out = f"wigle-merged-{stamp}.csv"
    buf = io.StringIO()
    w = csv.DictWriter(buf, fieldnames=COLUMNS, extrasaction="ignore", lineterminator="\n")
    w.writeheader()
    for r in out_rows:
        w.writerow(r)
    with open(out, "w", newline="", encoding="utf-8") as f:
        f.write(pre + "\n")
        f.write(buf.getvalue())
    print(f"{out}: {len(out_rows)} rows ({kept} placed by the phone link, {len(out_rows) - kept} from the GPX), "
          f"{outside} of {total} left out (no position for them)")
    if total and not out_rows:
        print("nothing landed on the track: check that the GPX covers the same time, or the watch's clock (--offset)",
              file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
