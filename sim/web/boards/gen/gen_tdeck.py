# Generates devlab/svg/tdeck.svg  (units = mm)
import os
OUT = os.path.join(os.path.dirname(__file__), '..', 'tdeck.svg')

W, H = 72.0, 120.7
TOP = 8.0                    # body top (SMA stub sits above)
L, R = 0.9, 71.1             # body side edges (main outline)
BOT = H
f = lambda v: ('%.2f' % v).rstrip('0').rstrip('.')

o = []
a = o.append

# ---------- outline helpers ----------
def body_path(l, r, t, b, rt, rbx, rby):
    k = 0.5523
    return ('M{l},{y1} '
            'C{l},{c1} {c2},{t} {x1},{t} '
            'L{x2},{t} '
            'C{c3},{t} {r},{c1} {r},{y1} '
            'L{r},{y2} '
            'C{r},{c4} {c5},{b} {x3},{b} '
            'L{x4},{b} '
            'C{c6},{b} {l},{c4} {l},{y2} Z').format(
        l=f(l), r=f(r), t=f(t), b=f(b),
        y1=f(t + rt), c1=f(t + rt - rt * k), c2=f(l + rt - rt * k), x1=f(l + rt),
        x2=f(r - rt), c3=f(r - rt + rt * k),
        y2=f(b - rby), c4=f(b - rby + rby * k), c5=f(r - rbx + rbx * k), x3=f(r - rbx),
        x4=f(l + rbx), c6=f(l + rbx - rbx * k))

a('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %s %s" data-w-mm="%s" data-h-mm="%s">' % (f(W), f(H), f(W), f(H)))
a('<title>Handheld keyboard device, front</title>')
a('<defs>')
# shell metal
a('<linearGradient id="shellG" x1="0" y1="0" x2="1" y2="0">'
  '<stop offset="0" stop-color="#2e3033"/><stop offset=".05" stop-color="#5d6064"/>'
  '<stop offset=".12" stop-color="#4a4d51"/><stop offset=".5" stop-color="#55585c"/>'
  '<stop offset=".88" stop-color="#4a4d51"/><stop offset=".95" stop-color="#5f6266"/>'
  '<stop offset="1" stop-color="#2c2e31"/></linearGradient>')
a('<linearGradient id="shellV" x1="0" y1="0" x2="0" y2="1">'
  '<stop offset="0" stop-color="#fff" stop-opacity=".22"/><stop offset=".04" stop-color="#fff" stop-opacity=".05"/>'
  '<stop offset=".5" stop-color="#000" stop-opacity="0"/><stop offset=".97" stop-color="#000" stop-opacity=".25"/>'
  '<stop offset="1" stop-color="#000" stop-opacity=".4"/></linearGradient>')
# cheap sand-blasted texture: tiny speckle tile (no filter, renders fast)
a('<pattern id="grain" width="1.1" height="1.1" patternUnits="userSpaceOnUse">'
  '<circle cx=".15" cy=".2" r=".07" fill="#fff" opacity=".10"/><circle cx=".7" cy=".45" r=".06" fill="#000" opacity=".22"/>'
  '<circle cx=".4" cy=".85" r=".07" fill="#fff" opacity=".07"/><circle cx=".95" cy=".95" r=".05" fill="#000" opacity=".18"/>'
  '<circle cx=".55" cy=".1" r=".05" fill="#000" opacity=".15"/><circle cx=".9" cy=".25" r=".06" fill="#fff" opacity=".08"/>'
  '<circle cx=".2" cy=".6" r=".05" fill="#000" opacity=".2"/></pattern>')
# glass
a('<linearGradient id="glassG" x1="0" y1="0" x2="0" y2="1">'
  '<stop offset="0" stop-color="#1a1b1d"/><stop offset=".15" stop-color="#0f1011"/>'
  '<stop offset="1" stop-color="#0b0b0c"/></linearGradient>')
a('<linearGradient id="glassSheen" x1="0" y1="0" x2="1" y2="1">'
  '<stop offset="0" stop-color="#fff" stop-opacity=".10"/><stop offset=".35" stop-color="#fff" stop-opacity=".02"/>'
  '<stop offset=".36" stop-color="#fff" stop-opacity="0"/><stop offset="1" stop-color="#fff" stop-opacity="0"/></linearGradient>')
