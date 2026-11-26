# Grab the Tab5's console the moment esptool lets go of it (dtr/rts off, no reset), then read N seconds.
import sys, time, serial
port = sys.argv[1]; secs = float(sys.argv[2]) if len(sys.argv) > 2 else 30
deadline = time.time() + 240
s = None
while time.time() < deadline:
    try:
        s = serial.Serial(); s.port = port; s.baudrate = 115200; s.timeout = 0.2
        s.dtr = False; s.rts = False
        s.open(); break
    except Exception:
        s = None; time.sleep(0.1)
if not s: sys.exit("could not open " + port)
print("[console] opened", port, flush=True)
end = time.time() + secs
while time.time() < end:
    d = s.read(4096)
    if d:
        sys.stdout.write(d.decode("utf-8", "replace")); sys.stdout.flush()
s.close()
