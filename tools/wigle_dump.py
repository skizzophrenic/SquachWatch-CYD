#!/usr/bin/env python3
"""Save a board's wardrive log as a WiGLE file (T-Watch S3 Plus, T-LoRa Pager).

    python tools/wigle_dump.py COM14                # real rows only
    python tools/wigle_dump.py COM14 --all          # bench (GPS FAKE) rows too
    python tools/wigle_dump.py COM14 -o trip.csv

The watch writes a WigleWifi-1.6 CSV between two marker lines when it is sent
WIGLE on its console (see wigleExport in src/main.cpp); this keeps what is
between them. The result uploads to https://wigle.net/uploads as it is.

The port is opened with DTR up and RTS down: on the S3's native USB, the
default open can be a reset, and a reset would lose nothing on flash but would
drop the GPS fix mid-drive. Only an S3's native USB port is opened (its USB
serial number is its MAC; the known watches are named).
"""
import csv, os, re, sys, time
import serial
import serial.tools.list_ports

ESPRESSIF_VID = 0x303A
MAC_RE = re.compile(r"^[0-9a-f]{2}(:[0-9a-f]{2}){5},")

WATCHES = {
    "A0:F2:62:E1:29:10": "T-Watch S3",
    "68:EE:8F:48:1F:E4": "T-Watch S3 Plus",
}

def main():
    if len(sys.argv) < 2:
        print(__doc__); return 2
    port = sys.argv[1]
    every = "--all" in sys.argv
    out = None
    if "-o" in sys.argv: out = sys.argv[sys.argv.index("-o") + 1]

    info = {p.device: p for p in serial.tools.list_ports.comports()}
    p = info.get(port)
    sn = (p.serial_number or "").upper() if p else ""
    # Any board on Espressif's own USB (the S3's native port, VID 0x303A) can
    # be asked; a USB-serial bridge (a CYD's CH340) would be reset by the open.
    if not p or p.vid != ESPRESSIF_VID:
        print("%s is not an ESP32-S3's own USB port (USB serial %r); nothing opened." % (port, sn or None)); return 1
    print("%s: %s (%s)" % (port, WATCHES.get(sn, "an S3 board"), sn))

    ser = serial.Serial()
    ser.port = port; ser.baudrate = 115200; ser.timeout = 0.5
    # DTR held up, RTS down, set before the open so the lines never pass through
    # DTR low with RTS high -- that is the S3's reset (EN), and on the T-LoRa
    # Pager it also left the port taking nothing more until a reflash.
    ser.dsrdtr = False; ser.rtscts = False; ser.dtr = True; ser.rts = False
    ser.open()
    ser.reset_input_buffer()
    ser.write(b"WIGLE ALL\n" if every else b"WIGLE\n")

    # The watch keeps logging while it waits for the command, so "no data"
    # never happens; what matters is whether the export itself starts, and
    # whether its rows keep coming.
    buf = b""; lines = []; inside = False; summary = ""
    start = time.time(); lastRow = time.time()
    while True:
        buf += ser.read(8192)
        while b"\n" in buf:
            raw, buf = buf.split(b"\n", 1)
            line = raw.rstrip(b"\r").decode("utf-8", "replace")
            if line.startswith("=== WIGLE BEGIN"): inside = True; lastRow = time.time(); continue
            if line.startswith("=== WIGLE END"): summary = line; inside = False; break
            if inside: lines.append(line); lastRow = time.time()
        if summary: break
        if not inside and not lines and time.time() - start > 15:
            print("the watch did not start an export (is its firmware new enough?); nothing saved"); return 1
        if inside and time.time() - lastRow > 10:
            print("the export stopped part-way (%d lines so far); nothing saved" % len(lines)); return 1
    ser.close()
    if "INCOMPLETE" in summary:
        print("the watch says the export was cut short; nothing saved"); return 1

    # The rest of the watch keeps printing while it exports (the radio tasks
    # log on their own), and a line of that can land between the WiGLE lines.
    # Only lines shaped like the file are kept: the two header lines, and rows
    # that start with an address and parse to WiGLE's 14 columns.
    pre = next((l for l in lines if l.startswith("WigleWifi-1.6,")), None)
    cols = next((l for l in lines if l.startswith("MAC,SSID,AuthMode,")), None)
    if not pre or not cols:
        print("no WiGLE header came back; nothing saved"); return 1
    rows, noise = [], 0
    for l in lines:
        if l is pre or l is cols: continue
        if MAC_RE.match(l) and len(next(csv.reader([l]))) == 14: rows.append(l)
        else: noise += 1
    m = re.search(r"END (\d+) rows", summary)
    sent = int(m.group(1)) if m else -1
    if sent >= 0 and sent != len(rows):
        print("the watch sent %d rows but %d arrived intact; saved anyway, check it" % (sent, len(rows)))
    if not out:
        out = "squachwatch-wigle-%s.csv" % time.strftime("%Y%m%d-%H%M%S")
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join([pre, cols] + rows) + "\n")
    print("%s -- %d rows saved to %s%s" % (summary.strip("= "), len(rows), os.path.abspath(out),
                                           " (%d other lines set aside)" % noise if noise else ""))
    return 0

if __name__ == "__main__":
    sys.exit(main())
