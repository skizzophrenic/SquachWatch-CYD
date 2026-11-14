"""Generate cardputer.svg (M5Stack Cardputer ADV front view), units = mm."""
import sys

W, H = 84.0, 54.0
out = []
A = out.append

def f(v):
    s = ('%.3f' % v).rstrip('0').rstrip('.')
    return s if s not in ('-0', '') else '0'

def rrect(x, y, w, h, r, **kw):
    attrs = ''.join(' %s="%s"' % (k.replace('_', '-'), v) for k, v in kw.items())
    return '<rect x="%s" y="%s" width="%s" height="%s" rx="%s"%s/>' % (f(x), f(y), f(w), f(h), f(r), attrs)

def text(x, y, s, size, fill, anchor='middle', weight=700, extra=''):
    s = s.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;')
    return ('<text x="%s" y="%s" font-size="%s" fill="%s" text-anchor="%s" font-weight="%d"%s>%s</text>'
            % (f(x), f(y), f(size), fill, anchor, weight, extra, s))

ORANGE = '#f4622d'
BLUE2 = '#2f68ad'   # secondary legend blue
INK = '#141414'
AA = '#32659c'
GREEN = '#2fcd98'
TILE = '#cdcdcd'
STRIP = '#e9e9e9'

A('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %s %s" data-w-mm="%s" data-h-mm="%s" '
  'font-family="sans-serif">' % (f(W), f(H), f(W), f(H)))
A('<defs>')
A('<linearGradient id="gBody" x1="0" y1="0" x2="0" y2="1">'
  '<stop offset="0" stop-color="#dedede"/><stop offset="0.45" stop-color="#d6d6d6"/>'
  '<stop offset="1" stop-color="#cbcbcb"/></linearGradient>')
A('<linearGradient id="gEdgeX" x1="0" y1="0" x2="1" y2="0">'
  '<stop offset="0" stop-color="#000" stop-opacity="0.05"/><stop offset="0.02" stop-color="#000" stop-opacity="0"/>'
  '<stop offset="0.98" stop-color="#000" stop-opacity="0"/><stop offset="1" stop-color="#000" stop-opacity="0.10"/></linearGradient>')
A('<linearGradient id="gDeck" x1="0" y1="0" x2="0" y2="1">'
  '<stop offset="0" stop-color="#e4e4e4"/><stop offset="0.08" stop-color="#d9d9d9"/>'
  '<stop offset="1" stop-color="#d2d2d2"/></linearGradient>')
A('<linearGradient id="gLedge" x1="0" y1="0" x2="0" y2="1">'
  '<stop offset="0" stop-color="#a9a9a9"/><stop offset="0.5" stop-color="#b9b9b9"/>'
  '<stop offset="1" stop-color="#9f9f9f"/></linearGradient>')
A('<linearGradient id="gChamf" x1="0" y1="0" x2="0" y2="1">'
  '<stop offset="0" stop-color="#cbcbcb"/><stop offset="1" stop-color="#a7a7a7"/></linearGradient>')
A('<linearGradient id="gRecess" x1="0" y1="0" x2="0" y2="1">'
  '<stop offset="0" stop-color="#a9a9a9"/><stop offset="0.25" stop-color="#c4c4c4"/>'
  '<stop offset="1" stop-color="#e6e6e6"/></linearGradient>')
A('<linearGradient id="gGlass" x1="0" y1="0" x2="1" y2="1">'
  '<stop offset="0" stop-color="#1d1f21"/><stop offset="0.5" stop-color="#070707"/>'
  '<stop offset="1" stop-color="#121314"/></linearGradient>')
A('<linearGradient id="gKey" x1="0" y1="0" x2="0" y2="1">'
  '<stop offset="0" stop-color="#3c3c3c"/><stop offset="0.35" stop-color="#232323"/>'
  '<stop offset="1" stop-color="#0e0e0e"/></linearGradient>')
A('<linearGradient id="gKeyHi" x1="0" y1="0" x2="0" y2="1">'
  '<stop offset="0" stop-color="#fff" stop-opacity="0.22"/><stop offset="1" stop-color="#fff" stop-opacity="0"/></linearGradient>')
A('<linearGradient id="gHole" x1="0" y1="0" x2="0" y2="1">'
  '<stop offset="0" stop-color="#8e8e8e"/><stop offset="1" stop-color="#c9c9c9"/></linearGradient>')
A('<linearGradient id="gStrip" x1="0" y1="0" x2="0" y2="1">'
  '<stop offset="0" stop-color="#f1f1f1"/><stop offset="0.5" stop-color="#e9e9e9"/>'
  '<stop offset="1" stop-color="#e2e2e2"/></linearGradient>')