# chin / keyboard base
a('<linearGradient id="chinG" x1="0" y1="0" x2="0" y2="1">'
  '<stop offset="0" stop-color="#121214"/><stop offset=".6" stop-color="#0c0c0d"/>'
  '<stop offset="1" stop-color="#19191b"/></linearGradient>')
# clear strip
a('<linearGradient id="clearG" x1="0" y1="0" x2="0" y2="1">'
  '<stop offset="0" stop-color="#4a4c50"/><stop offset=".08" stop-color="#2b2c2f"/>'
  '<stop offset=".5" stop-color="#232427"/><stop offset=".92" stop-color="#2a2b2e"/>'
  '<stop offset="1" stop-color="#46484c"/></linearGradient>')
a('<linearGradient id="clearSide" x1="0" y1="0" x2="1" y2="0">'
  '<stop offset="0" stop-color="#55575c" stop-opacity=".9"/><stop offset=".5" stop-color="#2a2b2e" stop-opacity=".9"/>'
  '<stop offset="1" stop-color="#3c3e42" stop-opacity=".9"/></linearGradient>')
# chevron silver
a('<linearGradient id="chev" x1="0" y1="0" x2="0" y2="1">'
  '<stop offset="0" stop-color="#e9eaec"/><stop offset=".5" stop-color="#b9bcc0"/>'
  '<stop offset=".52" stop-color="#9a9ea3"/><stop offset="1" stop-color="#d4d6d9"/></linearGradient>')
# keys
a('<linearGradient id="keyG" x1="0" y1="0" x2="0" y2="1">'
  '<stop offset="0" stop-color="#3b3c3f"/><stop offset=".45" stop-color="#2c2d30"/>'
  '<stop offset="1" stop-color="#1d1e20"/></linearGradient>')
a('<linearGradient id="keyArc" x1="0" y1="0" x2="1" y2="0">'
  '<stop offset="0" stop-color="#000" stop-opacity=".35"/><stop offset=".7" stop-color="#000" stop-opacity=".05"/>'
  '<stop offset="1" stop-color="#fff" stop-opacity=".10"/></linearGradient>')
a('<linearGradient id="barG" x1="0" y1="0" x2="0" y2="1">'
  '<stop offset="0" stop-color="#5a5d61"/><stop offset=".18" stop-color="#f4f5f6"/>'
  '<stop offset=".45" stop-color="#c3c6ca"/><stop offset=".6" stop-color="#aeb2b7"/>'
  '<stop offset=".82" stop-color="#eef0f1"/><stop offset="1" stop-color="#3f4245"/></linearGradient>')
# trackball
a('<radialGradient id="ballG" cx=".38" cy=".33" r=".75">'
  '<stop offset="0" stop-color="#fffbe9"/><stop offset=".12" stop-color="#e6ddbd"/>'
  '<stop offset=".55" stop-color="#b7ad8a"/><stop offset=".9" stop-color="#7d755c"/>'
  '<stop offset="1" stop-color="#5e5845"/></radialGradient>')
a('<radialGradient id="ringG" cx=".5" cy=".5" r=".5">'
  '<stop offset=".78" stop-color="#6d7075"/><stop offset=".86" stop-color="#f3f4f5"/>'
  '<stop offset=".93" stop-color="#9da1a6"/><stop offset="1" stop-color="#2d2f32"/></radialGradient>')
# gold
a('<linearGradient id="goldG" x1="0" y1="0" x2="1" y2="0">'
  '<stop offset="0" stop-color="#8a6a1e"/><stop offset=".3" stop-color="#f3d77a"/>'
  '<stop offset=".5" stop-color="#d6ae45"/><stop offset=".8" stop-color="#fbe9a6"/>'
  '<stop offset="1" stop-color="#7b5c17"/></linearGradient>')
a('<linearGradient id="earG" x1="0" y1="0" x2="0" y2="1">'
  '<stop offset="0" stop-color="#3a3c3f"/><stop offset=".5" stop-color="#8d9095"/>'
  '<stop offset="1" stop-color="#55585c"/></linearGradient>')
