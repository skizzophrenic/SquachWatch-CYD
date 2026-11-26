#!/usr/bin/env python3
"""Writes src/meaty_shape.inc: Meaty's body as lookup tables.

His outline is a squarish round (a superellipse, flatter where he sits) with a
ring of rounded bumps that turn as he rolls. Worked out here once, so a board
only does whole-number lookups: no square roots, powers or angles per pixel,
which matters on the ESP32-C5 boards with no floating-point unit.

    MW_PU[b][col]   |u| to the power p, x32768: b = 0 above the middle (p 2.3),
                    1 below it (p 3.1, flatter where he sits)
    MW_PV[row]      |v| to that row's power p, x32768
    MW_T[b][k]      where the edge is when a bump reaches MW_BUMP_MIN + k (x200),
                    in the same units: a point is inside when PU + PV < T. It
                    makes the same call, point for point, as the 1,150-byte
                    radius table it replaced -- checked below on every run
    MW_ATAN[k]      the slope (x65536) past which the angle within an eighth of
                    a turn rounds up to k+1; mwAngle() in pet_meaty.inc counts
                    them, the same answer as the 1,150-byte angle table it replaced
    MW_BUMP[i]      the radius the edge reaches at angle i past his rotation

Rows run from v = +1.25 (above his top) down to -1.0 (the floor) in steps of
0.05; columns from |u| = 0 to 1.2. Re-run after changing the shape:
    python tools/gen_meaty_shape.py
"""
import math, os

STEP = 0.05
ROWS = int(round((1.25 + 1.0) / STEP)) + 1        # 46
COLS = int(round(1.25 / STEP))                    # 25
r_rows = []
for ri in range(ROWS):
    v = 1.25 - ri * STEP
    rr = []
    for ci in range(COLS):
        u = (ci + 0.5) * STEP
        p = 3.1 if v < 0 else 2.3
        r = (abs(u) ** p + abs(v) ** p) ** (1 / p)
        rr.append(min(255, int(round(r * 200))))
    r_rows.append(rr)
bump = []
for i in range(256):
    th = i / 256 * 2 * math.pi
    bump.append(int(round((0.925 + 0.06 * math.sqrt(abs(math.cos(6.5 * th)))) * 200)))

# The radius table, split into powers: r < b (both x200, r rounded) is the same
# as |u|^p + |v|^p < ((b - 0.5) / 200)^p. Kept as whole numbers x32768; anything
# past 2.0 is capped, which is far outside him either way.
SC, CAP = 1 << 15, 65535
bmin, bmax = min(bump), max(bump)
pu = [[min(CAP, int(round(((ci + 0.5) * STEP) ** p * SC))) for ci in range(COLS)] for p in (2.3, 3.1)]
pv, rbot = [], []
for ri in range(ROWS):
    v = 1.25 - ri * STEP
    p = 3.1 if v < 0 else 2.3
    pv.append(min(CAP, int(round(abs(v) ** p * SC))))
    rbot.append(1 if v < 0 else 0)
vbot = rbot.index(1)
assert all(rbot[i] == (1 if i >= vbot else 0) for i in range(ROWS))
thr = [[int(round(((b - 0.5) / 200) ** p * SC)) for b in range(bmin, bmax + 1)] for p in (2.3, 3.1)]
for ri in range(ROWS):
    for ci in range(COLS):
        sm = pu[rbot[ri]][ci] + pv[ri]
        for b in range(bmin, bmax + 1):
            assert (r_rows[ri][ci] < b) == (sm < thr[rbot[ri]][b - bmin]), (ri, ci, b)