A('<linearGradient id="gPlate" x1="0" y1="0" x2="0" y2="1">'
  '<stop offset="0" stop-color="#c4c4c4"/><stop offset="0.05" stop-color="#d4d4d4"/>'
  '<stop offset="1" stop-color="#d6d6d6"/></linearGradient>')
A('<radialGradient id="gScrew" cx="0.4" cy="0.35" r="0.75">'
  '<stop offset="0" stop-color="#ffffff"/><stop offset="0.55" stop-color="#d4d6d8"/>'
  '<stop offset="1" stop-color="#8c8f92"/></radialGradient>')
A('<linearGradient id="gScrewPit" x1="0" y1="0" x2="0" y2="1">'
  '<stop offset="0" stop-color="#9a9a9a"/><stop offset="1" stop-color="#e2e2e2"/></linearGradient>')
A('<linearGradient id="gUsb" x1="0" y1="0" x2="1" y2="0">'
  '<stop offset="0" stop-color="#8d9091"/><stop offset="0.35" stop-color="#e4e6e7"/>'
  '<stop offset="0.7" stop-color="#c3c6c7"/><stop offset="1" stop-color="#7e8182"/></linearGradient>')
A('<linearGradient id="gSticker" x1="0" y1="0" x2="0" y2="1">'
  '<stop offset="0" stop-color="#f2f2f2"/><stop offset="1" stop-color="#e6e6e6"/></linearGradient>')
A('</defs>')

# ---------------- enclosure ----------------
rt, rb = 1.2, 2.6
body = ('M%s,0 H%s A%s,%s 0 0 1 %s,%s V%s A%s,%s 0 0 1 %s,%s H%s A%s,%s 0 0 1 0,%s V%s A%s,%s 0 0 1 %s,0 Z'
        % (f(rt), f(W - rt), f(rt), f(rt), f(W), f(rt), f(H - rb), f(rb), f(rb), f(W - rb), f(H),
           f(rb), f(rb), f(rb), f(H - rb), f(rt), f(rt), f(rt), f(rt)))
A('<path id="body" d="%s" fill="url(#gBody)"/>' % body)

# top deck (raised frame around screen + module)
A(rrect(0.25, 0.25, W - 0.5, 18.6, 1.0, fill='url(#gDeck)'))
# lower ledge left and right of the screen boss
A('<path d="M0.3,18.55 H17.25 L19.55,20.05 H49.4 L51.7,18.55 H83.7 V21.45 H0.3 Z" fill="url(#gLedge)"/>')
# screen boss chamfer face + its lit top
A('<path d="M17.25,18.55 L19.55,20.05 H49.4 L51.7,18.55 Z" fill="#d7d7d7"/>')
A('<path d="M19.55,20.05 H49.4 L50.2,20.95 H18.75 Z" fill="url(#gChamf)"/>')
A('<rect x="0.3" y="20.95" width="83.4" height="0.5" fill="#9a9a9a" opacity="0.55"/>')
# top highlight line
A('<path d="M1.3,0.3 H82.7" stroke="#f6f6f6" stroke-width="0.25" stroke-linecap="round"/>')

# ---------------- left label sticker ----------------
def _e(s):
    return s.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;')

def htext(x0, x1, y0, y1, s, fill, weight=700, adj='spacingAndGlyphs', cap=0.72, extra=''):
    """Horizontal text whose capital letters fill the box x0..x1 / y0..y1 (y1 = baseline)."""
    return ('<text x="%s" y="%s" font-size="%s" fill="%s" font-weight="%d" textLength="%s" lengthAdjust="%s"%s>%s</text>'
            % (f(x0), f(y1), f((y1 - y0) / cap), fill, weight, f(x1 - x0), adj, extra, _e(s)))

def rtext(x0, x1, y0, y1, s, fill, weight=700, adj='spacingAndGlyphs', cap=0.72, fit=True, extra=''):
    """Text turned 90 deg anticlockwise (reads bottom to top): glyph tops at x0, baseline x1, runs y1 -> y0."""
    tl = ' textLength="%s" lengthAdjust="%s"' % (f(y1 - y0), adj) if fit else ''
    return ('<text transform="translate(%s,%s) rotate(-90)" font-size="%s" fill="%s" font-weight="%d"%s%s>%s</text>'
            % (f(x1), f(y1), f((x1 - x0) / cap), fill, weight, tl, extra, _e(s)))

def utext(x0, x1, y0, y1, s, fill, weight=700, adj='spacingAndGlyphs', cap=0.72, extra=''):
    """Upside-down text: reads right to left, visual top y0 is the baseline."""
    return ('<text transform="translate(%s,%s) rotate(180)" font-size="%s" fill="%s" font-weight="%d" textLength="%s" lengthAdjust="%s"%s>%s</text>'
            % (f(x1), f(y0), f((y1 - y0) / cap), fill, weight, f(x1 - x0), adj, extra, _e(s)))