a('</defs>')

# ---------- SMA antenna jack (top, left of centre) ----------
sx = 18.1
a('<g id="antenna">')
a('<rect x="%s" y="6.55" width="8.6" height="1.6" rx=".15" fill="url(#goldG)" stroke="#6b5214" stroke-width=".08"/>' % f(sx - 4.3))
a('<rect x="%s" y="1.25" width="5.8" height="5.4" fill="url(#goldG)"/>' % f(sx - 2.9))
for i in range(10):
    y = 1.45 + i * 0.52
    a('<rect x="%s" y="%s" width="5.8" height=".2" fill="#6e5216" opacity=".75"/>' % (f(sx - 2.9), f(y + 0.27)))
    a('<rect x="%s" y="%s" width="5.8" height=".12" fill="#fff4c4" opacity=".55"/>' % (f(sx - 2.9), f(y)))
a('<rect x="%s" y="0" width="4.9" height="1.4" rx=".2" fill="url(#goldG)" stroke="#7a5c18" stroke-width=".06"/>' % f(sx - 2.45))
a('<rect x="%s" y=".18" width="1.2" height="1.0" fill="#fff8d8" opacity=".35"/>' % f(sx - 0.9))
a('</g>')

# ---------- shell (back half, gray metal) ----------
outline = body_path(L, R, TOP, BOT, 12.0, 27.0, 14.0)
# side bulges of the back shell (upper half)
by1, by2 = 20.9, 69.7
a('<path d="M0.9,%s L0.25,%s Q0,%s 0,%s L0,%s Q0,%s 0.25,%s L0.9,%s Z" fill="#3b3d40"/>' % (
    f(by1), f(by1 + 0.6), f(by1 + 0.7), f(by1 + 1.4), f(by2 - 1.4), f(by2 - 0.7), f(by2 - 0.6), f(by2)))
a('<path d="M71.1,%s L71.75,%s Q72,%s 72,%s L72,%s Q72,%s 71.75,%s L71.1,%s Z" fill="#3b3d40"/>' % (
    f(by1), f(by1 + 0.6), f(by1 + 0.7), f(by1 + 1.4), f(by2 - 1.4), f(by2 - 0.7), f(by2 - 0.6), f(by2)))
a('<rect x="0.12" y="%s" width=".35" height="%s" fill="#6a6d71" opacity=".6"/>' % (f(by1 + 1.2), f(by2 - by1 - 2.4)))
a('<rect x="71.53" y="%s" width=".35" height="%s" fill="#6a6d71" opacity=".5"/>' % (f(by1 + 1.2), f(by2 - by1 - 2.4)))
a('<path d="%s" fill="url(#shellG)"/>' % outline)
a('<path d="%s" fill="url(#grain)"/>' % outline)
a('<path d="%s" fill="url(#shellV)"/>' % outline)
# lower half of the outline is the dark keyboard frame
a('<clipPath id="lowerClip"><rect x="0" y="69.2" width="72" height="52"/></clipPath>')
a('<path d="%s" fill="#2a2b2e" clip-path="url(#lowerClip)"/>' % outline)
a('<path d="%s" fill="none" stroke="#000" stroke-opacity=".55" stroke-width=".18"/>' % outline)

# ---------- front glass (upper face) ----------
gl, gr, gt, gb = 2.25, 69.75, 9.9, 69.75
glass = ('M{l},{gb} L{l},{y1} C{l},{c1} {c2},{t} {x1},{t} L{x2},{t} C{c3},{t} {r},{c1} {r},{y1} L{r},{gb} Z').format(
    l=f(gl), r=f(gr), t=f(gt), gb=f(gb), y1=f(gt + 10.4), c1=f(gt + 10.4 - 10.4 * .5523),
    c2=f(gl + 10.4 - 10.4 * .5523), x1=f(gl + 10.4), x2=f(gr - 10.4), c3=f(gr - 10.4 + 10.4 * .5523))
