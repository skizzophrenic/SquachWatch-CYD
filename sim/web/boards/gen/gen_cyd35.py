# Generates cyd35.svg (Sunton ESP32-3248S035R front, landscape). Units = mm.
import sys

OX = 0.8            # micro-USB shell sticks out 0.8 mm past the left board edge
BW, BH = 101.5, 54.9
W, H = round(BW + OX, 2), BH

def f(v):
    return ('%.3f' % v).rstrip('0').rstrip('.')

def X(v):  # board coords -> footprint coords
    return v + OX

out = []
a = out.append

# ---- board outline: rounded rect, antenna notch on the right, 4 holes ----
r = 2.6
x0, y0, x1, y1 = X(0), 0.0, X(BW), BH
nY0, nY1, nX = 18.7, 36.3, X(94.9)   # antenna notch (open at the right edge)
nr = 0.5
board = (
    f"M{f(x0+r)},{f(y0)} H{f(x1-r)} A{r},{r} 0 0 1 {f(x1)},{f(y0+r)} "
    f"V{f(nY0)} H{f(nX+nr)} A{nr},{nr} 0 0 0 {f(nX)},{f(nY0+nr)} "
    f"V{f(nY1-nr)} A{nr},{nr} 0 0 0 {f(nX+nr)},{f(nY1)} H{f(x1)} "
    f"V{f(y1-r)} A{r},{r} 0 0 1 {f(x1-r)},{f(y1)} H{f(x0+r)} "
    f"A{r},{r} 0 0 1 {f(x0)},{f(y1-r)} V{f(y0+r)} A{r},{r} 0 0 1 {f(x0+r)},{f(y0)} Z"
)
holes = [(X(3.5), 3.5), (X(BW-3.5), 3.5), (X(3.5), BH-3.5), (X(BW-3.5), BH-3.5)]
HR = 1.5
def circ_path(cx, cy, rr):
    return (f"M{f(cx-rr)},{f(cy)} A{rr},{rr} 0 1 0 {f(cx+rr)},{f(cy)} "
            f"A{rr},{rr} 0 1 0 {f(cx-rr)},{f(cy)} Z")
board_d = board + ' ' + ' '.join(circ_path(cx, cy, HR) for cx, cy in holes)

# ---- display module (resistive touch panel on the ST7796 glass) ----
mX0, mX1, mY0, mY1 = X(6.65), X(93.95), 0.05, BH - 0.05
blkX0, blkX1, blkY0, blkY1 = X(14.47), X(92.3), 1.59, 52.53
ledgeX0 = X(10.3)
SX, SY, SW, SH = X(16.65), 2.6, 73.44, 48.96   # active 480x320 area

a(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {f(W)} {f(H)}" '
  f'data-w-mm="{f(W)}" data-h-mm="{f(H)}">')
a('<title>3.5 inch ESP32 touch display board, front</title>')
a('<defs>')
a('<linearGradient id="pcb" x1="0" y1="0" x2="0.35" y2="1">'
  '<stop offset="0" stop-color="#f8e530"/><stop offset="0.45" stop-color="#f0d915"/>'
  '<stop offset="1" stop-color="#e2c40c"/></linearGradient>')
a('<radialGradient id="pcbSheen" cx="0.3" cy="0.15" r="0.9">'
  '<stop offset="0" stop-color="#fff" stop-opacity="0.35"/>'
  '<stop offset="0.5" stop-color="#fff" stop-opacity="0.06"/>'
  '<stop offset="1" stop-color="#fff" stop-opacity="0"/></radialGradient>')
a('<radialGradient id="ring" cx="0.4" cy="0.35" r="0.7">'
  '<stop offset="0" stop-color="#f2dc78"/><stop offset="0.6" stop-color="#d8b443"/>'
  '<stop offset="1" stop-color="#b8922c"/></radialGradient>')
