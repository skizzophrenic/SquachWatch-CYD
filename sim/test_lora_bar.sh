#!/bin/bash
# What a press on the LORA screen's button bar does, pinned.
#
#   ./test_lora_bar.sh
#
# THE BUG THIS EXISTS FOR, in the owner's words from the bench: "frisch
# gebootet: ich druecke LORA. ich sehe eine frame-liste. ich klicke auf VIEWS.
# ich bekomme einen einzelnen frame angezeigt. erst wenn ich nochmal auf VIEWS
# klicke, kann ich die views auswaehlen."
#
# The bar's buttons are drawn at y 214..234 of a 400x240 canvas and the body
# stops at 210, so eleven rows of screen -- 211..213 above the buttons and
# 235..239 below them, about 2 mm of a 7.6 mm target's surround -- belonged to
# nobody. uiLoraTap let them fall through to the frame list's row arithmetic,
# which has no bottom, and they opened row 9 and row 10: a single frame. A
# second press landing inside the twenty then opened the picker, which is
# exactly the two-press sequence reported.
#
# So the assertions are about the EDGES of the button, not its middle: the
# middle never was the problem. Nothing here needs the board -- that is the
# other half of the point, since this screen shipped with no way to press it
# except by flashing one.
cd "$(dirname "$0")"

SIZE=400x240      # the CrowPanel 7's logical canvas (SQW_LOGICAL_W/H)
VIEWS_X=200       # the centre of bar slot 1: margin 4 + 128 + gap 4 .. + 128
BODY_BOTTOM=210   # bodyBottom(): Theme::computeButtonBar's y (240-20-6) minus 4
BAR_TOP=214
BAR_BOT=234
LAST_ROW_Y=200    # inside the last frame row drawn (row 8 spans 188..207)

pass=0; fail=0

# press <view-to-open-on> <x> <y> -> prints the LoraView the press left showing
press() {
  ./squachsim lora /dev/null --size "$SIZE" --frames 3 --loraview "$1" \
      --tap "2:$2:$3" 2>&1 >/dev/null |
    sed -n 's/.*\[lora\] tap [0-9-]*,[0-9-]* -> \([A-Z]*\), view \([0-9]*\)/\1 \2/p'
}

# want <description> <expected "RESULT view"> <open-on> <x> <y>
want() {
  local got; got="$(press "$3" "$4" "$5")"
  if [ "$got" = "$2" ]; then
    pass=$((pass+1))
  else
    fail=$((fail+1)); echo "  FAIL: $1 -- wanted '$2', got '$got'"
  fi
}

echo "the [ VIEWS ] button, edge to edge (LoraView 0 = LIST, 9 = PICK)"
# Every row from the first one under the body to the last one on the screen.
# 211..213 and 235..239 are the rows that used to open a frame.
for y in 211 212 213 $BAR_TOP 220 224 230 $BAR_BOT 235 236 238 239; do
  want "a press at y=$y opens the picker" "HANDLED 9" 0 $VIEWS_X $y
done

echo "the frame list keeps its rows, and stops where it stops drawing"
want "a press on a drawn row opens that frame" "HANDLED 1" 0 $VIEWS_X $LAST_ROW_Y
# drawList draws a row only while a whole one fits, so 208..210 is under the
# END of the list. It used to open the frame that did not fit -- a frame the
# list never showed.
for y in 208 209 $BODY_BOTTOM; do
  want "a press at y=$y opens nothing" "NONE 0" 0 $VIEWS_X $y
done

echo "the views either side of it"
# BACK and SURVEY reach the same way. Slot 0 is x 4..132, slot 2 is 268..396.
want "BACK's bottom edge leaves the screen"  "BACK 0"    0  60 239
want "SURVEY's bottom edge opens the survey" "HANDLED 7" 0 330 239
# The SURVEY view's START button ends exactly ON bodyBottom (surveyButtonBox),
# and that row is still the button's: the bar takes the screen BELOW the body,
# not the body's own last row.
want "START keeps its last pixel row"        "HANDLED 7" 7 $VIEWS_X $BODY_BOTTOM
want "the row under START is the bar"        "HANDLED 9" 7 $VIEWS_X 211

echo "the picker gives two slots up, and declines them"
# In PICK barLabels() returns no label for slots 1 and 2, drawBar paints them as
# empty space, and a press on them is declined rather than falling through.
got="$(./squachsim lora /dev/null --size "$SIZE" --frames 6 --loraview 0 \
        --tap 2:$VIEWS_X:224 --tap 4:$VIEWS_X:224 2>&1 >/dev/null |
       sed -n 's/.*view \([0-9]*\)/\1/p' | tr '\n' ' ')"
if [ "$got" = "9 9 " ]; then
  pass=$((pass+1))
else
  fail=$((fail+1)); echo "  FAIL: a second press on the dead slot -- wanted '9 9 ', got '$got'"
fi

echo "the picker's dead slots are PAINTED out, not left on the glass"
# The per-frame repaint stops at bodyBottom and never reaches the bar, so a
# slot barLabels() gives up kept whatever button the view before had drawn
# there: the picker showed [ VIEWS ] and [ SURVEY ] over a screen that IS the
# views, both of them inert. Checked on the pixels, in raw RGB888 (--raw), at
# the top-left corner of slot 1 -- a border pixel when a button is drawn there
# -- against a point in the bare strip under the bar, which is background in
# every view. No palette constant needed: the question is only whether those
# two are the same colour.
pix() {   # <raw file> <x> <y>
  dd if="$1" bs=1 skip=$(( ($3 * 400 + $2) * 3 )) count=3 2>/dev/null |
    od -An -tu1 | tr -s ' ' | sed 's/^ //;s/ $//'
}
RAW=$(mktemp -d)
./squachsim lora /dev/null --size "$SIZE" --frames 3 --loraview 0 --raw "$RAW/list.raw" >/dev/null 2>&1
./squachsim lora /dev/null --size "$SIZE" --frames 6 --loraview 0 --tap 2:$VIEWS_X:224 --raw "$RAW/pick.raw" >/dev/null 2>&1
for case in "list DIFFER" "pick SAME"; do
  set -- $case
  corner="$(pix "$RAW/$1.raw" 136 $BAR_TOP)"; bare="$(pix "$RAW/$1.raw" 136 238)"
  if { [ "$2" = SAME ] && [ "$corner" = "$bare" ]; } ||
     { [ "$2" = DIFFER ] && [ "$corner" != "$bare" ]; }; then
    pass=$((pass+1))
  else
    fail=$((fail+1))
    echo "  FAIL: slot 1's corner in $1 should $2 from the strip under the bar" \
         "-- corner '$corner', bare '$bare'"
  fi
done
rm -rf "$RAW"

echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