a('<path d="%s" fill="#08090a" stroke="#7d8085" stroke-opacity=".55" stroke-width=".22"/>' % glass)
a('<path d="%s" fill="url(#glassG)" transform="translate(0 .15)"/>' % glass)
# thin bright bevel just inside the metal rim (as in the photos)
a('<path d="%s" fill="none" stroke="#c9ccd1" stroke-opacity=".28" stroke-width=".12" transform="translate(.35 .35) scale(%s %s)"/>' % (
    glass, f((gr - gl - .7) / (gr - gl)), '1'))
# earpiece slot
a('<rect x="28.68" y="13.55" width="15.15" height="1.28" rx=".64" fill="url(#earG)"/>')
a('<rect x="31.85" y="13.8" width="8.6" height=".78" rx=".39" fill="#111214"/>')
a('<rect x="28.9" y="13.62" width="14.7" height=".25" rx=".12" fill="#fff" opacity=".18"/>')

# LCD inactive border + active area
SX, SY, SW, SH = 7.25, 24.1, 57.6, 43.2
a('<rect x="%s" y="%s" width="%s" height="%s" fill="#121316" stroke="#1d1f23" stroke-width=".12"/>' % (
    f(SX - 0.5), f(SY - 0.45), f(SW + 1.0), f(SH + 0.9)))
a('<rect id="screen" x="%s" y="%s" width="%s" height="%s" fill="#000"/>' % (f(SX), f(SY), f(SW), f(SH)))
# glass sheen only on the bezel (top band above the panel)
a('<path d="M%s,%s L%s,%s L%s,%s L%s,%s Z" fill="#fff" opacity=".035"/>' % (
    f(gl + 0.3), f(gt + 3), f(30), f(gt + 0.1), f(44), f(gt + 0.1), f(gl + 0.3), f(SY - 0.6)))

# ---------- keyboard module ----------
# clear plastic frame around keyboard
kf = 'M2.3,69.75 L69.7,69.75 L69.7,107.4 L2.3,107.4 Z'
a('<path d="%s" fill="url(#clearSide)"/>' % kf)
# black chin (bottom area, inside dark rim)
chin = body_path(2.1, 69.9, 40.0, BOT - 1.15, 4.0, 25.6, 12.9)
a('<clipPath id="chinClip"><rect x="0" y="104.6" width="72" height="20"/></clipPath>')
a('<path d="%s" fill="url(#chinG)" clip-path="url(#chinClip)"/>' % chin)
a('<path d="%s" fill="none" stroke="#5a5c60" stroke-opacity=".45" stroke-width=".15" clip-path="url(#chinClip)"/>' % chin)
# clear plastic sides visible next to rows (slightly lighter edges)
a('<rect x="2.3" y="69.75" width=".35" height="37" fill="#8a8d92" opacity=".35"/>')
a('<rect x="69.35" y="69.75" width=".35" height="37" fill="#8a8d92" opacity=".3"/>')

# top clear strip with chevrons and the trackball
a('<rect x="3.0" y="69.75" width="67.3" height="11.9" fill="url(#clearG)"/>')
a('<rect x="3.0" y="69.75" width="67.3" height=".25" fill="#9b9ea3" opacity=".55"/>')
# circuit-style trace lines
tl = '#c9ccd0'
a('<g fill="none" stroke="%s" stroke-width=".16" opacity=".75">' % tl)
a('<path d="M8.6,76.8 L11.2,72.6 L25.6,72.6"/>')
a('<path d="M9.6,76.8 L12.0,73.3 L14.2,73.3"/>')
a('<path d="M26.4,75.3 L29.0,75.3"/>')
a('<path d="M64.4,76.8 L61.8,72.6 L47.6,72.6"/>')
a('<path d="M44.6,75.3 L47.0,75.3"/>')
a('<path d="M66.2,70.6 L66.2,80.9 M67.8,70.6 L67.8,80.9"/>')
a('</g>')
a('<circle cx="67.0" cy="75.5" r="1.05" fill="none" stroke="%s" stroke-width=".12" opacity=".55"/>' % tl)
a('<path d="M10.2,77.8 h1.0 l.4,-.6 h-1.0 z M11.9,77.8 h1.0 l.4,-.6 h-1.0 z M13.6,77.8 h1.0 l.4,-.6 h-1.0 z" fill="#d7d9dc" opacity=".8"/>')

