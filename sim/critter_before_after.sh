#!/bin/bash
# The fire (owl, wolf) and snowfall (riders, yeti) backgrounds at several
# moments, with theme.cpp as committed (before) and as it is now (after), at
# the small boards' sizes, where nothing should move. Run from the repo's sim/.
set -e
cd "$(dirname "$0")"
export SQUACH_EPOCH="${SQUACH_EPOCH:-1789396740}"
shoot() {
  mkdir -p "out/crit/$1"
  for s in 320x240 240x320; do
    for b in 6 7; do
      for f in 150 300 450 600 900 1200; do
        ./squachsim clear "out/crit/$1/bg${b}_${s}_${f}.png" --bg "$b" --frames "$f" --size "$s" >/dev/null 2>&1
      done
    done
  done
}
git stash push -q -- ../src/theme.cpp
trap 'git stash pop -q || true' EXIT
make -s squachsim >/dev/null 2>&1
shoot before
git stash pop -q
trap - EXIT
make -s squachsim >/dev/null 2>&1
shoot after
same=0; diff=0
for f in out/crit/before/*.png; do
  if cmp -s "$f" "out/crit/after/$(basename "$f")"; then same=$((same+1)); else diff=$((diff+1)); echo "DIFFERS: $(basename "$f")"; fi
done
echo "$same identical, $diff differ"