a('<linearGradient id="barrel" x1="0" y1="0" x2="0" y2="1">'
  '<stop offset="0" stop-color="#6b5420"/><stop offset="1" stop-color="#c9a24a"/></linearGradient>')
a('<linearGradient id="frame" x1="0" y1="0" x2="1" y2="1">'
  '<stop offset="0" stop-color="#f1efe6"/><stop offset="0.5" stop-color="#e3e0d2"/>'
  '<stop offset="1" stop-color="#d6d2c2"/></linearGradient>')
a('<linearGradient id="ledge" x1="0" y1="0" x2="1" y2="0">'
  '<stop offset="0" stop-color="#8a8b92"/><stop offset="0.5" stop-color="#7c7d85"/>'
  '<stop offset="1" stop-color="#6d6e75"/></linearGradient>')
a('<linearGradient id="metal" x1="0" y1="0" x2="1" y2="0">'
  '<stop offset="0" stop-color="#3a3c40"/><stop offset="0.5" stop-color="#5a5d62"/>'
  '<stop offset="1" stop-color="#43464b"/></linearGradient>')
a('<linearGradient id="fpc" x1="0" y1="0" x2="1" y2="0">'
  '<stop offset="0" stop-color="#8a5414"/><stop offset="0.5" stop-color="#d0902e"/>'
  '<stop offset="1" stop-color="#9c6418"/></linearGradient>')
a('<linearGradient id="glare" x1="0" y1="0" x2="1" y2="1">'
  '<stop offset="0" stop-color="#fff" stop-opacity="0.13"/>'
  '<stop offset="0.35" stop-color="#fff" stop-opacity="0.025"/>'
  '<stop offset="1" stop-color="#fff" stop-opacity="0"/></linearGradient>')
a('<linearGradient id="usbShell" x1="0" y1="0" x2="0" y2="1">'
  '<stop offset="0" stop-color="#e9ecef"/><stop offset="0.5" stop-color="#b9bec4"/>'
  '<stop offset="1" stop-color="#8d939a"/></linearGradient>')
a('<linearGradient id="ant" x1="0" y1="0" x2="1" y2="0">'
  '<stop offset="0" stop-color="#0b0d0c"/><stop offset="1" stop-color="#1c201e"/></linearGradient>')
a('<radialGradient id="lens" cx="0.45" cy="0.4" r="0.6">'
  '<stop offset="0" stop-color="#fbfbf8"/><stop offset="0.7" stop-color="#d9dbd6"/>'
  '<stop offset="1" stop-color="#b3b6b2"/></radialGradient>')
a('<linearGradient id="pad" x1="0" y1="0" x2="0" y2="1">'
  '<stop offset="0" stop-color="#f3dd8a"/><stop offset="1" stop-color="#c9a040"/></linearGradient>')
a('<linearGradient id="solder" x1="0" y1="0" x2="0" y2="1">'
  '<stop offset="0" stop-color="#f4f4f2"/><stop offset="1" stop-color="#a9adb0"/></linearGradient>')
a('</defs>')

# micro-USB shell sliver poking out on the left (the socket is on the back)
uY0, uY1 = 16.4 - 3.75, 16.4 + 3.75
a(f'<rect x="0" y="{f(uY0)}" width="{f(OX+0.6)}" height="{f(uY1-uY0)}" rx="0.25" '
  f'fill="url(#usbShell)" stroke="#6f757b" stroke-width="0.08"/>')
a(f'<rect x="0.12" y="{f(uY0+0.9)}" width="0.3" height="{f(uY1-uY0-1.8)}" fill="#7d838a" opacity="0.6"/>')

# antenna of the module on the back, seen through the notch
a(f'<rect x="{f(nX+0.2)}" y="{f(nY0+0.15)}" width="{f(x1-nX-0.35)}" height="{f(nY1-nY0-0.3)}" '
  f'rx="0.3" fill="url(#ant)"/>')