def chevron(x, y, w, h, t, d):
    # d=1 points right, d=-1 points left
    if d == 1:
        pts = [(x, y), (x + t, y), (x + w, y + h / 2), (x + t, y + h), (x, y + h), (x + w - t, y + h / 2)]
    else:
        pts = [(x + w, y), (x + w - t, y), (x, y + h / 2), (x + w - t, y + h), (x + w, y + h), (x + t, y + h / 2)]
    return '<path d="M%s Z" fill="url(#chev)" stroke="#55585c" stroke-width=".06"/>' % ' L'.join('%s,%s' % (f(p[0]), f(p[1])) for p in pts)

for i in range(3):
    a(chevron(15.0 + i * 3.55, 73.15, 3.6, 4.6, 1.75, 1))
    a(chevron(48.5 + i * 3.55, 73.15, 3.6, 4.6, 1.75, -1))

# gold mounting ring + header pads (PCB seen through clear plastic)
a('<circle cx="5.5" cy="71.65" r="1.15" fill="url(#goldG)"/><circle cx="5.5" cy="71.65" r=".62" fill="#151617"/>')
for y in (77.38, 79.25, 81.13, 85.07, 87.08):
    a('<circle cx="4.7" cy="%s" r=".52" fill="url(#goldG)"/><circle cx="4.7" cy="%s" r=".27" fill="#141516"/>' % (f(y), f(y)))

# trackball housing
TBX, TBY = 37.26, 75.55
a('<g fill="none" stroke="#c4c7cb" stroke-width=".13" opacity=".55">')
a('<path d="M%s,70.1 A9.4,9.4 0 0 0 %s,81.2"/>' % (f(TBX - 7.6), f(TBX - 7.6)))
a('<path d="M%s,70.1 A9.4,9.4 0 0 1 %s,81.2"/>' % (f(TBX + 7.6), f(TBX + 7.6)))
a('<path d="M%s,70.6 A7.2,7.2 0 0 0 %s,80.6"/>' % (f(TBX - 5.4), f(TBX - 5.4)))
a('<path d="M%s,70.6 A7.2,7.2 0 0 1 %s,80.6"/>' % (f(TBX + 5.4), f(TBX + 5.4)))
a('</g>')
for dx in (-6.6, -5.3, 5.3, 6.6):
    for dy in (-2.6, 0.0, 2.6):
        a('<circle cx="%s" cy="%s" r=".13" fill="#0a0a0b" opacity=".8"/>' % (f(TBX + dx), f(TBY + dy)))
# octagonal clear plate
oc = 4.65
pts = []
for (px_, py_) in [(-oc, -2.0), (-2.6, -4.05), (2.6, -4.05), (oc, -2.0), (oc, 2.0), (2.6, 4.05), (-2.6, 4.05), (-oc, 2.0)]:
    pts.append('%s,%s' % (f(TBX + px_), f(TBY + py_)))
a('<polygon points="%s" fill="#3a3c40" fill-opacity=".75" stroke="#d9dbde" stroke-width=".22"/>' % ' '.join(pts))
# vertical clear rails each side of the ball
for dx in (-4.0, 3.2):
    a('<rect x="%s" y="70.0" width=".8" height="11.4" fill="#8f9297" fill-opacity=".35" stroke="#d0d2d5" stroke-width=".08" stroke-opacity=".7"/>' % f(TBX + dx))
for (dx, dy) in ((-2.0, -3.6), (2.0, -3.6), (-2.0, 3.6), (2.0, 3.6)):
    a('<circle cx="%s" cy="%s" r=".85" fill="none" stroke="#c9ccd0" stroke-width=".12" opacity=".6"/>' % (f(TBX + dx), f(TBY + dy)))
a('<circle cx="%s" cy="%s" r="3.25" fill="url(#ringG)"/>' % (f(TBX), f(TBY)))
a('<g id="trackball">')
a('<circle cx="%s" cy="%s" r="2.5" fill="url(#ballG)"/>' % (f(TBX), f(TBY)))
a('<ellipse cx="%s" cy="%s" rx=".8" ry=".5" fill="#fff" opacity=".55" transform="rotate(-30 %s %s)"/>' % (
    f(TBX - 0.85), f(TBY - 1.05), f(TBX - 0.85), f(TBY - 1.05)))
