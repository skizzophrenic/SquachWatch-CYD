#!/usr/bin/env python3
"""Writes src/meaty_lines.inc: everything M3@ty says, packed.

Edit the lines HERE, then run:
    python tools/gen_meaty_lines.py

Five bits a letter: 0 ends a line, 1-26 are a-z, 27 space, 28 '.', 29 an
apostrophe, 30 "the next letter is a capital", 31 "the next eight bits are
the character itself" (for ! , ? % - and digits). A capital after the start
of a line or a full stop, question or exclamation mark comes free. The lines
sit end to end; mwLine(id) in pet_meaty.inc counts its way to one. Every
line is unpacked again here and checked against its text before writing.

Groups are named MWL_<NAME> (the first line) and MWL_<NAME>_N (how many).
CATCH is in the order of mwCatchLine()'s switch.
"""
import os

GROUPS = [
    ('TIPS', [
        "I'm a meatball. I'm doing a real good job.",
        'What we doing? Are we still doing it?',
        'I rolled here all by myself.',
        'You wanna see a hot dog? I can do a hot dog.',
        "I got a hair in me. It's mine now.",
        "I know what that thing is. It's a thing.",
        'Nobody tells me nothing.',
        "I'm helping. You just can't tell yet.",
        'I counted to three. Then I lost it.',
        "My face don't move when I roll. Watch.",
        'Is it lunch? It feels like lunch.',
        "I'm keepin' a eye out. The big one.",
        'I found a penny. I ate it. Was that okay?',
        "If I was a dog, I'd be a good dog.",
        "I'm round on purpose. It's easier.",
        "I don't need a bed. I am the bed.",
        "They said I'm ground chuck. I don't know Chuck.",
        'I like it here. It smells like you.',
        "I been practicin' my faces. This is one.",
        'Can we get a pool? A little one?',
        "I ain't got a nose. I smell with my whole self.",
        "What's a WiFi? Can I eat it?",
        "I'm watchin' the screen. It's doin' stuff.",
        "I made a friend. It's a crumb.",
        "You ever just roll around? It's real nice.",
        "I'm not lost. I'm right here. I checked.",
        'Do I got a birthday? I want one.',
        "I'm a good listener. I don't hear nothin', but I'm good.",
        "Tell me a secret. I won't eat it.",
        "I'm part of the team now. The meat part.",
    ]),
    ('NIGHT', [
        "It's dark. I'm a little scared of the dark.",
        "I'm gonna be a igloo for bed.",
        'Leave the screen on. Just a little.',
        "Is the moon made of meat? Askin' for me.",
        "Night night. I'll guard the crumbs.",
    ]),
    ('SQUAD', [
        'Company! You want some of my hair?',
        "Hey new friend. I'm made of meat.",
        "Two of us! Now it's a party.",
        'Hi! You smell like a visitor.',
        'Wanna see my igloo? Okay, later.',
    ]),
    ('POKE', [
        "Hey! That's my face.",
        'You poked my meat.',
        'Ow. Do it again.',
        "I'm ticklish in the middle.",
        'That tickles my meat.',
        'Boop. Now you do it to you.',
        "You found my good side. It's all sides.",
        "Hey. I was thinkin'. Now I ain't.",
    ]),
    ('THROWN', [
        'Can I do that again?',
        "I'm okay! Most of me is okay!",
        'I got grass in me again.',
        "Whee! I'm a meteor!",
        'My face stayed on. Good.',
        "Throwing ain't nice. Do it again.",
        'I bounced off my own face.',
        "I'm flatter now. Is that bad?",
        "That's how I go places.",
        'I saw the top of the screen!',
        'Somebody catch me next time.',
        "I'm gonna need a minute. I'm meat.",
    ]),
    ('SPLIT', [
        "Whoa! There's three of me!",
        "We're all gonna get back together.",
        "Which one's me? Oh. All of 'em.",
        'Hold on. Gotta find my other parts.',
        'Ow. One of me landed on a rock.',
        'Everybody roll back! Meet in the middle!',
    ]),
    ('STREAK', [
        "That's %d. I'm countin' 'em.",
        "%d throws! I'm a ball, I can take it.",
        'Number %d. My hair fell out.',
        '%d times. Am I a sport now?',
    ]),
    ('SQTHROWN', [
        "Whoa! You're flyin' like me!",
        'Roll when you land! Roll!',
        'Can I do that next?',
    ]),
    ('SCARED', [
        "I'm a igloo now. Nobody's home.",
        "Go away. I'm a igloo.",
        "You can't see me. I'm ice.",
        "Nobody's home. Leave a message.",
        "It's cold in here, but it's safe.",
        'Knock knock. Nobody. Go away.',
    ]),
    ('DOG', [
        'Look! A hot dog!',
        'Hot dog! Ta-da!',
        "It's a hot dog. You can tell.",
        'Hot dog! Hold the mustard.',
        'Ta-da! You can clap now.',
        'I can do a hot dog every time. Watch.',
    ]),
    ('SHAKE', [
        'Shake it! Shake it!',
        "I'm dancing! Don't look!",
        'This is how you do it.',
        "Look at my moves! Ain't they movin'?",
        "Don't tell nobody I can dance.",
    ]),
    ('DANCE', [
        "Watch this. I been practicin'.",
        "This one's called The Meatball.",
        'I learned this off the TV. The TV was off.',
        'Everybody do it with me! Nobody? Okay.',
        "Five, six, seven, eight! I don't know nine.",
        "Don't clap yet. I ain't done.",
    ]),
    ('CATCH', [
        "I ate the camera. It was lookin' at me funny.",
        'A body camera. Tasted official.',
        'Camera glasses. Crunchy.',
        "It wanted my card. I ain't got a card.",
        "It listens for bangs. I'm quiet. I'm meat.",
        'That one beeped all the way down.',
        'Come down here. I just wanna talk to you.',
        'It reads plates? I been on a plate.',
        'Tasted like batteries. Good batteries.',
        'A doorbell. Ding dong. I ate it.',
        "Somebody's kickin' the WiFi. Not me.",
        "A evil twin. I ain't got no twin. Wait.",
        "I don't know what that was. I ate it to be safe.",
        'A shop beacon. I wanna go to the shop.',
    ]),
]

