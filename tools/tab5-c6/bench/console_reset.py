# Reset the Tab5 through the USB-serial-JTAG's DTR/RTS (esptool's hard_reset: RTS alone), then read the boot.
import sys, time, serial
port = sys.argv[1]; secs = float(sys.argv[2]) if len(sys.argv) > 2 else 15
s = serial.Serial(); s.port = port; s.baudrate = 115200; s.timeout = 0.2
s.dtr = False; s.rts = False
s.open()
s.dtr = False; s.rts = True; time.sleep(0.2)
s.rts = False
end = time.time() + secs
while time.time() < end:
    d = s.read(4096)
    if d:
        sys.stdout.write(d.decode("utf-8", "replace")); sys.stdout.flush()
s.close()