a('</g>')

# ---------- keys ----------
GOLD = '#f0962a'
KX0, KP, KW, KH = 6.01, 6.18, 5.78, 6.76
ROWY = [82.83, 90.59, 98.36]
BARY = [81.9, 89.66, 97.43, 105.19]
for by in BARY:
    a('<rect x="5.6" y="%s" width="63.7" height=".92" rx=".3" fill="url(#barG)"/>' % f(by))

rows = [
    [('q', 'Q', '#', 'ๆ', 'ไ'), ('w', 'W', '1', 'ภ', 'ๅท'), ('e', 'E', '2', 'ฎ', 'พถ'), ('r', 'R', '3', 'ธ', 'ะจ'),
     ('t', 'T', '(', '', ''), ('y', 'Y', ')', '', 'ค'), ('u', 'U', '_', 'ณ', 'รด'), ('i', 'I', '-', 'ฯ', 'นจ'),
     ('o', 'O', '+', 'ญ', 'ยข'), ('p', 'P', '@', 'ฐ', 'บช')],
    [('a', 'A', '*', 'ฤ', 'ฟห'), ('s', 'S', '4', 'ฆ', 'กด'), ('d', 'D', '5', 'โ', 'เ'), ('f', 'F', '6', 'ฌ', ''),
     ('g', 'G', '/', '+', ''), ('h', 'H', ':', '~', 'า'), ('j', 'J', ';', 'ษ', 'ส'), ('k', 'K', "'", 'ศ', 'ว'),
     ('l', 'L', '"', 'ซ', 'ง'), ('backspace', None, None, None, None)],
    [('alt', 'alt', None, None, None), ('z', 'Z', '7', 'ฒ', 'ผ'), ('x', 'X', '8', 'ฌ', 'แ'), ('c', 'C', '9', 'ฉ', 'อ'),
     ('v', 'V', '?', 'ซ', ''), ('b', 'B', '!', '', ''), ('n', 'N', ',', '', 'ทม'), ('m', 'M', '.', 'ฒ', 'ใฝ'),
     ('$', '$', None, None, 'ลฟ'), ('enter', None, None, None, None)],
]

def esc(s):
    return s.replace('&', '&amp;').replace('<', '&lt;').replace('"', '&quot;')

def key_shape(x, y, w, h, rx=0.55):
    s = []
    s.append('<rect x="%s" y="%s" width="%s" height="%s" rx="%s" fill="#0a0a0b"/>' % (f(x - .12), f(y - .05), f(w + .24), f(h + .15), f(rx + .1)))
    s.append('<rect x="%s" y="%s" width="%s" height="%s" rx="%s" fill="url(#keyG)"/>' % (f(x), f(y), f(w), f(h), f(rx)))
    # sculpted arc (BlackBerry style): darker crescent on the right part
    cx = x + w * 0.62
    s.append('<path d="M%s,%s Q%s,%s %s,%s L%s,%s Q%s,%s %s,%s Z" fill="url(#keyArc)"/>' % (
        f(cx), f(y + .15), f(cx - 1.2), f(y + h * .55), f(cx - .2), f(y + h - .1),
        f(x + w - .25), f(y + h - .1), f(x + w - .05), f(y + h * .5), f(x + w - .25), f(y + .15)))
    s.append('<path d="M%s,%s Q%s,%s %s,%s" fill="none" stroke="#6b6d71" stroke-width=".09" opacity=".75"/>' % (
        f(cx), f(y + .2), f(cx - 1.2), f(y + h * .55), f(cx - .2), f(y + h - .15)))
    s.append('<rect x="%s" y="%s" width="%s" height=".14" rx=".07" fill="#fff" opacity=".12"/>' % (f(x + .4), f(y + .1), f(w - .8)))
    return s

