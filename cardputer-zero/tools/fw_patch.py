#!/usr/bin/env python3
"""Copy one upstream firmware source into the build dir with whole-line
substitutions, so the firmware's own sources are never edited.

usage: fw_patch.py SRC OUT [N:]'old line' 'new line' [...]

Each old line must occur exactly once -- or exactly N times with an "N:"
prefix, every one replaced -- (whole line, exact text) or the build stops:
upstream moved it, and the substitution must be re-checked by hand. A new
text may hold several lines (a #line keeps the numbering in step); the old
one is always a single whole line.
The copy starts with a #line directive so warnings point at the original.
"""
import sys

src, out, pairs = sys.argv[1], sys.argv[2], sys.argv[3:]
if not pairs or len(pairs) % 2:
    sys.exit("fw_patch: need old/new line pairs")
lines = open(src).read().split("\n")
for old, new in zip(pairs[0::2], pairs[1::2]):
    want = 1
    head, sep, rest = old.partition(":")
    if sep and head.isdigit():
        want, old = int(head), rest
    hits = [i for i, l in enumerate(lines) if l == old]
    if len(hits) != want:
        sys.exit(f"fw_patch: {src}: expected {want} line(s) {old!r}, found {len(hits)}")
    for i in hits:
        # A replacement of several lines: a #line after it puts the numbers
        # back in step with the original, so warnings still point right.
        lines[i] = new if "\n" not in new else f'{new}\n#line {i + 2} "{src}"'
with open(out + ".tmp", "w") as f:
    f.write(f'#line 1 "{src}"\n' + "\n".join(lines))
import os
os.replace(out + ".tmp", out)
