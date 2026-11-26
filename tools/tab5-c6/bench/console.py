# Read the Tab5's USB console without resetting it: dtr/rts off before open().
import sys, time, serial
port = sys.argv[1]; secs = float(sys.argv[2]) if len(sys.argv) > 2 else 15
s = serial.Serial(); s.port = port; s.baudrate = 115200; s.timeout = 0.2
s.dtr = False; s.rts = False
s.open()
end = time.time() + secs
buf = b""
while time.time() < end:
    d = s.read(4096)
    if d:
        buf += d
        sys.stdout.write(d.decode("utf-8", "replace")); sys.stdout.flush()
s.close()
