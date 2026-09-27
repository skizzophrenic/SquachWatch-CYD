#!/bin/bash
# The bottom button bar every screen shares, and the one control on it that
# destroys something. Pinned through the REAL src/main.cpp -- squachsim-live
# compiles the firmware's own loop() and touch dispatch -- so these are
# transitions of the actual state machine, not a hit test called in isolation.
#
#   ./test_button_bar.sh
#
# THE TWO THINGS IT HOLDS.
#
# 1. THE BAR OWNS THE BOTTOM EDGE OF THE GLASS. Theme::computeButtonBar puts
#    the bar at screenH - h - 6, so there are six rows of bare screen under the
#    drawn buttons and nothing ever drawn there. hitTestButtonBar used to answer
#    NONE in that band. Those rows are 1.9 mm on the CrowPanel and 0.9 mm on the
#    2.8" CYD, off the bottom of a target that was 8.0 mm tall to begin with --
#    and on the CLEAR screen the band was not merely dead, it belonged to the
#    background-cycling edge zone, so a press aimed at [ DESK ] and landing a
#    millimetre low changed the wallpaper instead. Same species as the LORA
#    screen's reported bug (sim/test_lora_bar.sh), on every other screen.
#
#    The bar's height follows the glass now (Theme::buttonBarH): this binary is
#    the 2.8" CYD build, 0.178 mm to the pixel, where a 20-row bar plus the six
#    was 4.6 mm, so it is 34 rows here -- 40 touchable, 7.1 mm. The constants
#    below are that geometry; the CrowPanel keeps 20 and test_lora_bar.sh
#    still says 214.
#
# 2. [ CLR ] ASKS FIRST. It wipes the detection log, and it is the third slot of
#    the same bar -- the same rectangle that says [ DESK ] and opens desk mode on
#    the screen you just came from. SETTINGS > RESET STATS raises a whole confirm
#    panel to zero the detection COUNTS; erasing every entry had nothing. It is
#    now arm-and-confirm: one press asks, a second within 2.5 s goes through,
#    and an arm left over from a previous visit to the screen does not count.
#
# The first run of the firmware wants the colour check answered, so the NVS is
# primed once into a scratch directory and every case starts from that.
cd "$(dirname "$0")"

NVS="$(mktemp -d)"      # the primed store, never written to by a case
RUN="$NVS.run"          # the copy each case actually runs against
trap 'rm -rf "$NVS" "$RUN"' EXIT

# 320x240 -- squachsim-live's own canvas. computeButtonBar(320,240) at the
# 2.8"'s pitch: h=34, y=200, margin 8, gap 8, so the slots are x 8..104,
# 112..208 and 216..312, drawn in rows 200..233 with 234..239 bare under them.
BAR_TOP=200       # the first row the buttons are drawn in
BAR_DRAWN=233     # the last one
BOTTOM=239        # the last row of the screen: the band this test is about
SCAN_X=56         # slot 0 centre
LOG_X=160         # slot 1 centre
CLR_X=264         # slot 2 centre
CORNER_X=300      # still inside slot 2, and inside CLEAR's right-hand tenth
                  # (320/10 = 32, so x >= 288): the wallpaper corner.

pass=0; fail=0