def ctext(cx, cy, s, fill, capmm=0.62, weight=700):
    """Rotated (bottom-to-top) label centred on a point, e.g. a pin number in a dot."""
    return ('<text transform="translate(%s,%s) rotate(-90)" font-size="%s" fill="%s" font-weight="%d" text-anchor="middle">%s</text>'
            % (f(cx + capmm / 2), f(cy), f(capmm / 0.72), fill, weight, _e(s)))

A(rrect(1.6, 1.45, 17.65, 15.15, 0.8, fill='url(#gSticker)', stroke='#c4c4c4', stroke_width='0.12'))
cb = '#14c2ef'
for (x, y, dx, dy) in [(2.25, 2.5, 1, 1), (18.7, 2.5, -1, 1), (2.25, 11.8, 1, -1), (18.7, 11.8, -1, -1)]:
    A('<path d="M%s,%s V%s M%s,%s H%s" stroke="%s" stroke-width="0.32" fill="none"/>'
      % (f(x + 0.16 * dx), f(y + 1.1 * dy), f(y), f(x), f(y + 0.16 * dy), f(x + 1.1 * dx), cb))
# our wordmark where the product name sits: thin spaced line + heavy line
A(htext(4.75, 16.2, 4.08, 5.63, 'SQUACH', '#1b1b1b', weight=400, adj='spacing'))
A(htext(3.55, 17.25, 7.05, 10.67, 'WATCH', '#111', weight=900))
# microphone icon (marks where the mic is)
A(rrect(8.25, 12.3, 1.15, 2.3, 0.575, fill=ORANGE))
A('<path d="M7.75,13.95 A1.08,1.08 0 0 0 9.9,13.95" stroke="#151515" stroke-width="0.28" fill="none"/>')
A('<path d="M8.825,15.05 V15.5 M8.35,16.0 L8.825,15.4 L9.3,16.0 Z" stroke="#151515" stroke-width="0.26" fill="#151515" stroke-linejoin="round"/>')

# screw recesses (cut the sticker corner like the real one)
for cx in (13.04, 54.5):
    cy = 17.89
    A('<circle cx="%s" cy="%s" r="2.75" fill="url(#gDeck)"/>' % (f(cx), f(cy)))
    A('<circle cx="%s" cy="%s" r="2.05" fill="url(#gScrewPit)"/>' % (f(cx), f(cy)))
    A('<circle cx="%s" cy="%s" r="1.55" fill="url(#gScrew)" stroke="#9fa2a5" stroke-width="0.08"/>' % (f(cx), f(cy)))
    hexpts = ' '.join('%s,%s' % (f(cx + 0.52 * __import__('math').cos(a * 3.14159265 / 3 + 0.5236)),
                                 f(cy + 0.52 * __import__('math').sin(a * 3.14159265 / 3 + 0.5236))) for a in range(6))
    A('<polygon points="%s" fill="#6f7276" stroke="#55585b" stroke-width="0.06"/>' % hexpts)
    A('<circle cx="%s" cy="%s" r="0.22" fill="#4d5053"/>' % (f(cx), f(cy)))

# mic holes "||"
for x in (8.3, 9.25):
    A(rrect(x - 0.22, 18.55, 0.44, 1.85, 0.22, fill='#151515'))

# ---------------- screen ----------------
A(rrect(20.2, 2.4, 26.15, 16.1, 0.9, fill='url(#gRecess)'))
A(rrect(20.42, 2.62, 25.68, 15.6, 0.7, fill='#2a2a2a'))
A(rrect(20.58, 2.82, 25.36, 15.22, 0.55, fill='url(#gGlass)'))
SX, SY, SW, SH = 20.795, 3.33, 24.91, 14.0119
A('<rect id="screen" x="%s" y="%s" width="%s" height="%s" fill="#000"/>' % (f(SX), f(SY), f(SW), f(SH)))
# faint glass glare on the border only (top band)
A('<path d="M20.9,2.95 H45.7" stroke="#ffffff" stroke-opacity="0.10" stroke-width="0.18"/>')

# ---------------- StampS3A module ----------------
mx0, my0, mx1, my1, ch = 57.6, 1.1, 81.8, 19.45, 1.15
A('<path d="M%s,%s H%s L%s,%s V%s L%s,%s H%s L%s,%s V%s Z" fill="#2b2e2d" stroke="#1a1c1b" stroke-width="0.12"/>'
  % (f(mx0 + ch), f(my0), f(mx1 - ch), f(mx1), f(my0 + ch), f(my1 - ch), f(mx1 - ch), f(my1),
     f(mx0 + ch), f(mx0), f(my1 - ch), f(my0 + ch)))