FONT = 'font-family="Arial, Helvetica, sans-serif"'
for ri, row in enumerate(rows):
    y = ROWY[ri]
    for ci, (dk, letter, sym, th1, th2) in enumerate(row):
        x = KX0 + ci * KP
        g = ['<g class="key" data-key="%s">' % esc(dk)]
        g += key_shape(x, y, KW, KH)
        if dk == 'backspace':
            cx_, cy_ = x + KW / 2, y + KH / 2
            g.append('<path d="M%s,%s L%s,%s L%s,%s L%s,%s L%s,%s Z" fill="none" stroke="#f2f2f2" stroke-width=".32" stroke-linejoin="round"/>' % (
                f(cx_ - 1.75), f(cy_), f(cx_ - .85), f(cy_ - 1.0), f(cx_ + 1.6), f(cy_ - 1.0), f(cx_ + 1.6), f(cy_ + 1.0), f(cx_ - .85), f(cy_ + 1.0)))
            g.append('<path d="M%s,%s l1.0,1.0 M%s,%s l-1.0,1.0" stroke="#f2f2f2" stroke-width=".3" stroke-linecap="round"/>' % (
                f(cx_ - .2), f(cy_ - .5), f(cx_ + .8), f(cy_ - .5)))
        elif dk == 'enter':
            cx_, cy_ = x + KW / 2, y + KH / 2
            g.append('<path d="M%s,%s L%s,%s L%s,%s" fill="none" stroke="#f2f2f2" stroke-width=".38" stroke-linejoin="round" stroke-linecap="round"/>' % (
                f(cx_ + 1.3), f(cy_ - 1.2), f(cx_ + 1.3), f(cy_ + .6), f(cx_ - 1.4), f(cy_ + .6)))
            g.append('<path d="M%s,%s l.9,-.75 v1.5 z" fill="#f2f2f2"/>' % (f(cx_ - 1.75), f(cy_ + .6)))
        elif dk == 'alt':
            g.append('<text x="%s" y="%s" %s font-size="2.05" font-weight="700" fill="#f4f4f4">alt</text>' % (f(x + .75), f(y + 4.3), FONT))
        elif dk == '$':
            # speaker icon (white) + orange $ + orange Thai
            sx_, sy_ = x + 2.9, y + 0.75
            g.append('<path d="M%s,%s h.55 l.85,-.7 v2.3 l-.85,-.7 h-.55 z" fill="none" stroke="#f2f2f2" stroke-width=".16" stroke-linejoin="round"/>' % (f(sx_), f(sy_ + .55)))
            g.append('<path d="M%s,%s a.9,.9 0 0 1 0,1.3 M%s,%s a1.4,1.4 0 0 1 0,2.0" fill="none" stroke="#f2f2f2" stroke-width=".14"/>' % (
                f(sx_ + 1.75), f(sy_ + .3), f(sx_ + 2.15), f(sy_ - .05)))
            g.append('<text x="%s" y="%s" %s font-size="2.0" font-weight="700" fill="%s">$</text>' % (f(x + .6), f(y + 4.3), FONT, GOLD))
            g.append('<text x="%s" y="%s" %s font-size="1.45" fill="%s">%s</text>' % (f(x + .9), f(y + 6.2), FONT, GOLD, th2))
        else:
            g.append('<text x="%s" y="%s" %s font-size="2.15" font-weight="700" fill="#f6f6f6">%s</text>' % (f(x + .5), f(y + 4.45), FONT, esc(letter)))
            if sym:
                g.append('<text x="%s" y="%s" %s font-size="1.75" font-weight="700" fill="#f6f6f6">%s</text>' % (f(x + .55), f(y + 2.25), FONT, esc(sym)))
            if th1:
                g.append('<text x="%s" y="%s" %s font-size="1.45" fill="%s">%s</text>' % (f(x + 3.0), f(y + 3.95), FONT, GOLD, esc(th1)))
            if th2:
                g.append('<text x="%s" y="%s" %s font-size="1.45" fill="%s" text-anchor="middle">%s</text>' % (f(x + 3.55), f(y + 6.05), FONT, GOLD, esc(th2)))
        g.append('</g>')
        o.extend(g)

