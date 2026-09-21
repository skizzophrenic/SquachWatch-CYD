#!/usr/bin/env python3
"""USB checks/screenshots for the original Cardputer (requires pyserial).

Examples:
  python tools/cardputer_bench.py --port PORT --command FRAMECHECK
  python tools/cardputer_bench.py --port PORT --key k --screenshot settings.png
  python tools/cardputer_bench.py --port PORT --seconds 3600 --log soak.log

KEY mirrors physical controls; 'e' means Enter, '`' means Back, ' ' is Space.
Keys and TIME can change persistent settings just as on the device. Read-only
STATUS, SCREEN and FRAMECHECK do not award detections or game progress.
"""
import argparse
from pathlib import Path
import struct
import time
import zlib

import serial


def save_png(path, rows):
    if set(rows) != set(range(135)):
        raise RuntimeError(f"Incomplete framebuffer: {len(rows)}/135 rows")
    def chunk(kind, data):
        return (struct.pack(">I", len(data)) + kind + data
                + struct.pack(">I", zlib.crc32(kind + data)))
    pixels = bytearray()
    for y in range(135):
        pixels.append(0)  # PNG filter: none
        for p in rows[y]:
            pixels.extend((((p >> 11) & 31) * 255 // 31,
                           ((p >> 5) & 63) * 255 // 63,
                           (p & 31) * 255 // 31))
    image = b"\x89PNG\r\n\x1a\n"
    image += chunk(b"IHDR", struct.pack(">IIBBBBB", 240, 135, 8, 2, 0, 0, 0))
    image += chunk(b"IDAT", zlib.compress(pixels)) + chunk(b"IEND", b"")
    Path(path).write_bytes(image)


class Bench:
    def __init__(self, port, log=None):
        self.port = serial.Serial(port, 115200, timeout=0.2)
        self.port.reset_input_buffer()
        self.log = log
        self.lines = []

    def close(self):
        self.port.close()

    def send(self, command):
        self.port.write((command + "\n").encode("ascii"))

    def read(self):
        line = self.port.readline().decode("ascii", errors="replace").strip()
        if line and not line.startswith("[screen]"):
            print(line, flush=True)
            self.lines.append(line)
            if self.log:
                self.log.write(line + "\n")
                self.log.flush()
        return line

    def drain(self, seconds):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            self.read()

    def key(self, key):
        if len(key) != 1 or key not in "sld;.e`pob ckjh,/ix":
            raise ValueError(f"Unknown key: {key!r}")
        self.send("KEY " + key)
        self.drain(0.25)  # allow one frame before a capture or next action

    def screenshot(self, path):
        self.send("SCREEN")
        rows = {}
        end = time.monotonic() + 8
        while time.monotonic() < end and len(rows) < 135:
            line = self.read()
            if line.startswith("[screen] "):
                parts = line.split()
                if len(parts) != 3 or len(parts[2]) != 960:
                    raise RuntimeError("Corrupt framebuffer row; close other serial monitors")
                row = int(parts[1])
                if not 0 <= row < 135:
                    raise RuntimeError("Invalid framebuffer row")
                rows[row] = [int(parts[2][x:x+4], 16) for x in range(0, 960, 4)]
        save_png(path, rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--key", action="append", default=[])
    parser.add_argument("--command", action="append", default=[])
    parser.add_argument("--seconds", type=float, default=1)
    parser.add_argument("--screenshot")
    parser.add_argument("--log")
    args = parser.parse_args()
    log = open(args.log, "w") if args.log else None
    bench = None
    try:
        bench = Bench(args.port, log)
        for key in args.key:
            bench.key(key)
        for command in args.command:
            bench.send(command)
        bench.send("STATUS")
        bench.drain(max(0.25, args.seconds))
        if args.screenshot:
            bench.screenshot(args.screenshot)
        if "FRAMECHECK" in args.command and not any(
                "[framecheck] PASS pixels=32400 stale=0" in s for s in bench.lines):
            raise RuntimeError("Framebuffer check did not pass")
    finally:
        if bench:
            bench.close()
        if log:
            log.close()


if __name__ == "__main__":
    main()