# castellated pads along top and bottom
for i in range(9):
    x = 59.2 + i * 2.54
    if x > 80.6:
        break
    A(rrect(x - 0.38, my0 + 0.08, 0.76, 0.42, 0.15, fill='#4a4d4c'))
    A(rrect(x - 0.38, my1 - 0.5, 0.76, 0.42, 0.15, fill='#4a4d4c'))
# sticker (portrait artwork turned on its side; most print reads bottom to top)
sx0, sy0, sx1, sy1, sc = 58.45, 1.9, 81.55, 18.9, 0.85
A('<path d="M%s,%s H%s L%s,%s V%s L%s,%s H%s L%s,%s V%s Z" fill="#f3f3f1"/>'
  % (f(sx0 + sc), f(sy0), f(sx1 - sc), f(sx1), f(sy0 + sc), f(sy1 - sc), f(sx1 - sc), f(sy1),
     f(sx0 + sc), f(sx0), f(sy1 - sc), f(sy0 + sc)))
INK2 = '#1d1d1d'
DOT = ' stroke="#9a9a9a" stroke-width="0.07" stroke-dasharray="0.14 0.12" fill="none"'
# --- name block (left) ---
A(rrect(58.97, 2.19, 4.86, 13.18, 0.35, **{'fill': 'none', 'stroke': '#a8a8a8', 'stroke-width': '0.07', 'stroke-dasharray': '0.14 0.12'}))
A(rtext(59.14, 60.8, 8.53, 14.95, 'SQUACH', INK2, weight=400))
A(rtext(59.39, 61.7, 2.52, 8.37, 'WATCH', '#e4252f', weight=800))
A(rtext(61.15, 62.15, 8.75, 12.15, 'ESP32', '#d9606a', weight=400))
A(rtext(62.2, 63.65, 8.55, 12.15, 'S3FN8', '#1e8ad6', weight=800))
A(rtext(62.15, 63.4, 2.77, 8.0, '3D-ANT', '#262626', weight=400))
A(rrect(61.0, 12.57, 2.67, 2.47, 0.35, fill='#e8501f'))
A('<rect x="61.72" y="13.22" width="1.23" height="1.17" rx="0.12" fill="none" stroke="#fff" stroke-width="0.16"/>')
A('<path d="M61.5,13.5 h0.22 M61.5,13.8 h0.22 M61.5,14.1 h0.22 M62.95,13.5 h0.22 M62.95,13.8 h0.22 M62.95,14.1 h0.22" stroke="#fff" stroke-width="0.1"/>')
# --- our small wordmark where the maker's logo sits ---
A(rtext(64.33, 65.95, 2.27, 4.9, 'SQUACH', '#1e8ad6', weight=800))
A(rtext(66.05, 66.62, 2.3, 4.05, 'WATCH', INK2, weight=900))
# --- fine print ---
A(rtext(64.33, 64.8, 6.4, 14.95, 'Squachy inside', '#333', weight=600))
A(rtext(65.08, 65.55, 6.4, 14.95, 'Cryptid detector', '#333', weight=600))
# crossed-out bin (side-on, lid to the left)
A('<path d="M68.45,12.85 H69.9 L70.15,14.55 H68.2 Z M68.15,12.6 V14.8 M68.0,13.35 H68.15" stroke="#333" stroke-width="0.1" fill="none" stroke-linejoin="round"/>')
A('<path d="M67.95,12.4 L70.6,14.95 M70.6,12.4 L67.95,14.95" stroke="#333" stroke-width="0.1"/>')
# --- GPIO + ADC header ---
A(utext(64.16, 69.43, 15.28, 15.98, 'GPIO+ADC', '#111', weight=800))
A('<path d="M59.7,16.35 V16.0 H63.95"%s/>' % DOT)
A('<path d="M69.7,15.65 H70.65 L71.0,16.1"%s/>' % DOT)
cyan = [(59.72, 18.08), (61.03, 16.85), (62.27, 18.08), (63.59, 16.85), (64.82, 18.08), (66.14, 16.85),
        (67.37, 18.08), (68.69, 16.85), (69.93, 18.08), (71.08, 17.0)]
for i, (x, y) in enumerate(cyan):
    if i % 2 == 1:
        A(rrect(x - 0.17, y, 0.34, 18.7 - y, 0.12, fill='#d3d3d3'))
for i, (x, y) in enumerate(cyan):
    A('<circle cx="%s" cy="%s" r="0.69" fill="#22c0ee"/>' % (f(x), f(y)))
    A(ctext(x, y, str(i + 1), '#13406b'))
