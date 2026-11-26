#!/usr/bin/env python3
"""Step a dev build's cursor until it sits at X,Y, then optionally press.

  goto.py PORT X Y                 stop on the target (within 6 px)
  goto.py PORT X Y press           ...then a tap (press:MS holds it, e.g. press:900)
  goto.py PORT X Y select          ...then SELECT (a long press on a long-press stop)

Stop numbers shift with what is on screen; positions do not. Read them from the
"[keys] stop i of n at x,y (screen s)" lines a KEY NEXT prints.
Not for list rows: after a scroll the same X,Y holds a different row. Count steps instead.
Only answers on a dev build. See docs/SMALL-BOARDS-DEV.md. Needs pyserial.
"""
import re
import sys
import time

import serial

if len(sys.argv) < 4:
    raise SystemExit(__doc__)
port, tx, ty = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
action = sys.argv[4] if len(sys.argv) > 4 else ""
s = serial.Serial(port, 115200, timeout=0.1)
time.sleep(0.2)
s.reset_input_buffer()


def key(cmd, wait=0.5):
    s.write((cmd + "\n").encode())
    end, buf = time.time() + wait, b""
    while time.time() < end:
        buf += s.read(4096)
    return buf.decode(errors="replace")


for _ in range(40):
    m = re.findall(r"stop (\d+) of (\d+) at (-?\d+),(-?\d+) \(screen (\d+)\)", key("KEY NEXT"))
    if m and abs(int(m[-1][2]) - tx) <= 6 and abs(int(m[-1][3]) - ty) <= 6:
        print("at stop %s of %s (screen %s)" % (m[-1][0], m[-1][1], m[-1][4]))
        break
else:
    raise SystemExit("no stop at %d,%d" % (tx, ty))
if action == "select":
    key("KEY SELECT")
    print("selected")
elif action.startswith("press"):
    ms = action.partition(":")[2] or "120"
    key("KEY PRESS " + ms, int(ms) / 1000 + 0.3)
    print("pressed")
