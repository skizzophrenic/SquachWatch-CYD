"""Generate devlab/svg/cyd28.svg (2.8in yellow ESP32 display board, front, landscape, USB left).

All numbers in millimetres. Board coordinates b(x) are measured from the board's left edge;
the micro-USB shell sticks out OX mm past that edge, so footprint x = OX + board x.
"""
import sys

OX = 0.8            # USB lip past the board's left edge
BW, BH = 86.0, 50.0  # board
W, H = OX + BW, BH
M = OX + 8.4        # display module left end (module 69.2 x 50, centred on the board)
MW = 69.2
AA = (M + 8.55, 3.4, 57.6, 43.2)   # active area: 3.05 from the right (non-FPC) end, centred vertically
GL = (M + 3.05, 1.9, 64.75, 46.2)  # LCD glass


def f(v):
    s = ('%.3f' % v).rstrip('0').rstrip('.')
    return s if s != '-0' else '0'


def bx(x):
    return OX + x


def circ_path(cx, cy, r):
    return 'M%s %sa%s %s 0 1 0 0.001 0Z' % (f(cx), f(cy - r), f(r), f(r))


def rrect_path(x, y, w, h, r):
    return ('M%s %sH%sA%s %s 0 0 1 %s %sV%sA%s %s 0 0 1 %s %sH%sA%s %s 0 0 1 %s %sV%sA%s %s 0 0 1 %s %sZ' % (
        f(x + r), f(y), f(x + w - r), f(r), f(r), f(x + w), f(y + r), f(y + h - r), f(r), f(r), f(x + w - r), f(y + h),
        f(x + r), f(r), f(r), f(x), f(y + h - r), f(y + r), f(r), f(r), f(x + r), f(y)))


HOLES = [(bx(4), 4), (bx(82), 4), (bx(4), 46), (bx(82), 46)]

o = []
a = o.append
a('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %s %s" data-w-mm="%s" data-h-mm="%s">' % (f(W), f(H), f(W), f(H)))
a('''  <!-- 2.8in yellow ESP32 display board (ILI9341 320x240, resistive touch), front side, landscape.
       Units are millimetres. Board 86 x 50 starting at x=%s; the micro-USB mouth pokes out %s mm on the left.
       Panel module 69.2 x 50 centred on the board; active area 57.6 x 43.2 sits 3.05 mm from the
       panel end away from the USB side, centred vertically (panel datasheet). -->''' % (f(OX), f(OX)))
a('''  <defs>
    <linearGradient id="pcb" x1="0" y1="0" x2="0.3" y2="1">
      <stop offset="0" stop-color="#fbc210"/>
      <stop offset="0.55" stop-color="#f6b800"/>
      <stop offset="1" stop-color="#eba900"/>
    </linearGradient>
    <radialGradient id="pcbSheen" cx="0.22" cy="0.08" r="0.95">
      <stop offset="0" stop-color="#fff7cf" stop-opacity="0.38"/>
      <stop offset="0.45" stop-color="#fff7cf" stop-opacity="0.08"/>
      <stop offset="1" stop-color="#fff7cf" stop-opacity="0"/>
    </radialGradient>
    <linearGradient id="frame" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="#fbfaf1"/>
      <stop offset="0.5" stop-color="#e2dcc8"/>
      <stop offset="1" stop-color="#cbc3a8"/>
    </linearGradient>
    <linearGradient id="frameV" x1="0" y1="0" x2="1" y2="0">
      <stop offset="0" stop-color="#f3efe0"/>
      <stop offset="0.6" stop-color="#d3cbb1"/>
      <stop offset="1" stop-color="#b7af94"/>
    </linearGradient>
    <linearGradient id="glass" x1="0" y1="0" x2="1" y2="1">
      <stop offset="0" stop-color="#1d1e19"/>
      <stop offset="0.45" stop-color="#0d0e0b"/>
      <stop offset="1" stop-color="#060606"/>
    </linearGradient>
    <linearGradient id="glassSheen" x1="0" y1="0" x2="0.8" y2="1">
      <stop offset="0" stop-color="#ffffff" stop-opacity="0.13"/>
      <stop offset="0.3" stop-color="#ffffff" stop-opacity="0.03"/>
      <stop offset="1" stop-color="#ffffff" stop-opacity="0"/>
    </linearGradient>
    <radialGradient id="ring" cx="0.4" cy="0.35" r="0.7">
      <stop offset="0" stop-color="#f6f0d8"/>
      <stop offset="0.6" stop-color="#ddd2aa"/>
      <stop offset="1" stop-color="#b8a774"/>
    </radialGradient>
    <linearGradient id="flap" x1="0" y1="0" x2="1" y2="0">
      <stop offset="0" stop-color="#0c0d0a"/>
      <stop offset="0.5" stop-color="#1d1e18"/>
      <stop offset="1" stop-color="#101109"/>
    </linearGradient>
    <linearGradient id="usb" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="#f2f2f0"/>
      <stop offset="0.5" stop-color="#bdbdba"/>
      <stop offset="1" stop-color="#8b8b88"/>
    </linearGradient>
    <linearGradient id="tail" x1="0" y1="0" x2="1" y2="0">
      <stop offset="0" stop-color="#c9b78c"/>
      <stop offset="0.5" stop-color="#f2e9d1"/>
      <stop offset="1" stop-color="#d6c59c"/>
    </linearGradient>
    <linearGradient id="ldr" x1="0" y1="0" x2="1" y2="0">
      <stop offset="0" stop-color="#e7dfc0"/>
      <stop offset="0.5" stop-color="#fbf8ea"/>
      <stop offset="1" stop-color="#e0d6b4"/>
    </linearGradient>
  </defs>''')

