#!/usr/bin/env python3
"""Grab a screenshot from a running SquachWatch-CYD over USB serial.

    python tools/screenshot.py COM33 -o shot.png

The device answers an "SQCAP\\n" request with one framed RGB565 snapshot
of whatever is on screen right now -- see include/screencap.h for the
wire format. This is the PC half of that: send the request, find the
frame among the device's ordinary debug chatter on the same port, and
write it out as a PNG.

PNG is emitted by hand from stdlib zlib rather than pulling in Pillow --
it's about thirty lines for the uncompressed-scanline case, and keeping
the dependency list at "pyserial" makes this trivial for anyone to run.
"""

import argparse
import struct
import sys
import time
import zlib

try:
    import serial  # pyserial
except ImportError:
    sys.exit("needs pyserial:  pip install pyserial")

MAGIC = b"SQFB"
HEADER_LEN = 14
FMT_RGB565 = 0   # 2 bytes per pixel
FMT_RGB332 = 1   # 1 byte per pixel -- what the device's 8bpp sprite holds
BYTES_PER_PX = {FMT_RGB565: 2, FMT_RGB332: 1}

# Set by --force. Writing a frame the checksum rejects is normally a bad
# idea, but "what does the broken image actually look like?" is the
# fastest way to tell corrupted bytes from a misread pixel format.
ALLOW_BAD_CHECKSUM = False


def read_exact(port, n, deadline):
    """Accumulate exactly n bytes, or raise once the deadline passes.

    pyserial's read(n) returns early with a short result when its own
    timeout expires -- and it always will here, since a 150KB payload at
    2,000,000 baud takes the better part of a second to arrive. Looping
    against a wall-clock deadline is what actually reads a large frame.
    """
    buf = bytearray()
    while len(buf) < n:
        if time.time() > deadline:
            raise IOError(f"timed out after {len(buf)} of {n} bytes")
        chunk = port.read(n - len(buf))
        if chunk:
            buf += chunk
    return bytes(buf)


def read_frame(port, timeout_s):
    """Scan the stream for a frame header, then read and verify the payload."""
    deadline = time.time() + timeout_s
    window = b""

    # Hunt for the magic one byte at a time. Anything that isn't part of
    # the magic is the device's normal log output sharing this port, so
    # it gets echoed rather than silently swallowed -- if something goes
    # wrong, the error text is usually the explanation.
    while time.time() < deadline:
        b = port.read(1)
        if not b:
            continue
        window += b
        if window.endswith(MAGIC):
            noise = window[: -len(MAGIC)]
            if noise.strip():
                print(noise.decode("utf-8", "replace").rstrip(), file=sys.stderr)
            break
        if len(window) > 4096:
            window = window[-len(MAGIC):]
    else:
        raise TimeoutError("no frame header arrived before the timeout")

    # Everything past the magic is fixed-size and must arrive intact, so
    # the rest of the frame reads against a deadline rather than trusting
    # any single read() to come back full.
    deadline = time.time() + timeout_s
    rest = read_exact(port, HEADER_LEN - len(MAGIC), deadline)
    version, fmt, width, height, length = struct.unpack("<BBHHI", rest)

    if version != 1:
        raise IOError(f"unsupported protocol version {version}")
    if fmt not in BYTES_PER_PX:
        raise IOError(f"unsupported payload format {fmt}")
    expected = width * height * BYTES_PER_PX[fmt]
    if length != expected:
        raise IOError(f"payload length {length} doesn't match {width}x{height} format {fmt} ({expected})")

    payload = read_exact(port, length, deadline)
    (checksum,) = struct.unpack("<H", read_exact(port, 2, deadline))
    actual = sum(payload) & 0xFFFF
    if actual != checksum:
        msg = f"checksum mismatch: device said {checksum}, payload sums to {actual}"
        if not ALLOW_BAD_CHECKSUM:
            raise IOError(msg)
        print(f"warning: {msg} -- writing anyway", file=sys.stderr)

    return width, height, fmt, payload


def to_rgb888(payload, width, height, fmt, swap):
    """Expand the device's pixels to 8-bit-per-channel PNG scanlines.

    Each channel's high bits are replicated down into the low bits rather
    than zero-filled, so full-scale stays full-scale (5-bit 0x1F becomes
    0xFF, not 0xF8) instead of the whole image reading slightly dim.
    """
    rows = bytearray()
    i = 0
    for _ in range(height):
        rows.append(0)  # PNG per-scanline filter: 0 = None
        for _ in range(width):
            if fmt == FMT_RGB565:
                lo, hi = payload[i], payload[i + 1]
                i += 2
                v = (lo << 8) | hi if swap else (hi << 8) | lo
                r, g, b = (v >> 11) & 0x1F, (v >> 5) & 0x3F, v & 0x1F
                rows.append((r << 3) | (r >> 2))
                rows.append((g << 2) | (g >> 4))
                rows.append((b << 3) | (b >> 2))
            else:  # FMT_RGB332 -- RRRGGGBB in one byte
                v = payload[i]
                i += 1
                r, g, b = (v >> 5) & 0x7, (v >> 2) & 0x7, v & 0x3
                rows.append((r << 5) | (r << 2) | (r >> 1))
                rows.append((g << 5) | (g << 2) | (g >> 1))
                rows.append((b << 6) | (b << 4) | (b << 2) | b)
    return bytes(rows)


def write_png(path, width, height, rgb_rows):
    def chunk(tag, data):
        return (
            struct.pack(">I", len(data))
            + tag
            + data
            + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
        )

    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)  # 8-bit truecolor
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", ihdr))
        f.write(chunk(b"IDAT", zlib.compress(rgb_rows, 9)))
        f.write(chunk(b"IEND", b""))


def main():
    ap = argparse.ArgumentParser(description="Screenshot a SquachWatch-CYD over serial.")
    ap.add_argument("port", help="serial port, e.g. COM33 or /dev/ttyUSB0")
    ap.add_argument("-o", "--out", default="squachwatch.png", help="output PNG path")
    ap.add_argument("-b", "--baud", type=int, default=2000000,
                    help="must match the board's SERIAL_BAUD (cyd/cyd-ili9341 use 2000000, awok 921600)")
    ap.add_argument("-t", "--timeout", type=float, default=10.0, help="seconds to wait for a frame")
    ap.add_argument("--swap", action="store_true",
                    help="swap RGB565 byte order if colors come out wrong")
    ap.add_argument("--force", action="store_true",
                    help="write the PNG even if the checksum fails")
    args = ap.parse_args()

    global ALLOW_BAD_CHECKSUM
    ALLOW_BAD_CHECKSUM = args.force

    with serial.Serial(args.port, args.baud, timeout=0.2) as port:
        # The board reboots when the port opens (DTR/RTS auto-reset on
        # most CYDs), so give it a moment to get back to drawing frames
        # before asking for one.
        time.sleep(2.0)
        port.reset_input_buffer()
        port.write(b"SQCAP\n")
        port.flush()

        width, height, fmt, payload = read_frame(port, args.timeout)

    rgb = to_rgb888(payload, width, height, fmt, args.swap)
    write_png(args.out, width, height, rgb)
    print(f"saved {args.out}  ({width}x{height}, format {fmt})")


if __name__ == "__main__":
    main()
