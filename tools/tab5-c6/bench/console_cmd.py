# Open the Tab5's console without resetting it, send a line, read for N seconds.
import sys, time, serial
port = sys.argv[1]; cmd = sys.argv[2]; secs = float(sys.argv[3]) if len(sys.argv) > 3 else 15
s = serial.Serial(); s.port = port; s.baudrate = 115200; s.timeout = 0.2
s.dtr = False; s.rts = False
s.open()
time.sleep(0.3)
s.write((cmd + "\n").encode()); s.flush()
end = time.time() + secs
while time.time() < end:
    d = s.read(4096)
    if d:
        sys.stdout.write(d.decode("utf-8", "replace")); sys.stdout.flush()
s.close()