# --- micro-USB shell (on the back; its mouth sticks out past the left edge)
uy, uh = 11.6, 7.6
a('  <!-- micro-USB shell, mostly under the board -->')
a('  <rect x="0" y="%s" width="3" height="%s" rx="0.45" fill="url(#usb)" stroke="#6e6e6c" stroke-width="0.12"/>' % (f(uy), f(uh)))
a('  <rect x="0.05" y="%s" width="0.3" height="%s" rx="0.1" fill="#3a3a38"/>' % (f(uy + 0.75), f(uh - 1.5)))
a('  <path d="M0.12 %sH0.9" stroke="#ffffff" stroke-opacity="0.75" stroke-width="0.12"/>' % f(uy + 0.22))

# --- PCB
holes_small = ' '.join(circ_path(x, y, 1.6) for x, y in HOLES)
holes_big = ' '.join(circ_path(x, y, 2.8) for x, y in HOLES)
a('  <!-- PCB: olive mask rim, yellow field, mounting holes cut through -->')
a('  <path fill="#b4aa06" fill-rule="evenodd" d="%s %s"/>' % (rrect_path(OX, 0, BW, BH, 2.6), holes_small))
a('  <path fill="url(#pcb)" fill-rule="evenodd" d="%s %s"/>' % (rrect_path(OX + 0.42, 0.42, BW - 0.84, BH - 0.84, 2.18), holes_big))
a('  <path fill="url(#pcbSheen)" fill-rule="evenodd" d="%s %s"/>' % (rrect_path(OX + 0.42, 0.42, BW - 0.84, BH - 0.84, 2.18), holes_big))
a('  <path d="M%s 46.5V2.7A1.95 1.95 0 0 1 %s 0.68H%s" fill="none" stroke="#fff3b0" stroke-opacity="0.5" stroke-width="0.18"/>' % (f(OX + 0.68), f(OX + 2.63), f(OX + 83.4)))
a('  <path d="M%s 3V47.3A1.95 1.95 0 0 1 %s 49.32H%s" fill="none" stroke="#7a6400" stroke-opacity="0.35" stroke-width="0.18"/>' % (f(OX + 85.32), f(OX + 83.37), f(OX + 3)))

# --- traces under the mask (board coords)
tr = [
    'M%s 9.3H%sV15.2' % (f(bx(80.75)), f(bx(79.6))),
    'M%s 12.5H%sL%s 13.5V44.4L%s 45.4' % (f(bx(80.75)), f(bx(80.1)), f(bx(79.1)), f(bx(80.1))),
    'M%s 10.4V14.4' % f(bx(84.9)),
    'M%s 35.2V41.1L%s 41.9' % (f(bx(84.6)), f(bx(83.8))),
    'M%s 12.5V9.4L%s 8.2' % (f(bx(5.0)), f(bx(6.2))),
    'M%s 17.5V21.3L%s 22.7V35.8' % (f(bx(5.0)), f(bx(6.4))),
    'M%s 24V39.8' % f(bx(3.0)),
]
a('  <g fill="none" stroke="#ffd24a" stroke-opacity="0.5" stroke-width="0.22" stroke-linecap="round" stroke-linejoin="round">')
for t in tr:
    a('    <path d="%s"/>' % t)