# --- BTN (the module top is the G0 button) ---
A('<g id="btn-g0">')
A(rrect(67.95, 4.9, 2.65, 6.92, 1.32, fill='#0e3b34', stroke='#0a2a25', stroke_width='0.06'))
A('<circle cx="69.27" cy="6.22" r="0.92" fill="#f26b2b"/>')
A(ctext(69.27, 6.22, '0', '#ffe0cc', capmm=0.95, weight=400))
A(rtext(68.2, 69.25, 7.7, 10.84, 'BTN', '#ffffff', weight=800))
A(rtext(69.55, 69.98, 7.5, 10.4, 'DOWNLOAD', '#e6d33a', weight=800))
A(rtext(70.05, 70.45, 8.2, 9.8, 'MODE', '#e6d33a', weight=800))
A('</g>')
# --- top pin row ---
A(rrect(66.8, 2.19, 1.15, 2.14, 0.57, fill='#d8471c'))
A(rtext(67.05, 67.7, 2.45, 4.05, '3V3', '#fff', weight=800))
A(rtext(68.62, 69.02, 2.4, 3.1, 'GP', INK2, weight=800))
A(rtext(69.6, 69.92, 3.55, 4.15, 'U0', INK2, weight=700))
A(rtext(69.95, 70.27, 3.55, 4.15, 'TX', INK2, weight=700))
A(rtext(71.28, 71.64, 2.6, 3.8, 'MMS', INK2, weight=800))
A(rtext(72.32, 72.78, 3.95, 5.4, 'U0RX', INK2, weight=800))
A(rtext(73.58, 74.04, 2.8, 4.05, 'MDI', INK2, weight=800))
A(rtext(76.12, 76.58, 2.6, 3.9, 'MDO', INK2, weight=800))
A(rrect(74.55, 2.1, 1.14, 3.35, 0.57, fill='#f2a3ea'))
A(rtext(74.85, 75.38, 3.45, 4.7, 'RST', '#3a1440', weight=800))
A(rrect(77.1, 2.1, 1.14, 3.65, 0.57, fill='#f2aaa3'))
A(rtext(77.42, 77.92, 3.7, 5.45, 'GPIO', '#3a1a16', weight=800))
A(rrect(78.85, 3.65, 1.25, 1.5, 0.15, fill='#f7ecb0'))
A(rtext(78.9, 79.28, 3.75, 4.98, 'G21', INK2, weight=600))
A('<text transform="translate(79.68,4.98) rotate(-90)" font-size="0.53" font-weight="800" textLength="1.2" lengthAdjust="spacingAndGlyphs"><tspan fill="#e0262c">R</tspan><tspan fill="#24a548">G</tspan><tspan fill="#2a64c9">B</tspan></text>')
A(rtext(79.82, 80.08, 3.6, 5.0, 'STATUS', '#333', weight=600))
A('<path d="M80.0,1.95 L80.75,3.45" stroke="#333" stroke-width="0.07"/>')
A(rrect(80.3, 3.43, 1.16, 2.14, 0.58, fill='#262626'))
A(rtext(80.55, 81.2, 3.7, 5.3, 'GND', '#fff', weight=800))
for x, y, c, n, tc in [(68.77, 4.0, '#33c27f', '46', '#0f2e1d'), (69.93, 2.68, '#5aa2ec', '43', '#fff'),
                       (71.25, 4.74, '#d4d93c', '42', '#3b3b10'), (72.56, 2.85, '#5aa2ec', '44', '#fff'),
                       (73.8, 4.99, '#d4d93c', '41', '#3b3b10'), (75.12, 2.77, '#9a2bbf', 'EN', '#fff'),
                       (76.35, 4.99, '#d4d93c', '40', '#3b3b10'), (77.67, 2.77, '#f58a3c', '0', '#fff'),
                       (79.15, 3.01, '#d4d93c', '39', '#3b3b10')]:
    A('<circle cx="%s" cy="%s" r="0.69" fill="%s"/>' % (f(x), f(y), c))
    A(ctext(x, y, n, tc))
# --- regulatory marks + USB block ---
A(rtext(71.25, 73.3, 6.06, 12.3, 'SQW', '#1a1a1a', weight=800))
for i, s in enumerate(('SquachWatch', 'open source', 'firmware')):
    A(rtext(72.07 + i * 0.4, 72.4 + i * 0.4, 12.2, 14.95, s, '#555', weight=400, extra=' font-style="italic"'))
