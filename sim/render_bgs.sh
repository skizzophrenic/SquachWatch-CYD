#!/bin/bash
# Render the home screen on every background, at a given size, into a folder.
#   ./render_bgs.sh out/bgs-640 640x360 [frames]
# For checking that a background fills the whole band on a panel it was not
# drawn on first (the Tab5 renders 640x360).
cd "$(dirname "$0")"
DEST="${1:-out/bgs}"
SIZE="${2:-640x360}"
FRAMES="${3:-60}"
mkdir -p "$DEST"
export SQUACH_EPOCH="${SQUACH_EPOCH:-1789396740}"
for b in 0 1 2 3 4 5 6 7 8 10 11; do
  ./squachsim clear "$DEST/bg$b.png" --bg "$b" --frames "$FRAMES" --size "$SIZE" >/dev/null 2>&1 || echo "FAILED: bg $b"
done
echo "rendered into $DEST at $SIZE, $FRAMES frames"