# faint meander of the antenna trace (barely visible through the module's own mask)
my0, my1 = nY0 + 1.6, nY1 - 1.6
mx = [nX + 1.4, nX + 4.6]
pts = []
yy = my0
toggle = 0
while yy <= my1 + 1e-6:
    pts.append(f"{f(mx[toggle])},{f(yy)}")
    toggle ^= 1
    pts.append(f"{f(mx[toggle])},{f(yy)}")
    yy += 2.2
a(f'<polyline points="{" ".join(pts)}" fill="none" stroke="#2a302d" stroke-width="0.45" '
  f'stroke-linejoin="round" opacity="0.15"/>')

# the board
a(f'<path d="{board_d}" fill="url(#pcb)" fill-rule="evenodd" stroke="#b99a07" stroke-width="0.12"/>')
a(f'<path d="{board_d}" fill="url(#pcbSheen)" fill-rule="evenodd"/>')
# soft inner edge highlight
a(f'<path d="{board}" fill="none" stroke="#fff6b0" stroke-width="0.25" opacity="0.55" '
  f'transform="translate(0.12 0.12)" clip-path="none"/>')

# mounting holes: plated ring + barrel
for cx, cy in holes:
    a(f'<path d="{circ_path(cx, cy, 2.4)} {circ_path(cx, cy, HR)}" fill="url(#ring)" fill-rule="evenodd" stroke="#b08a20" stroke-width="0.06"/>')
    a(f'<circle cx="{f(cx)}" cy="{f(cy)}" r="{f(HR+0.12)}" fill="none" stroke="url(#barrel)" stroke-width="0.18"/>')

# left strip: through-hole legs of the USB shell + two vias + a few traces
for cy in (12.9, 19.9):
    a(f'<rect x="{f(X(1.1))}" y="{f(cy-0.7)}" width="2.2" height="1.4" rx="0.7" fill="url(#pad)"/>')
    a(f'<rect x="{f(X(1.45))}" y="{f(cy-0.32)}" width="1.5" height="0.64" rx="0.32" fill="url(#solder)"/>')
for cy in (14.0, 18.7):
    a(f'<circle cx="{f(X(4.7))}" cy="{f(cy)}" r="0.42" fill="#e5c84a" stroke="#c7a520" stroke-width="0.1"/>')
    a(f'<circle cx="{f(X(4.7))}" cy="{f(cy)}" r="0.17" fill="#6b5a1a"/>')
for (ax, ay, bx, by) in ((4.7, 14.0, 6.2, 12.5), (4.7, 18.7, 6.2, 20.2)):
    a(f'<path d="M{f(X(ax))},{f(ay)} L{f(X(bx))},{f(by)}" stroke="#e8cd18" stroke-width="0.3" opacity="0.7"/>')

# right strip: RGB LED, light sensor, traces
LX0, LY0, LS = X(95.35), 9.3, 5.0
for i in range(3):
    px = LX0 + 0.55 + i * 1.6
    a(f'<rect x="{f(px)}" y="{f(LY0-0.75)}" width="0.85" height="1.0" rx="0.1" fill="url(#pad)"/>')
    a(f'<rect x="{f(px)}" y="{f(LY0+LS-0.25)}" width="0.85" height="1.0" rx="0.1" fill="url(#pad)"/>')
a(f'<rect id="led" x="{f(LX0)}" y="{f(LY0)}" width="{f(LS)}" height="{f(LS)}" rx="0.25" '
  f'fill="#f4f3ee" stroke="#c9c8c0" stroke-width="0.08"/>')
cx, cy = LX0 + LS / 2, LY0 + LS / 2
a(f'<path d="M{f(LX0+LS-0.9)},{f(LY0)} L{f(LX0+LS)},{f(LY0+0.9)} V{f(LY0)} Z" fill="#d6d4cc"/>')
a(f'<circle cx="{f(cx)}" cy="{f(cy)}" r="1.85" fill="url(#lens)" stroke="#a9aca7" stroke-width="0.1" opacity="0.92"/>')
for ang_dx, ang_dy, col in ((-0.55, -0.35, '#c96a5a'), (0.55, -0.35, '#6a9a6a'), (0.0, 0.55, '#6a7fb0')):
    a(f'<rect x="{f(cx+ang_dx-0.25)}" y="{f(cy+ang_dy-0.25)}" width="0.5" height="0.5" fill="{col}" opacity="0.75"/>')