a('  </g>')
a('  <g fill="none" stroke="#b98c00" stroke-opacity="0.3" stroke-width="0.2" stroke-linecap="round" stroke-linejoin="round" transform="translate(0.14 0.14)">')
for t in tr[:2] + tr[5:6]:
    a('    <path d="%s"/>' % t)
a('  </g>')

# --- mounting holes
a('  <!-- mounting holes: darker mask edge, tin ring, plated wall -->')
a('  <path fill="#cdbb14" fill-opacity="0.85" fill-rule="evenodd" d="%s"/>' % ' '.join(circ_path(x, y, 2.8) + ' ' + circ_path(x, y, 1.6) for x, y in HOLES))
a('  <path fill="url(#ring)" fill-rule="evenodd" d="%s"/>' % ' '.join(circ_path(x, y, 2.42) + ' ' + circ_path(x, y, 1.6) for x, y in HOLES))
for x, y in HOLES:
    a('  <circle cx="%s" cy="%s" r="1.66" fill="none" stroke="#7a6733" stroke-width="0.18"/>' % (f(x), f(y)))
    a('  <path d="M%s %sA2.4 2.4 0 0 1 %s %s" fill="none" stroke="#ffffff" stroke-opacity="0.55" stroke-width="0.12"/>' % (f(x - 1.85), f(y - 0.85), f(x + 0.8), f(y - 2.3)))
    a('  <path d="M%s %sA2.4 2.4 0 0 1 %s %s" fill="none" stroke="#5e5228" stroke-opacity="0.35" stroke-width="0.12"/>' % (f(x + 1.85), f(y + 0.85), f(x - 0.8), f(y + 2.3)))

# --- USB shell lugs (plated slots) and two pins, left margin
a('  <!-- USB shell solder lugs and pins -->')
for y in (11.5, 18.75):
    x0 = bx(1.3)
    a('  <rect x="%s" y="%s" width="2.3" height="1.3" rx="0.65" fill="#b08a00" fill-opacity="0.8"/>' % (f(x0 - 0.15), f(y - 0.65)))
    a('  <rect x="%s" y="%s" width="2" height="1" rx="0.5" fill="url(#ring)"/>' % (f(x0), f(y - 0.5)))
    a('  <rect x="%s" y="%s" width="1.2" height="0.5" rx="0.25" fill="#4b4115"/>' % (f(x0 + 0.4), f(y - 0.25)))
for y in (12.5, 17.5):
    cx = bx(5.0)
    a('  <circle cx="%s" cy="%s" r="0.72" fill="#b08a00" fill-opacity="0.8"/>' % (f(cx), f(y)))
    a('  <circle cx="%s" cy="%s" r="0.55" fill="url(#ring)"/>' % (f(cx), f(y)))
    a('  <circle cx="%s" cy="%s" r="0.24" fill="#3b3312"/>' % (f(cx), f(y)))
# vias
vias = [(7.1, 1.3), (6.2, 8.2), (6.4, 35.8), (3.0, 24), (3.0, 39.8), (79.6, 15.2), (84.9, 14.4), (83.8, 41.9), (80.1, 45.4)]
a('  <g fill="#dca400" stroke="#bd8c00" stroke-width="0.08">')
a('    ' + ''.join('<circle cx="%s" cy="%s" r="0.22"/>' % (f(bx(x)), f(y)) for x, y in vias))
a('  </g>')

