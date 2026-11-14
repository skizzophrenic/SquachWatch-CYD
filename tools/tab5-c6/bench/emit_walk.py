# Walk every SquachEmit category (COM26) while reading the Tab5 (COM15), and
# usage: python emit_walk.py [CATEGORY ...]   (no names = all 17)
#
# write a timeline: each shot and each new detection, so catches can be matched
# to shots. Both ports are opened without touching DTR/RTS.
import serial, threading, time, sys, re, json, os
# The timeline lands beside the repo's ignored bench output.
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..', 'sim', 'out', 'tab5', 'emit_walk.json')
# Ports: confirm both with tools/flash_known.py --check first (COM15 = Tab5
# 30:ed:a0:e3:24:83, COM26 = SquachEmit 88:57:21:2e:e6:e0 on 2026-10-19).
CATS = ["FLOCK", "AXON", "META", "SKIMMER", "RAVEN", "AIRTAG", "DRONE", "ALPR", "CAMERA",
        "SAMSUNG_TAG", "GOOGLE_TAG", "TILE", "RING", "IBEACON", "EVILTWIN", "HACKER", "SQUACHMESH"]
only = [c for c in sys.argv[1:]] or None
events = []
t0 = time.time()
stop = False

def opener(port):
    s = serial.Serial(); s.port = port; s.baudrate = 115200; s.timeout = 0.2; s.dtr = False; s.rts = False
    s.open(); return s

def reader(port, tag):
    s = opener(port); buf = b''; quiet = time.time()
    while not stop:
        try:
            d = s.read(4096)
        except Exception:
            time.sleep(1)
            try: s.close(); s = opener(port)
            except Exception: pass
            continue
        if d:
            quiet = time.time(); buf += d
            while b'\n' in buf:
                line, buf = buf.split(b'\n', 1)
                l = line.decode('utf8', 'replace').strip()
                if tag == 'tab5' and ('[det]' in l or 'Brownout' in l or 'last reset' in l or '[power]' in l):
                    events.append((round(time.time() - t0, 1), tag, l))
                if tag == 'emit' and '[shot]' in l:
                    events.append((round(time.time() - t0, 1), tag, l))
        elif tag == 'tab5' and time.time() - quiet > 20:
            try: s.close(); s = opener(port); quiet = time.time()
            except Exception: pass
    s.close()

threading.Thread(target=reader, args=('COM15', 'tab5'), daemon=True).start()
emit = opener('COM26')
emit_lines = []
def emit_reader():
    buf = b''
    while not stop:
        d = emit.read(4096)
        if d:
            buf += d
            while b'\n' in buf:
                line, buf = buf.split(b'\n', 1)
                l = line.decode('utf8', 'replace').strip()
                emit_lines.append(l)
                if '[shot]' in l: events.append((round(time.time() - t0, 1), 'emit', l))
threading.Thread(target=emit_reader, daemon=True).start()
time.sleep(2)
for i, c in enumerate(CATS):
    if only and c not in only: continue
    events.append((round(time.time() - t0, 1), 'walk', 'WALK ' + c))
    emit.write(('WALK %d\n' % i).encode())
    end = time.time() + 400
    while time.time() < end:
        if any(('WALK %d done' % i) in l for l in emit_lines[-5:]): break
        time.sleep(0.3)
    time.sleep(6)          # let the detector's queue and log catch up
    json.dump(events, open(OUT, 'w'), indent=0)
    print('done', c, flush=True)
time.sleep(5)
stop = True
time.sleep(1)
emit.close()
json.dump(events, open(OUT, 'w'), indent=0)
cur = None; shots = {}; dets = {}
for t, tag, l in events:
    if tag == 'walk': cur = l[5:]; shots[cur] = 0; dets[cur] = []
    elif tag == 'emit' and cur: shots[cur] += 1
    elif tag == 'tab5' and cur and '[det]' in l: dets[cur].append(l.split('[det] ', 1)[-1])
    elif tag == 'tab5' and 'Brownout' in l: print('!! BROWNOUT at', t)
for c in shots:
    print('%-12s shots %3d  caught %d  %s' % (c, shots[c], len(dets[c]), '; '.join(dets[c][:4])))
