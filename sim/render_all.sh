#!/bin/bash
# Render every screen the emulator knows, at a given size, into a given folder.
#   ./render_all.sh out/before 480x320
# Used to diff a UI change across all of them at once instead of by eye.
cd "$(dirname "$0")"
DEST="${1:-out/shots}"
SIZE="${2:-480x320}"
mkdir -p "$DEST"
# Pin the clock, or anything showing a time renders differently every run
# and a comparison between two folders is all noise.
export SQUACH_EPOCH="${SQUACH_EPOCH:-1789396740}"
SCREENS="clear log alert settings detfilter power diary hunt rawscan watchalert colorcheck boot phone meshmenu meshwarn bingo lora"
ok=0; bad=0
for s in $SCREENS; do
  if ./squachsim "$s" "$DEST/$s.png" --frames 40 --size "$SIZE" >/dev/null 2>&1; then
    ok=$((ok+1))
  else
    echo "FAILED: $s"; bad=$((bad+1))
  fi
done
echo "$ok rendered into $DEST at $SIZE, $bad failed"
