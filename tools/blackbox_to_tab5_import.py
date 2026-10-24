"""A board's black box, as a file the Tab5 base station imports onto its map.

Reads a board's detection history over USB with its BLACKBOX console
command, or from a saved dump, and writes it in the Tab5's catches format
(/squachwatch-base/catches.csv) for /squachwatch-base/import/ on the Tab5's
card, which the base station reads at every start:

    python tools/blackbox_to_tab5_import.py COM3 MYBOARD out.csv
    python tools/blackbox_to_tab5_import.py dump.txt MYBOARD out.csv

The port is opened with DTR and RTS already low: on a board's own USB,
toggling them is a reset. The address tail is written the way a live
HEADSUP prints it (..mac[5]:mac[4]:mac[3]), so a device seen live and in
an import is the same device on the Tab5.
"""
import datetime, os, re, sys, time

def read_dump(src):
    if os.path.exists(src):
        return open(src, encoding="utf-8").read()
    import serial
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = src, 115200, 0.2
    s.dtr = False
    s.rts = False
    s.open()
    s.reset_input_buffer()
    s.write(b"BLACKBOX\r\n")
    buf, t0, last = b"", time.time(), time.time()
    while time.time() - t0 < 60:
        d = s.read(16384)
        if d:
            buf, last = buf + d, time.time()
        elif time.time() - last > 4 and len(buf) > 1000:
            break
    s.close()
    return buf.decode("utf-8", "replace")

def main():
    src, board, out = sys.argv[1], sys.argv[2], sys.argv[3]
    lines = read_dump(src).splitlines()
    start = next(i for i, l in enumerate(lines) if l.startswith("boot,epoch,up_s,type,mac"))
    rows = []
    for l in lines[start + 1:]:
        c = l.split(",")
        if len(c) < 13 or not re.match(r"^[0-9a-f:]{17}$", c[4]):
            continue
        epoch, typ, mac, rssi, lat, lon = int(c[1]), c[3], c[4], c[5], c[11], c[12]
        utc = (datetime.datetime.fromtimestamp(epoch, datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
               if epoch > 1_700_000_000 else "")
        b = mac.upper().split(":")
        rows.append((epoch, utc, typ, rssi, "%s:%s:%s" % (b[5], b[4], b[3]), lat, lon))
    rows.sort(key=lambda r: r[0])          # oldest first: the Tab5 replays them in order
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write("utc,type,board,rssi,device_tail,lat,lon,acc_m,source,event\n")
        for _, utc, typ, rssi, tail, lat, lon in rows:
            f.write("%s,%s,%s,%s,%s,%s,%s,,import,\n" % (utc, typ, board, rssi, tail, lat, lon))
    print("%s: %d catches, %d with a position" % (out, len(rows), sum(1 for r in rows if r[5])))

if __name__ == "__main__":
    main()