A(rtext(73.55, 75.5, 10.0, 14.95, 'USB', '#141414', weight=900))
A(rtext(73.72, 74.5, 6.25, 9.7, 'D-:G19', '#222', weight=400))
A(rtext(74.65, 75.55, 6.25, 9.7, 'D+:G20', '#222', weight=400))
A(rrect(75.6, 9.85, 0.92, 5.1, 0.46, fill='#e6177f'))
A(rtext(75.8, 76.33, 10.15, 14.65, 'OTG/SERIAL', '#fff', weight=700))
# --- LCD port pin list (printed upside down) ---
A('<rect x="76.1" y="6.14" width="5.36" height="8.65" rx="0.25"%s/>' % DOT)
A(rrect(80.3, 6.39, 1.07, 5.76, 0.12, fill='#f5a3ad'))
A(rtext(80.55, 81.15, 6.9, 11.7, 'LCD.PORT', '#7a1622', weight=800))
A(rrect(80.3, 12.15, 1.07, 2.47, 0.12, fill='#f9d3d7'))
A(rtext(80.55, 81.1, 12.6, 14.2, 'Ext.', '#7a1622', weight=800))
LCD = [('12-G37/CS', '#3a6fb0'), ('11-3V3', '#d9541e'), ('10-G36/SCK', '#3a6fb0'), ('9-G35/DAT', '#3a6fb0'),
       ('8-G34/RS', '#3a6fb0'), ('7-G33/RST', '#3a6fb0'), ('6-GND', '#262626'), ('5-BL-G38', '#9b3fa0'),
       ('4-G16', '#2a9d5c'), ('3-G17', '#2a9d5c'), ('2-G18', '#2a9d5c'), ('1-5V', '#d9541e')]
LCDW = {'12-G37/CS': 3.1, '11-3V3': 2.0, '10-G36/SCK': 3.55, '9-G35/DAT': 3.2, '8-G34/RS': 2.8, '7-G33/RST': 3.2,
        '6-GND': 1.95, '5-BL-G38': 2.8, '4-G16': 1.75, '3-G17': 1.75, '2-G18': 1.75, '1-5V': 1.4}
for k, (s, c) in enumerate(LCD):
    yc = 6.64 + k * 0.705
    A(utext(80.13 - LCDW[s], 80.13, yc - 0.24, yc + 0.24, s, c, weight=700))
A('<text transform="translate(77.62,13.31) rotate(-57)" font-size="0.62" font-weight="700" fill="#3b74c4" text-anchor="middle" textLength="3.0" lengthAdjust="spacingAndGlyphs">BACKSIDE</text>')
# --- bottom pin row ---
A(rrect(71.74, 16.27, 1.32, 2.47, 0.66, fill='#262626'))
A(rtext(72.1, 72.75, 16.6, 18.4, 'GND', '#fff', weight=800))
A(rrect(74.54, 16.27, 1.15, 2.47, 0.57, fill='#d84315'))
A(rtext(74.85, 75.45, 16.85, 18.15, '5V', '#fff', weight=800))
A(rrect(77.0, 15.28, 1.25, 3.46, 0.62, fill='#f6cf1c'))
A(rtext(77.35, 77.9, 15.6, 16.75, 'SDA', '#2b2300', weight=800))
A(ctext(77.62, 18.05, '13', '#2b2300'))
A(rtext(79.8, 80.28, 15.6, 16.7, 'SCL', INK2, weight=800))
for x in (73.75, 76.3, 78.9):
    A(rtext(x - 0.27, x + 0.27, 16.6, 18.25, 'GPIO', INK2, weight=800))
for x, y, c, n, tc in [(73.8, 15.7, '#2fc07a', '11', '#0f2e1d'), (76.35, 15.86, '#2fc07a', '12', '#0f2e1d'),
                       (78.9, 15.8, '#2fc07a', '14', '#0f2e1d'), (79.98, 18.08, '#c9c9c9', '15', '#333')]:
    A('<circle cx="%s" cy="%s" r="0.69" fill="%s"/>' % (f(x), f(y), c))
    A(ctext(x, y, n, tc))

# ---------------- USB-C (on the right edge, cable leaves to the right) ----------------
A(rrect(81.65, 4.85, 2.3, 10.1, 0.35, fill='#9a9a9a'))
A('<rect id="usb" x="81.8" y="5.05" width="2.2" height="9.7" rx="0.3" data-dir="right" fill="url(#gUsb)"/>')
A(rrect(82.0, 7.75, 0.8, 0.7, 0.12, fill='#2a2c2d'))
A(rrect(82.0, 11.55, 0.8, 0.7, 0.12, fill='#2a2c2d'))
A('<path d="M82.0,5.25 H83.9 M82.0,14.55 H83.9" stroke="#6f7273" stroke-width="0.08"/>')

# ---------------- keyboard ----------------
A(rrect(0.3, 21.45, 83.4, 32.25, 0.5, fill='url(#gPlate)'))
# body edge shading over plate
STRIP_TOP = [21.62, 28.95, 36.27, 43.95]
STRIP_H = 3.72
KEY_TOP = [25.79, 33.2, 40.71, 48.12]
KW, KH = 3.85, 2.37
for r in range(4):
    y = STRIP_TOP[r]
    A(rrect(1.5, y + 0.12, 81.1, STRIP_H, 0.55, fill='#b4b4b4', opacity='0.7'))
    A(rrect(1.45, y, 81.15, STRIP_H, 0.55, fill='url(#gStrip)', stroke='#c6c6c6', stroke_width='0.08'))
    # recessed row shadow above each key row
    A('<rect x="0.6" y="%s" width="82.8" height="0.18" fill="#000" opacity="0.06"/>' % f(y + STRIP_H + 0.12))

