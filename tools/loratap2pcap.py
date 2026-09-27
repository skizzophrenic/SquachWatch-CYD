#!/usr/bin/env python3
"""Turn a SquachWatch serial log into a Wireshark capture.

With `LORA TAP` switched on over the console, the CrowPanel 7 prints one
line per LoRa frame:

    [tap] <hex>

where <hex> is a LoRaTap header (version 0, 15 bytes, big-endian fields)
followed by the frame's bytes. This script wraps those lines in a pcap file
with link type 270 (LINKTYPE_LORATAP), which Wireshark opens straight into
its LoRaTap and LoRaWAN dissectors.

    pio device monitor -b 115200 | tee squach.log
    python3 tools/loratap2pcap.py squach.log capture.pcap

Frames without a timestamp on the line are stamped as they are read.
"""
import re
import struct
import sys
import time

LINKTYPE_LORATAP = 270


def main(argv):
    if len(argv) != 3:
        print(__doc__)
        return 2
    src, dst = argv[1], argv[2]
    pat = re.compile(r"\[tap\]\s+([0-9a-fA-F]+)")
    n = 0
    t0 = time.time()
    with open(src, "r", errors="replace") as f, open(dst, "wb") as out:
        # pcap global header: magic, 2.4, zone 0, sigfigs 0, snaplen, linktype
        out.write(struct.pack("<IHHiIII", 0xA1B2C3D4, 2, 4, 0, 0, 65535, LINKTYPE_LORATAP))
        for line in f:
            m = pat.search(line)
            if not m:
                continue
            data = bytes.fromhex(m.group(1))
            ts = t0 + n * 0.001   # keep them in order; the board has no epoch on the line
            out.write(struct.pack("<IIII", int(ts), int((ts % 1) * 1e6), len(data), len(data)))
            out.write(data)
            n += 1
    print(f"{n} frames -> {dst}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