# --- light sensor (LDR), top-right margin
lx = bx(80.95)
a('  <!-- light sensor (LDR) with its silkscreen box -->')
a('  <rect x="%s" y="7.75" width="3.5" height="6.05" rx="0.15" fill="none" stroke="#fbf6e2" stroke-width="0.14" stroke-opacity="0.9"/>' % f(lx - 0.4))
a('  <rect x="%s" y="8.0" width="2.5" height="0.28" rx="0.1" fill="#c7bd96"/>' % f(lx + 0.1))
a('  <rect x="%s" y="8.25" width="2.7" height="1.65" rx="0.12" fill="#fffef6" stroke="#d6cea8" stroke-width="0.08"/>' % f(lx))
a('  <rect x="%s" y="9.9" width="2.7" height="1.55" fill="url(#ldr)"/>' % f(lx))
zz = 'M%s 10.1' % f(lx + 0.3)
xx = lx + 0.3
for i in range(6):
    zz += 'V11.25' if i % 2 == 0 else 'V10.1'
    xx += 0.35
    zz += 'H%s' % f(xx)
a('  <path d="%s" fill="none" stroke="#b46c1c" stroke-width="0.2" stroke-linejoin="round"/>' % zz)
a('  <rect x="%s" y="11.45" width="2.7" height="1.45" rx="0.12" fill="#fbfaf1" stroke="#d6cea8" stroke-width="0.08"/>' % f(lx))
a('  <rect x="%s" y="12.9" width="2.5" height="0.45" rx="0.12" fill="#8c8257"/>' % f(lx + 0.1))

# --- black flex flap on the right edge
fx, fy, fw, fh = bx(79.3), 15.6, 6.0, 18.9
a('  <!-- black flex flap at the right edge, generic handling symbols -->')
a('  <rect x="%s" y="%s" width="%s" height="%s" rx="0.3" fill="url(#flap)"/>' % (f(fx), f(fy), f(fw), f(fh)))
a('  <path d="M%s %sH%s" stroke="#5c5d54" stroke-width="0.12" stroke-opacity="0.8"/>' % (f(fx + 0.15), f(fy + 0.15), f(fx + fw - 0.15)))
a('  <path d="M%s %sV%s" stroke="#3a3b33" stroke-width="0.1"/>' % (f(fx + 0.1), f(fy + 0.3), f(fy + fh - 0.2)))
sx = fx + 1.85
a('  <path d="M%s 24.2L%s 21.9L%s 24.2Z" fill="#efefe6"/>' % (f(sx - 1.25), f(sx), f(sx + 1.25)))
a('  <path d="M%s 23.95C%s 23.3 %s 22.95 %s 22.8" fill="none" stroke="#141510" stroke-width="0.2" stroke-linecap="round"/>' % (f(sx - 0.35), f(sx - 0.25), f(sx + 0.05), f(sx + 0.45)))
a('  <g fill="none" stroke="#efefe6" stroke-width="0.16">')
a('    <circle cx="%s" cy="27.2" r="1.15"/>' % f(sx))
a('    <path d="M%s 28L%s 26.4"/>' % (f(sx - 0.8), f(sx + 0.8)))
a('  </g>')