LETTERS = 'abcdefghijklmnopqrstuvwxyz'
OTHER = " .'"

def encode(line):
    codes = []                  # (value, bits)
    cap = True
    for ch in line:
        lo = ch.lower()
        if ch in LETTERS.upper() and lo in LETTERS:
            if not cap: codes.append((30, 5))
            codes.append((LETTERS.index(lo) + 1, 5))
        elif ch in LETTERS and not cap:
            codes.append((LETTERS.index(ch) + 1, 5))
        elif ch in OTHER:
            codes.append((27 + OTHER.index(ch), 5))
        else:
            assert 0 < ord(ch) < 256, (line, ch)
            codes += [(31, 5), (ord(ch), 8)]
        if ch in '.!?': cap = True
        elif ch != ' ': cap = False
    codes.append((0, 5))
    return codes

def decode(codes):
    out, cap, up, i = [], True, False, 0
    while True:
        c = codes[i][0]; i += 1
        if c == 0: return ''.join(out)
        if c == 30: up = True; continue
        if c == 31: ch = chr(codes[i][0]); i += 1
        else:
            ch = LETTERS[c - 1] if c <= 26 else OTHER[c - 27]
            if (cap or up) and c <= 26: ch = ch.upper()
        up = False
        if ch in '.!?': cap = True
        elif ch != ' ': cap = False
        out.append(ch)

stream, bit, acc = bytearray(), 0, 0
count, ids = 0, []
for name, lines in GROUPS:
    ids.append((name, count, len(lines)))
    for l in lines:
        assert len(l) < 80, l
        codes = encode(l)
        assert decode(codes) == l, (l, decode(codes))
        for v, n in codes:
            acc |= v << bit; bit += n
            while bit >= 8: stream.append(acc & 255); acc >>= 8; bit -= 8
        count += 1
if bit: stream.append(acc & 255)
stream += b'\0\0'                                    # the reader looks one byte ahead

here = os.path.dirname(os.path.abspath(__file__))
dst = os.path.join(here, '..', 'src', 'meaty_lines.inc')
with open(dst, 'w', newline='\n') as f:
    f.write('// SquachWatch-CYD -- what M3@ty says, packed. GENERATED by\n')
    f.write('// tools/gen_meaty_lines.py; edit the lines there, not here.\n')
    f.write('enum : uint8_t {\n')
    for name, first, n in ids:
        f.write('    MWL_%s = %d, MWL_%s_N = %d,\n' % (name, first, name, n))
    f.write('};\n')
    f.write('static const uint8_t MW_TEXT[%d] = {\n' % len(stream))
    for k in range(0, len(stream), 16):
        f.write('    ' + ', '.join('%d' % b for b in stream[k:k + 16]) + ',\n')
    f.write('};\n')
raw = sum(len(l) + 1 for _, ls in GROUPS for l in ls)
print('wrote', os.path.normpath(dst), count, 'lines,', raw, 'bytes as text ->', len(stream), 'packed')