# His face as little pictures, one character a pixel: '#' line, 'o' white
# (eye whites, the tooth), 'g' the inside of his mouth, '.' not drawn (his
# body shows). Packed two bits a pixel, a uint32 a row, the leftmost pixel in
# the low bits; mwBlit() in pet_meaty.inc draws them in runs.
# Mouths: 13 x 7, the top-left at (mx - 6, my - 3). Order = the MW_CLOSED... enum.
MOUTHS = [
    ['.............', '.............', '....#####....', '...#.#oo##...',
     '..#...oo..#..', '.#....##...#.', '.............'],                       # CLOSED, the frown
    ['.............', '.............', '...#######...', '..####oo###..',
     '..####oo###..', '.###.##..###.', '.............'],                       # SMALL
    ['.............', '...#######...', '.#####oo####.', '.#####oo####.',
     '#############', '#############', '#.##.###.##.#'],                       # WIDE
    ['...#######...', '..###oo####..', '..###oo####..', '..#########..',
     '..##ggggg##..', '..#########..', '...#######...'],                       # EAT
    ['.............', '.............', '...#.....#...', '....#oo.#....',
     '.....###.....', '.............', '.............'],                       # SMILE
    ['.............', '.............', '.....#o#.....', '.....###.....',
     '.....###.....', '.............', '.............'],                       # OH
]
# The face on each of the three little hims (the split): 7 x 5 from (cx - 1, fy - 7).
MINI = [['#######', '.o#.o#.', '.......', '###....', '.o.....']]
# Eyes: 6 x 5 from the eye's (x, y); the pupil and lash are drawn on top.
EYES = [
    ['.####.', '#oooo#', '#oooo#', '#oooo#', '.####.'],                        # open
    ['......', '#....#', '######', '#....#', '.####.'],                        # blink
    ['......', '..##..', '.#..#.', '......', '......'],                        # happy
]

def pack(rows):
    return ['0x%08X' % sum('.#og'.index(ch) << (2 * i) for i, ch in enumerate(r)) for r in rows]

def art(name, pics):
    out = ['static const uint32_t %s[%d][%d] = {' % (name, len(pics), len(pics[0]))]
    for p in pics: out.append('    { ' + ', '.join(pack(p)) + ' },')
    out.append('};')
    return '\n'.join(out)

here = os.path.dirname(os.path.abspath(__file__))
dst = os.path.join(here, '..', 'src', 'meaty_shape.inc')
with open(dst, 'w', newline='\n') as f:
    f.write('// SquachWatch-CYD -- Meaty\'s body as lookup tables. GENERATED by\n')
    f.write('// tools/gen_meaty_shape.py; edit that, not this. See it for what each holds.\n')
    f.write('static const int MW_ROWS = %d, MW_COLS = %d;   // rows from v = +1.25 down, 20 to a unit\n' % (ROWS, COLS))
    f.write('static const uint16_t MW_PU[2][%d] = {\n' % COLS)
    for row in pu: f.write('    { ' + ', '.join('%d' % x for x in row) + ' },\n')
    f.write('};\n')
    f.write('static const uint16_t MW_PV[%d] = {\n' % ROWS)
    for k in range(0, ROWS, 12): f.write('    ' + ', '.join('%d' % x for x in pv[k:k + 12]) + ',\n')
    f.write('};\n')
    f.write('static const int MW_VBOT = %d;   // the first row drawn with the flatter power\n' % vbot)
    f.write('static const uint16_t MW_T[2][%d] = {\n' % len(thr[0]))
    for row in thr: f.write('    { ' + ', '.join('%d' % x for x in row) + ' },\n')
    f.write('};\n')
    atan_t = [math.ceil(math.tan((k + 0.5) * math.pi / 128) * 65536) for k in range(32)]
    f.write('static const uint16_t MW_ATAN[32] = {\n')
    for k in range(0, 32, 8): f.write('    ' + ', '.join('%5d' % x for x in atan_t[k:k + 8]) + ',\n')
    f.write('};\n')
    f.write('static const uint8_t MW_BUMP[256] = {\n')
    for k in range(0, 256, 16): f.write('    ' + ', '.join('%3d' % x for x in bump[k:k + 16]) + ',\n')
    f.write('};\n')
    f.write('static const int MW_BUMP_MIN = %d, MW_BUMP_MAX = %d;   // his smallest and biggest reach\n'
            % (min(bump), max(bump)))
    f.write(art('MW_MOUTH_ART', MOUTHS) + '\n')
    f.write(art('MW_EYE_ART', EYES) + '\n')
    f.write(art('MW_MINI_ART', MINI) + '\n')
print('wrote', os.path.normpath(dst), ROWS, 'x', COLS)