# --- display module
R = M + MW
a('  <!-- ===== display module (69.2 x 50 metal backlight frame) ===== -->')
a('  <rect x="%s" y="0" width="%s" height="50" fill="url(#frame)"/>' % (f(M), f(MW)))
# left end (panel FPC end): thin frame edge, black tape band, silver lip, gap
a('  <rect x="%s" y="0.75" width="1.6" height="48.5" fill="#22231d"/>' % f(M + 0.2))
a('  <rect x="%s" y="0.75" width="0.9" height="48.5" fill="url(#frameV)"/>' % f(M + 1.8))
# opening around the glass (shadow gap)
a('  <rect x="%s" y="1.2" width="%s" height="47.6" fill="#191a15"/>' % (f(M + 2.7), f(GL[0] + GL[2] - (M + 2.7))))
# glass
gx, gy, gw, gh = GL
a('  <rect x="%s" y="%s" width="%s" height="%s" rx="0.12" fill="url(#glass)"/>' % (f(gx), f(gy), f(gw), f(gh)))
# viewing area / polariser edge
vx, vy, vw, vh = AA[0] - 1.65, AA[1] - 0.8, AA[2] + 3.3, AA[3] + 1.6
a('  <rect x="%s" y="%s" width="%s" height="%s" fill="#0a0a08" stroke="#272821" stroke-width="0.1"/>' % (f(vx), f(vy), f(vw), f(vh)))
a('  <rect x="%s" y="%s" width="%s" height="%s" rx="0.12" fill="url(#glassSheen)"/>' % (f(gx), f(gy), f(gw), f(gh)))
a('  <rect x="%s" y="%s" width="%s" height="%s" rx="0.12" fill="none" stroke="#3b3c34" stroke-width="0.1"/>' % (f(gx), f(gy), f(gw), f(gh)))
# frame edge lines
a('  <rect x="%s" y="0" width="%s" height="50" fill="none" stroke="#857e66" stroke-width="0.14"/>' % (f(M), f(MW)))
a('  <path d="M%s 0.2H%s" stroke="#ffffff" stroke-opacity="0.9" stroke-width="0.2"/>' % (f(M + 0.15), f(R - 0.15)))
a('  <path d="M%s 1.2H%s M%s 48.8H%s" stroke="#9a9378" stroke-width="0.1"/>' % (f(M + 2.7), f(GL[0] + GL[2]), f(M + 2.7), f(GL[0] + GL[2])))
a('  <path d="M%s 49.8H%s" stroke="#6d6752" stroke-opacity="0.6" stroke-width="0.14"/>' % (f(M + 0.15), f(R - 0.15)))
a('  <path d="M%s 0.3V49.7" stroke="#ffffff" stroke-opacity="0.6" stroke-width="0.12"/>' % f(M + 0.1))
# resistive touch film edge
a('  <rect x="%s" y="0.5" width="%s" height="49" fill="none" stroke="#ffffff" stroke-opacity="0.16" stroke-width="0.1"/>' % (f(M + 0.55), f(MW - 1.1)))

# --- active area
a('  <!-- active pixel area: the page lays the live screen on top of this -->')
a('  <rect id="screen" x="%s" y="%s" width="%s" height="%s" fill="#000"/>' % tuple(f(v) for v in AA))

# --- touch tail folded round the left end: three stacked tabs
tx = bx(7.5)
a('  <!-- resistive-touch tail folded round the left end of the panel -->')
a('  <rect x="%s" y="5.4" width="1.1" height="7.4" fill="#5a4a2a" fill-opacity="0.45"/>' % f(tx + 0.35))
for i, (ty, tw) in enumerate(((4.4, 3.0), (7.15, 3.25), (9.9, 3.0))):
    a('  <rect x="%s" y="%s" width="%s" height="2.45" rx="0.2" fill="url(#tail)"/>' % (f(tx + 0.25 * (i == 1) * -1 + 0.2), f(ty), f(tw)))
    a('  <path d="M%s %sH%s" stroke="#ffffff" stroke-opacity="0.65" stroke-width="0.12"/>' % (f(tx + 0.4), f(ty + 0.15), f(tx + tw - 0.1)))
    a('  <path d="M%s %sH%s" stroke="#7e6e48" stroke-opacity="0.7" stroke-width="0.12"/>' % (f(tx + 0.3), f(ty + 2.38), f(tx + tw)))
a('  <rect x="%s" y="4.6" width="0.45" height="9.9" fill="#ece3c8"/>' % f(tx + 2.95))

# --- parts on the back: invisible hit areas
a('  <!-- BOOT / RST buttons and the RGB LED are on the BACK; invisible hit areas at their real spots -->')
a('  <rect id="btn-boot" data-side="back" x="%s" y="48.4" width="3.55" height="1.6" fill="#000" fill-opacity="0"/>' % f(bx(71.8)))
a('  <rect id="btn-rst" data-side="back" x="%s" y="48.4" width="3.6" height="1.6" fill="#000" fill-opacity="0"/>' % f(bx(76.0)))
a('  <circle id="led" data-side="back" cx="%s" cy="11.8" r="2.4" fill="#000" fill-opacity="0"/>' % f(bx(55.4)))

# --- USB opening
a('  <rect id="usb" data-dir="left" x="0" y="%s" width="%s" height="%s" fill="#000" fill-opacity="0"/>' % (f(uy), f(OX), f(uh)))
a('</svg>')

open(sys.argv[1], 'w', encoding='utf-8', newline='\n').write('\n'.join(o) + '\n')
print('W', f(W), 'H', f(H), 'screen', [f(v) for v in AA])