def cx(i):
    return 4.50 + 5.762 * i

ROWS = [
    [('`', 'esc'), ('1', '!'), ('2', '@'), ('3', '#'), ('4', '$'), ('5', '%'), ('6', '^'), ('7', '&'),
     ('8', '*'), ('9', '('), ('0', ')'), ('-', 'us'), ('=', '+'), ('backspace', 'del')],
    [('tab', 'tab')] + [(c, None) for c in 'qwertyuiop'] + [('[', '{'), (']', '}'), ('\\', '|')],
    [('fn', 'fn'), ('shift', 'Aa')] + [(c, None) for c in 'asdfghjkl'] + [(';', ':'), ("'", '"'), ('enter', 'ok')],
    [('control', 'ctrl'), ('opt', 'opt'), ('alt', 'alt')] + [(c, None) for c in 'zxcvbnm'] +
    [(',', '<'), ('.', '>'), ('/', '?'), (' ', 'space')],
]

def keycap(c, ky):
    s = []
    s.append(rrect(c - KW / 2 - 0.16, ky - 0.12, KW + 0.32, KH + 0.3, 1.15, fill='url(#gHole)'))
    s.append(rrect(c - KW / 2, ky, KW, KH, 1.0, fill='url(#gKey)'))
    s.append(rrect(c - KW / 2 + 0.35, ky + 0.12, KW - 0.7, 0.75, 0.4, fill='url(#gKeyHi)'))
    return s