# The frames come back on stdout as "FRM w h bytes state mesh" headers with the
# raw pixels behind them, so the stream is walked rather than grepped.
states() { python3 -c '
import sys
b = sys.stdin.buffer
while True:
    line = b.readline()
    if not line: break
    if line.startswith(b"FRM "):
        p = line.split(); print(p[4].decode()); b.read(int(p[3]))
'; }

# HOW LONG A PRESS IS HELD, and it is bounded on both sides. One S step is one
# loop() iteration, and virtual time advances by the delay() the frame governor
# takes, so a step is about 33 ms here rather than 1 ms. The main screen acts on
# the PRESS and will not look at it until TOUCH_DEBOUNCE_MS (200 ms) has passed,
# which is six steps; the log screen acts on the RELEASE and only calls it a tap
# within TAP_MAX_MS (500 ms), which is fifteen. Ten steps -- about 330 ms -- is
# a tap to both of them, and nowhere near a hold (SQ_HOLD_MS is 600 and the CLR
# costume unlock is 4000). The settle afterwards is long on purpose: it has to
# clear the debounce before the next press.
# tap <x> <y> -> the command lines for one press
tap() { printf 'D %s %s\nS 10\nU\nS 20\n' "$1" "$2"; }

# EVERY CASE GETS ITS OWN STORE, copied from the primed one. The firmware
# persists what a case did -- entering desk mode sets deskWanted, so the next
# boot would come up on the desk instead of the main screen -- and a test whose
# starting screen depends on the case before it is not a test.
# run <script-on-stdin...> -> the state of the LAST frame emitted
#
# The settling press before each case is not ceremony. Squachy's show-off
# parade eats the first touch that lands during it ("any tap at all ends the
# parade, and is consumed doing it" -- src/main.cpp's CLEAR case), so without
# one the first real press is swallowed about as often as not. It lands in the
# middle of the screen, where the worst it can do is pet him.
run() {
  rm -rf "$RUN"; cp -R "$NVS" "$RUN"
  { printf 'S 600\nD 160 120\nS 60\nU\nS 20\n'; cat; printf 'Q\n'; } |
    SQUACHSIM_NVS="$RUN" ./squachsim-live 2>/dev/null | states | tail -1
}

want() {   # want <description> <expected state> <commands on stdin>
  local got; got="$(run)"
  if [ "$got" = "$2" ]; then
    pass=$((pass+1))
  else
    fail=$((fail+1)); echo "  FAIL: $1 -- wanted '$2', got '$got'"
  fi
}

# ---- prime the store so every case below boots straight to the main screen.
# [ LOOKS GOOD ] on the first-boot colour check, at 320x240.
printf 'S 300\nD 160 222\nS 10\nU\nS 10\nQ\n' |
  SQUACHSIM_NVS="$NVS" ./squachsim-live >/dev/null 2>&1

echo "the bar owns every row from its top edge to the bottom of the glass"
for y in $BAR_TOP 210 220 $BAR_DRAWN 235 237 $BOTTOM; do
  want "a press at y=$y on [ LOG ] opens the log" LOG <<EOF
$(tap $LOG_X $y)
EOF
done

echo "...and not one row above it"
# bodyBottom territory. The main screen keeps it; what it does with it is its
# own business, but it is not the bar.
for y in 196 199; do
  want "a press at y=$y is not the bar" CLEAR <<EOF
$(tap $LOG_X $y)
EOF
done

echo "the bottom corners, which used to change the wallpaper instead"
want "[ DESK ]'s bottom-right corner opens the desk" DESK <<EOF
$(tap $CORNER_X $BOTTOM)
EOF
want "[ DESK ]'s last drawn row still does too" DESK <<EOF
$(tap $CORNER_X $BAR_DRAWN)
EOF

echo "[ CLR ] asks before it erases the log"
want "one press on CLR leaves the log up" LOG <<EOF
$(tap $LOG_X $BOTTOM)
$(tap $CLR_X $BOTTOM)
EOF
want "a second press within the window goes through" CLEAR <<EOF
$(tap $LOG_X $BOTTOM)
$(tap $CLR_X $BOTTOM)
$(tap $CLR_X $BOTTOM)
EOF
# CLR_CONFIRM_MS is 2500; 4000 virtual milliseconds is past it.
want "an answer that came too late only asks again" LOG <<EOF
$(tap $LOG_X $BOTTOM)
$(tap $CLR_X $BOTTOM)
S 4000
$(tap $CLR_X $BOTTOM)
EOF
# The arm is a function static and would otherwise outlive the screen -- the
# shape that put a stale press on the LORA screen (see that screen's header).
want "an arm from a previous visit does not count" LOG <<EOF
$(tap $LOG_X $BOTTOM)
$(tap $CLR_X $BOTTOM)
$(tap $SCAN_X $BOTTOM)
$(tap $LOG_X $BOTTOM)
$(tap $CLR_X $BOTTOM)
EOF

echo "the other two slots reach from the bottom edge as well"
want "[ SCAN ] from the log's bottom edge returns to the main screen" CLEAR <<EOF
$(tap $LOG_X $BOTTOM)
$(tap $SCAN_X $BOTTOM)
EOF
want "[ LOG ] from the log's bottom edge toggles back off" CLEAR <<EOF
$(tap $LOG_X $BOTTOM)
$(tap $LOG_X $BOTTOM)
EOF

echo
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