a(f'<circle cx="{f(cx)}" cy="{f(cy)}" r="0.18" fill="#e8c86a"/>')
a(f'<path d="M{f(cx-1.2)},{f(cy-1.2)} A1.7,1.7 0 0 1 {f(cx+0.6)},{f(cy-1.6)}" fill="none" '
  f'stroke="#fff" stroke-width="0.25" opacity="0.8"/>')
# tiny polarity dots under the LED
for dx in (0.9, 2.1):
    a(f'<circle cx="{f(LX0+dx)}" cy="{f(LY0+LS+1.5)}" r="0.22" fill="#4d4a3c"/>')

# light sensor (white ceramic body, dark window, pads top+bottom)
sX0, sY0, sWd, sHt = X(96.6), 39.7, 3.4, 4.3
a(f'<rect x="{f(sX0-0.35)}" y="{f(sY0-0.35)}" width="{f(sWd+0.7)}" height="{f(sHt+0.7)}" fill="none" '
  f'stroke="#fdfbe8" stroke-width="0.15" opacity="0.8"/>')
a(f'<rect x="{f(sX0)}" y="{f(sY0)}" width="{f(sWd)}" height="1.15" fill="url(#pad)"/>')
a(f'<rect x="{f(sX0)}" y="{f(sY0+sHt-1.15)}" width="{f(sWd)}" height="1.15" fill="url(#pad)"/>')
a(f'<rect x="{f(sX0+0.55)}" y="{f(sY0+0.7)}" width="{f(sWd-1.1)}" height="{f(sHt-1.4)}" rx="0.15" '
  f'fill="#f2f1ec" stroke="#bdbbb3" stroke-width="0.06"/>')
a(f'<rect x="{f(sX0+0.75)}" y="{f(sY0+1.15)}" width="{f(sWd-1.5)}" height="{f(sHt-2.3)}" rx="0.1" fill="#1b1b1d"/>')
a(f'<rect x="{f(sX0+0.85)}" y="{f(sY0+1.25)}" width="0.5" height="{f(sHt-2.5)}" fill="#fff" opacity="0.15"/>')

# module drop shadow onto the PCB (inside the footprint only)
a(f'<rect x="{f(mX0-0.05)}" y="0" width="{f(mX1-mX0+0.35)}" height="{f(BH)}" fill="#000" opacity="0.18"/>')
a(f'<rect x="{f(mX0-0.02)}" y="0" width="{f(mX1-mX0+0.17)}" height="{f(BH)}" fill="#000" opacity="0.15"/>')

# module body: cream frame of the touch film
a(f'<rect x="{f(mX0)}" y="{f(mY0)}" width="{f(mX1-mX0)}" height="{f(mY1-mY0)}" rx="0.35" '
  f'fill="url(#frame)" stroke="#a9a596" stroke-width="0.08"/>')
# left side: flex cable folded round the edge
a(f'<rect x="{f(mX0)}" y="0.6" width="0.8" height="{f(BH-1.2)}" rx="0.2" fill="url(#fpc)"/>')
for i in range(9):
    yy = 6.5 + i * 5.2
    a(f'<rect x="{f(mX0+0.25)}" y="{f(yy)}" width="0.3" height="3.6" fill="#e9b45a" opacity="0.55"/>')
