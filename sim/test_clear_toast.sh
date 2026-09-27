#!/bin/bash
# A toast raised for the CLEAR screen is on the CLEAR screen, for its lifetime.
#
#   ./test_clear_toast.sh
#
# THE BUG THIS EXISTS FOR. main.cpp's CLEAR case drew Theme::drawToast() and
# then, over it, the time-zone card -- the "WHICH TIME ZONE?" panel that sits
# in the middle of the screen for as long as the clock is trusted and no zone
# has been picked. A toast is a box in the middle of the screen. So on every
# board in that state -- and the emulator is in it from its first boot until
# THIS IS RIGHT is pressed -- SNOOZED, READ, NOTHING NEARBY and the boot notes
# were painted and covered in the same frame, and a bench that drove the real
# state machine counted zero toast pixels on CLEAR in every case it tried.
#
# Driven through squachsim-live, the target that compiles the firmware's own
# main.cpp, so the frames here are the frames loop() pushes. The toast is
# SNOOZED (an AirTag alert's SNOOZE button), 1500 ms long, amber; what is
# asserted is the box, not the colour: drawToast() draws a one-pixel border in
# the accent round a box it fills with the background, and with the head
# "SNOOZED" and the sub "This one, until restart" that box is 168 x 48 on a
# 320x240 canvas -- x 76..243, y 96..143 (the arithmetic is in theme.cpp:
# textWidth + 30, centred). The four corners must be one colour, bright, and
# the top edge must be that colour from end to end; anything painted over the
# toast breaks the edge.
cd "$(dirname "$0")"

NVS="$(mktemp -d)"; RUN="$NVS.run"
trap 'rm -rf "$NVS" "$RUN"' EXIT

pass=0; fail=0

# Reads the frame stream and answers, for the LAST frame, whether the toast
# box is intact: "TOAST <state>" or "NOTOAST <state>".
toast_in_last_frame() { python3 -c '
import sys
b = sys.stdin.buffer; last = None
while True:
    line = b.readline()
    if not line: break
    if line.startswith(b"FRM "):
        p = line.split(); w, h, n = int(p[1]), int(p[2]), int(p[3])
        last = (p[4].decode(), w, b.read(n))
state, w, d = last
px = lambda x, y: d[(y * w + x) * 3:(y * w + x) * 3 + 3]
X0, X1, Y0, Y1 = 76, 243, 96, 143
c = px(X0, Y0)
corners = all(px(x, y) == c for x, y in ((X1, Y0), (X0, Y1), (X1, Y1)))
edge = all(px(x, Y0) == c for x in range(X0, X1 + 1))
bright = max(c) > 100
print(("TOAST " if corners and edge and bright else "NOTOAST ") + state)
'; }

# run <commands on stdin> -> TOAST|NOTOAST and the state of the last frame.
# The settling press first: Squachy'"'"'s boot parade eats the first touch.
run() {
  rm -rf "$RUN"; cp -R "$NVS" "$RUN"
  { printf 'S 600\nD 160 120\nS 60\nU\nS 20\n'; cat; printf 'Q\n'; } |
    SQUACHSIM_NVS="$RUN" ./squachsim-live 2>/dev/null | toast_in_last_frame
}

want() {   # want <description> <expected> <commands on stdin>
  local got; got="$(run)"
  if [ "$got" = "$2" ]; then pass=$((pass+1))
  else fail=$((fail+1)); echo "  FAIL: $1 -- wanted '$2', got '$got'"; fi
}

# [ LOOKS GOOD ] on the first-boot colour check, at 320x240. Nothing else: the
# zone card is then up on CLEAR, which is the state this test is about.
printf 'S 300\nD 160 222\nS 10\nU\nS 10\nQ\n' | SQUACHSIM_NVS="$NVS" ./squachsim-live >/dev/null 2>&1

# An AirTag, its alert, and SNOOZE: bottom centre of the ALERT screen, 84x28 at
# y 208..236 (ui_alert.cpp snoozeBtnRect). A 3-step press (~100 ms) so the
# finger is up before CLEAR'"'"'s own debounce could read it as a bar press.
SNOOZE='T AIRTAG
S 7
D 160 222
S 3
U
S 1'

echo "with the zone card up, which is where it was covered"
want "the frame after SNOOZE shows the toast on CLEAR" "TOAST CLEAR" <<EOF2
$SNOOZE
EOF2
want "...and it is still there 1.3 s later" "TOAST CLEAR" <<EOF2
$SNOOZE
S 39
EOF2
want "...and gone once its 1.5 s are up" "NOTOAST CLEAR" <<EOF2
$SNOOZE
S 50
EOF2

echo "with the zone card dismissed (THIS IS RIGHT), the case that always worked"
DISMISS='D 160 143
S 3
U
S 20'
want "the frame after SNOOZE shows the toast on CLEAR" "TOAST CLEAR" <<EOF2
$DISMISS
$SNOOZE
EOF2
want "...and gone once its 1.5 s are up" "NOTOAST CLEAR" <<EOF2
$DISMISS
$SNOOZE
S 50
EOF2

echo
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