def legend(key, sec, c, st):
    """Return svg for the printed legend of one key; st = strip top."""
    s = []
    base = st + 2.62          # main legend baseline
    sb = st + 1.78            # secondary legend baseline
    big = 2.3
    small = 1.75
    if key == '`':
        s.append('<polygon points="2.48,%s 7.24,%s 7.15,%s 2.48,%s" fill="%s"/>'
                 % (f(st + 0.05), f(st + 0.05), f(st + 1.12), f(st + 1.98), ORANGE))
        s.append(text(4.35, st + 1.18, 'esc', 1.45, '#fff', extra=' letter-spacing="0.03"'))
        s.append('<path d="M4.3,%s L5.3,%s" stroke="%s" stroke-width="0.36" stroke-linecap="round"/>'
                 % (f(st + 2.5), f(st + 2.98), INK))
        s.append('<path d="M5.35,%s q0.3,-0.45 0.6,-0.12 t0.6,-0.12" stroke="%s" stroke-width="0.22" fill="none" stroke-linecap="round"/>'
                 % (f(st + 2.05), BLUE2))
    elif key == 'backspace':
        x0, x1, y0, y1 = 77.05, 81.8, st + 0.05, st + STRIP_H - 0.12
        s.append('<rect x="%s" y="%s" width="%s" height="%s" fill="#0a0a0a"/>' % (f(x0), f(y0), f(x1 - x0), f(y1 - y0)))
        s.append('<polygon points="%s,%s %s,%s %s,%s %s,%s" fill="%s"/>'
                 % (f(x0), f(y0), f(x1), f(y0), f(x1), f(st + 2.5), f(x0), f(st + 0.6), ORANGE))
        s.append(text(80.35, st + 1.27, 'del', 1.5, '#fff'))
        s.append('<path d="M77.35,%s L78.45,%s V%s H79.65 V%s H78.45 V%s Z" fill="#fff"/>'
                 % (f(st + 2.35), f(st + 1.6), f(st + 1.95), f(st + 2.8), f(st + 3.12)))
    elif key == 'enter':
        x0, x1, y0, y1 = 77.05, 81.75, st + 0.03, st + STRIP_H - 0.12
        s.append('<rect x="%s" y="%s" width="%s" height="%s" fill="#050505"/>' % (f(x0), f(y0), f(x1 - x0), f(y1 - y0)))
        s.append(text(77.45, st + 1.33, 'ok', 1.35, '#fff', anchor='start'))
        # enter arrow: vertical bar at right, shaft to the left, head pointing left
        s.append('<path d="M80.55,%s H81.1 V%s H79.05 V%s L78.05,%s L79.05,%s V%s H80.55 Z" fill="#fff"/>'
                 % (f(st + 1.15), f(st + 2.85), f(st + 3.3), f(st + 2.5), f(st + 1.7), f(st + 2.15)))
    elif key in ('tab', 'control', 'alt', 'fn', 'shift', 'opt'):
        spans = {'tab': (2.5, 7.15), 'fn': (2.5, 7.2), 'shift': (8.2, 12.95),
                 'control': (2.3, 6.9), 'opt': (7.1, 12.7), 'alt': (13.8, 18.45)}
        fills = {'tab': TILE, 'control': TILE, 'alt': TILE, 'fn': ORANGE, 'shift': AA, 'opt': GREEN}
        inks = {'tab': '#222', 'control': '#222', 'alt': '#222', 'fn': '#fff', 'shift': '#fff', 'opt': '#10241c'}
        x0, x1 = spans[key]
        s.append('<rect x="%s" y="%s" width="%s" height="%s" fill="%s"/>'
                 % (f(x0), f(st + 0.04), f(x1 - x0), f(STRIP_H - 0.14), fills[key]))
        s.append(text((x0 + x1) / 2, st + 2.38, sec, 1.62 if key not in ('fn', 'shift') else 1.8, inks[key],
                      extra=' letter-spacing="0.04"'))
    elif key == ' ':
        s.append('<path d="M77.9,%s V%s H81.05 V%s" stroke="%s" stroke-width="0.34" fill="none"/>'
                 % (f(st + 2.05), f(st + 2.85), f(st + 2.05), INK))
    elif key == ';':
        s.append('<polygon points="%s,%s %s,%s %s,%s" fill="%s"/>'
                 % (f(c - 1.9), f(st + 3.33), f(c + 1.95), f(st + 3.33), f(c + 0.05), f(st + 1.42), ORANGE))
        s.append(text(c + 0.08, st + 2.82, ';', 2.2, INK))
        s.append(text(c + 1.75, st + 1.85, ':', 2.0, BLUE2))
    elif key == "'":
        s.append(text(c - 0.2, st + 3.75, '‘', 3.4, INK))
        s.append(text(c + 2.0, st + 2.75, '“', 3.0, BLUE2))
    elif key == ',':
        s.append('<polygon points="%s,%s %s,%s %s,%s" fill="%s"/>'
                 % (f(60.7), f(st + 2.0), f(62.85), f(st + 0.85), f(62.85), f(st + 2.95), ORANGE))
        s.append(text(62.1, st + 2.6, ',', 2.6, INK))
        s.append(text(63.7, st + 1.6, '<', 1.6, BLUE2))
    elif key == '.':
        s.append('<polygon points="%s,%s %s,%s %s,%s" fill="%s"/>'
                 % (f(65.97), f(st + 1.4), f(69.86), f(st + 1.4), f(67.92), f(st + 3.35), ORANGE))
        s.append('<circle cx="67.95" cy="%s" r="0.36" fill="%s"/>' % (f(st + 2.55), INK))
        s.append(text(69.45, st + 1.65, '>', 1.6, BLUE2))
    elif key == '/':
        s.append('<polygon points="%s,%s %s,%s %s,%s" fill="%s"/>'
                 % (f(73.06), f(st + 0.65), f(75.2), f(st + 1.8), f(73.06), f(st + 2.95), ORANGE))
        s.append('<path d="M73.15,%s L74.0,%s" stroke="%s" stroke-width="0.36" stroke-linecap="butt"/>'
                 % (f(st + 3.25), f(st + 1.15), INK))
        s.append(text(75.65, st + 1.95, '?', 1.6, BLUE2))
    elif key == '-':
        s.append('<rect x="%s" y="%s" width="1.0" height="0.3" fill="%s"/>' % (f(c - 0.35), f(st + 2.68), INK))
        s.append('<rect x="%s" y="%s" width="0.6" height="0.22" fill="%s"/>' % (f(c + 1.25), f(st + 0.95), BLUE2))
    elif sec is None:
        s.append(text(c + 0.2, base, key.upper(), big, INK))
    else:
        s.append(text(c + 0.2, base, key, big, INK))
        s.append(text(c + 0.95, sb, sec, small, BLUE2, anchor='start'))
    return s

nkeys = 0
for r, row in enumerate(ROWS):
    st = STRIP_TOP[r]
    for i, (key, sec) in enumerate(row):
        c = cx(i)
        dk = key.replace('&', '&amp;').replace('"', '&quot;').replace('<', '&lt;')
        A('<g class="key" data-key="%s">' % dk)
        A('<rect x="%s" y="%s" width="5.7" height="%s" fill="#000" fill-opacity="0"/>'
          % (f(c - 2.85), f(st), f(KEY_TOP[r] + KH - st)))
        out.extend(legend(key, sec, c, st))
        out.extend(keycap(c, KEY_TOP[r]))
        A('</g>')
        nkeys += 1

# side-wall shading over everything at the outer edge
A('<path d="%s" fill="url(#gEdgeX)" pointer-events="none"/>' % body)
A('<path d="%s" fill="none" stroke="#a9a9a9" stroke-width="0.18" pointer-events="none"/>' % body)
A('</svg>')

open(sys.argv[1], 'w', encoding='utf-8').write('\n'.join(out))
print('keys', nkeys)
