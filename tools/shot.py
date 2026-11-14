#!/usr/bin/env python3
"""Drive a SQW_DEVTOOLS build over serial and capture its screen as PNG.

  shot.py PORT out.png                 capture the current screen
  shot.py PORT out.png next next press capture after sending keys
  shot.py PORT - next                  send keys only
  shot.py --ext PORT out.png           the Cardputer's EXT panel (320x240) instead

Keys: next, prev, home, back, select, press, press:MS (a held press, e.g. press:900).
Only answers on a dev build (sticks3-dev, cardputer-adv-dev). The state number in the
output is the AppState the screen was in. See docs/SMALL-BOARDS-DEV.md.
Needs pyserial (PlatformIO's Python has it).
"""
import os
import struct
import sys
import time
import zlib

import serial


def send(s, line):
    s.write((line + "\n").encode())
    s.flush()


SHOT_CMD = os.environ.get("SHOT_CMD", "SHOT")


def capture(s, timeout=10.0):
    send(s, SHOT_CMD)
    buf = b""
    end = time.time() + timeout
    while time.time() < end:
        buf += s.read(65536)
        i = buf.find(b"[shot] BEGIN")
        if i >= 0 and b"[shot] END" in buf[i:]:
            body = buf[i:buf.index(b"[shot] END", i)].decode(errors="replace").splitlines()
            w, h, state = (int(v) for v in body[0].split()[2:5])
            got = {}
            for line in body[1:]:
                j = line.find("R")
                k = line.find(":", j)
                if j < 0 or k < 0 or not line[j + 1:k].isdigit():
                    continue
                hexrow = line[k + 1:k + 1 + 2 * w]
                if len(hexrow) == 2 * w:
                    got[int(line[j + 1:k])] = hexrow
            missing = [y for y in range(h) if y not in got]
            if missing:
                raise SystemExit(f"short capture: {len(missing)} of {h} rows missing")
            return w, h, state, [got[y] for y in range(h)]
    raise SystemExit("no [shot] reply (is this a -dev build?)")


def rgb332(v):
    r, g, b = (v >> 5) & 7, (v >> 2) & 7, v & 3
    return (r * 255 // 7, g * 255 // 7, b * 255 // 3)


def write_png(path, w, h, rows):
    raw = bytearray()
    for r in rows:
        raw.append(0)
        for x in range(w):
            raw.extend(rgb332(int(r[2 * x:2 * x + 2], 16)))

    def chunk(tag, data):
        c = tag + data
        return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def main():
    global SHOT_CMD
    if len(sys.argv) > 1 and sys.argv[1] == "--ext":
        SHOT_CMD = "SHOT EXT"
        del sys.argv[1]
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    port, out, keys = sys.argv[1], sys.argv[2], sys.argv[3:]
    s = serial.Serial(port, 115200, timeout=0.2)
    time.sleep(0.2)
    s.reset_input_buffer()
    for k in keys:
        name, _, ms = k.partition(":")
        name = name.lower()
        if name == "press":
            send(s, f"KEY PRESS {ms or 120}")
            time.sleep((int(ms) if ms else 120) / 1000 + 0.4)
        elif name in ("next", "prev", "home", "back", "select"):
            send(s, "KEY " + name.upper())
            time.sleep(0.35)
        else:
            raise SystemExit(f"unknown key {k}")
    if out != "-":
        time.sleep(0.3)
        s.reset_input_buffer()
        for attempt in range(3):
            try:
                w, h, state, rows = capture(s, timeout=6.0)
                break
            except SystemExit:
                if attempt == 2:
                    raise
                s.reset_input_buffer()
        write_png(out, w, h, rows)
        print(f"{out}: {w}x{h} state {state}")


if __name__ == "__main__":
    main()