# ---------- bottom row (shaped keys) ----------
RY = 106.5
def bottom_key(dk, x1, x2, yb_l, yb_r, round_left=False, round_right=False):
    s = ['<g class="key" data-key="%s">' % esc(dk)]
    t = RY
    if round_left:
        d = 'M%s,%s L%s,%s L%s,%s Q%s,%s %s,%s C%s,%s %s,%s %s,%s Z' % (
            f(x2), f(t), f(x2), f(yb_r), f(x1 + 3.0), f(yb_r - .15), f(x1 + .6), f(yb_l - .2), f(x1), f(t + 1.6),
            f(x1), f(t + .5), f(x1 + .5), f(t), f(x1 + 1.6), f(t))
    elif round_right:
        d = 'M%s,%s L%s,%s L%s,%s Q%s,%s %s,%s C%s,%s %s,%s %s,%s Z' % (
            f(x1), f(t), f(x1), f(yb_l), f(x2 - 3.0), f(yb_l - .15), f(x2 - .6), f(yb_r - .2), f(x2), f(t + 1.6),
            f(x2), f(t + .5), f(x2 - .5), f(t), f(x2 - 1.6), f(t))
    else:
        d = 'M%s,%s L%s,%s L%s,%s Q%s,%s %s,%s Z' % (
            f(x1 + .3), f(t), f(x2 - .3), f(t), f(x2), f(yb_r), f((x1 + x2) / 2), f(max(yb_l, yb_r) + .3), f(x1), f(yb_l))
    s.append('<path d="%s" fill="#0a0a0b" transform="translate(0 .12)"/>' % d)
    s.append('<path d="%s" fill="url(#keyG)"/>' % d)
    s.append('<rect x="%s" y="%s" width="%s" height=".14" rx=".07" fill="#fff" opacity=".12"/>' % (f(x1 + .5), f(t + .1), f(x2 - x1 - 1.0)))
    return s

def tail(s):
    s.append('</g>')
    o.extend(s)

def shift_arrow(cx, cy):
    return ('<path d="M%s,%s L%s,%s L%s,%s L%s,%s L%s,%s L%s,%s L%s,%s Z" fill="none" stroke="#f4f4f4" stroke-width=".26" stroke-linejoin="round"/>' % (
        f(cx), f(cy - 1.25), f(cx + 1.25), f(cy), f(cx + .55), f(cy), f(cx + .55), f(cy + 1.2), f(cx - .55), f(cy + 1.2), f(cx - .55), f(cy), f(cx - 1.25), f(cy)))

s = bottom_key('shift', 12.4, 19.3, 109.9, 111.3, round_left=True)
s.append(shift_arrow(16.2, 109.0))
tail(s)
s = bottom_key('0', 19.66, 26.3, 111.8, 111.8)
s.append('<text x="20.35" y="108.55" %s font-size="1.75" font-weight="700" fill="#f6f6f6">0</text>' % FONT)
s.append('<text x="20.35" y="111.0" %s font-size="1.5" fill="%s">ป</text>' % (FONT, GOLD))
# mic icon
s.append('<rect x="22.85" y="108.75" width=".95" height="1.55" rx=".47" fill="#f2f2f2"/>')
s.append('<path d="M22.45,109.75 a.88,.88 0 0 0 1.76,0 M23.33,110.65 v.45 M22.75,111.12 h1.16" fill="none" stroke="#f2f2f2" stroke-width=".17" stroke-linecap="round"/>')
tail(s)
s = bottom_key(' ', 26.8, 47.15, 112.0, 112.0)
s.append('<path d="M33.6,108.9 V110.0 H40.4 V108.9" fill="none" stroke="#f4f4f4" stroke-width=".32"/>')
tail(s)
s = bottom_key('sym', 47.46, 54.16, 111.8, 111.8)
s.append('<text x="50.8" y="109.75" %s font-size="1.95" font-weight="700" fill="#f6f6f6" text-anchor="middle">sym</text>' % FONT)
tail(s)
s = bottom_key('shift', 54.48, 61.55, 111.3, 109.9, round_right=True)
s.append(shift_arrow(58.0, 109.0))
tail(s)

# ---------- USB-C opening (bottom edge, cable leaves downward) ----------
a('<rect id="usb" data-dir="down" x="31.53" y="120.05" width="8.94" height=".65" rx=".3" fill="#0d0e10" stroke="#8d9196" stroke-width=".1"/>')

a('</svg>')
open(OUT, 'w', encoding='utf-8').write('\n'.join(o))
print('wrote', OUT, os.path.getsize(OUT))
