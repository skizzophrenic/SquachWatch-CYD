#!/bin/bash
# Render every background at the shipping sizes with theme.cpp as committed
# (before) and as it is now (after), then compare. Run from the repo's sim/.
set -e
cd "$(dirname "$0")"
git stash push -q -- ../src/theme.cpp
trap 'git stash pop -q || true' EXIT
make -s squachsim >/dev/null 2>&1
for s in 320x240 240x320 480x320 400x240; do bash render_bgs.sh "out/bga/before-$s" "$s" 120 >/dev/null; done
git stash pop -q
trap - EXIT
make -s squachsim >/dev/null 2>&1
for s in 320x240 240x320 480x320 400x240; do bash render_bgs.sh "out/bga/after-$s" "$s" 120 >/dev/null; done
for s in 320x240 240x320 480x320 400x240; do
  same=0; diff=0
  for f in out/bga/before-$s/*.png; do
    if cmp -s "$f" "out/bga/after-$s/$(basename "$f")"; then same=$((same+1)); else diff=$((diff+1)); echo "DIFFERS: $s $(basename "$f")"; fi
  done
  echo "$s: $same identical, $diff differ"
done
