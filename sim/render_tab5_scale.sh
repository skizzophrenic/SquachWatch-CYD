#!/bin/bash
# Every FONTS-page step on the Tab5's screens, for the scale lab artifact.
# Needs squachsim-tab5 (build_tab5_sim.sh). Files are named by screen and the
# six FONTS digits: MENUS TEXT BUTTONS BUBBLES XP_TILES PETS, 0..4, 2 = as built.
cd "$(dirname "$0")"
OUT=out/tab5scale
mkdir -p "$OUT"
export SQUACH_EPOCH=1789396740
S=./squachsim-tab5
n=0
# Home on the fire: Squachy, the paperclip and the owl all talking at frame 120.
for bu in 0 1 2 3 4; do for p in 0 1 2 3 4; do
  SQUACHSIM_T0=29000 SQUACHSIM_CLIPCATCH=1 SQUACHSIM_FONTS="222${bu}2${p}" \
    $S clear "$OUT/home_222${bu}2${p}.png" --bg 6 --pet 3 --frames 120 --size 640x360 >/dev/null 2>&1; n=$((n+1))
done; done
# The counter tiles and the button bar, on the rain, nobody talking.
for b in 0 1 2 3 4; do for x in 0 1 2 3 4; do
  SQUACHSIM_T0=200000 SQUACHSIM_FONTS="22${b}2${x}2" \
    $S clear "$OUT/chrome_22${b}2${x}2.png" --bg 0 --frames 60 --size 640x360 >/dev/null 2>&1; n=$((n+1))
done; done
# Settings: the menu rows.
for m in 0 1 2 3 4; do
  SQUACHSIM_T0=200000 SQUACHSIM_FONTS="${m}22222" \
    $S settings "$OUT/settings_${m}22222.png" --frames 30 --size 640x360 >/dev/null 2>&1; n=$((n+1))
done
# The log: body text.
for tx in 0 1 2 3 4; do
  SQUACHSIM_T0=200000 SQUACHSIM_FONTS="2${tx}2222" \
    $S log "$OUT/log_2${tx}2222.png" --frames 30 --size 640x360 >/dev/null 2>&1; n=$((n+1))
done
# An alert: body text and its buttons.
for tx in 0 1 2 3 4; do for b in 0 1 2 3 4; do
  SQUACHSIM_T0=200000 SQUACHSIM_FONTS="2${tx}${b}222" \
    $S alert "$OUT/alert_2${tx}${b}222.png" --alert 1 --frames 30 --size 640x360 >/dev/null 2>&1; n=$((n+1))
done; done
echo "$n renders in $OUT"