# glass ledge edge (dark metallic) below the touch tail, white frame with slot above
a(f'<rect x="{f(X(7.45))}" y="20.8" width="{f(9.1-7.45)}" height="{f(54.2-20.8)}" fill="url(#metal)"/>')
a(f'<rect x="{f(X(7.45))}" y="20.8" width="{f(9.1-7.45)}" height="0.25" fill="#c7c9cc" opacity="0.6"/>')
a(f'<rect x="{f(X(8.2))}" y="0.9" width="0.8" height="{f(13.6-0.9)}" rx="0.1" fill="#4b4d52"/>')
a(f'<rect x="{f(X(8.3))}" y="1.1" width="0.25" height="12.2" fill="#9a9ca0" opacity="0.6"/>')
for cy in (15.8, 18.5):
    a(f'<rect x="{f(X(8.15))}" y="{f(cy-0.5)}" width="0.9" height="1.0" fill="#3b3c40"/>')
# touch-panel tail traces running down the fold
for i, dy in enumerate((0, 0.5, 1.0, 1.5)):
    a(f'<path d="M{f(X(7.0)+0.1*i)},{f(9.5+dy)} L{f(X(8.0))},{f(14.6+dy)} L{f(X(8.0))},{f(20.5)}" '
      f'fill="none" stroke="#c98a2c" stroke-width="0.12" opacity="0.8"/>')
# grey TFT ledge (driver side), then the black mask
a(f'<rect x="{f(ledgeX0)}" y="{f(blkY0)}" width="{f(blkX0-ledgeX0)}" height="{f(blkY1-blkY0)}" fill="url(#ledge)"/>')
a(f'<rect x="{f(ledgeX0)}" y="{f(blkY0)}" width="0.18" height="{f(blkY1-blkY0)}" fill="#55565c"/>')
a(f'<rect x="{f(blkX0)}" y="{f(blkY0)}" width="{f(blkX1-blkX0)}" height="{f(blkY1-blkY0)}" fill="#07080b"/>')
# thin grey line at the bottom edge of the glass, as in the photos
a(f'<rect x="{f(ledgeX0)}" y="{f(blkY1)}" width="{f(blkX1-ledgeX0)}" height="0.65" fill="#8e8c90"/>')
# inner bevel of the touch film window
a(f'<rect x="{f(ledgeX0)}" y="{f(blkY0)}" width="{f(blkX1-ledgeX0)}" height="{f(blkY1-blkY0+0.65)}" '
  f'fill="none" stroke="#b3afa2" stroke-width="0.15"/>')

# active area (live screen goes here)
a(f'<rect id="screen" x="{f(SX)}" y="{f(SY)}" width="{f(SW)}" height="{f(SH)}" fill="#000"/>')

# glare on the touch film, everything except the active area
glare = (f"M{f(mX0)},{f(mY0)} H{f(mX1)} V{f(mY1)} H{f(mX0)} Z "
         f"M{f(SX)},{f(SY)} V{f(SY+SH)} H{f(SX+SW)} V{f(SY)} Z")
a(f'<path d="{glare}" fill="url(#glare)" fill-rule="evenodd"/>')
# edge highlight along top of the module
a(f'<rect x="{f(mX0+0.3)}" y="0.12" width="{f(mX1-mX0-0.6)}" height="0.15" fill="#fff" opacity="0.7"/>')

# BOOT / RST: tact switches on the BACK, under the panel (invisible hit targets at their true spot)
a(f'<rect id="btn-boot" data-side="back" x="{f(X(78.0))}" y="48.1" width="3.4" height="4.8" fill="#000" fill-opacity="0"/>')
a(f'<rect id="btn-rst" data-side="back" x="{f(X(82.15))}" y="48.1" width="3.4" height="4.8" fill="#000" fill-opacity="0"/>')

# USB opening (socket is on the back, cable leaves to the left)
a(f'<rect id="usb" data-dir="left" x="0" y="{f(uY0)}" width="{f(OX+0.6)}" height="{f(uY1-uY0)}" '
  f'fill="#000" fill-opacity="0"/>')

a('</svg>')
open(sys.argv[1], 'w', encoding='utf-8').write('\n'.join(out) + '\n')
print('W', W, 'H', H, 'screen', SX, SY, SW, SH)
