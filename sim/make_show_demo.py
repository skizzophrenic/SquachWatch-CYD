#!/usr/bin/env python3
"""Renders a release's show-off clip: captioned scenes, one after another.

    python3 make_show_demo.py --render-only [--clip NAME]   # under WSL, after `make`
    python  make_show_demo.py --encode-only [--clip NAME]   # wherever Pillow is installed

Every scene is a run of the one-shot emulator, so every frame is the shipping
code deciding what to draw; the caption under it is the only thing staged.
Output lands in the firmware's docs/ so the release notes can embed it from
the tag -- which is the whole reason this is a script in the repo and not a
one-off: the v1.13.0 clip was rendered from a heredoc, never copied into
docs/, and shipped in no release notes at all.

A scene is (screen, warm-up frames, captured frames, extra args, env, hold
ms, caption). A scene whose captured-frames field is a list is a scroll
sweep: one run per --scroll value, one frame each, which is how a list
scrolling to its stop is shown.

The clock is pinned (SQUACH_EPOCH) so the clip renders the same on any day.
"""
import json, os, shutil, subprocess, sys

HERE  = os.path.dirname(os.path.abspath(__file__))
FONT  = os.path.join(HERE, "Bangers-Regular.ttf")
EPOCH = "1789396740"   # Mon 14 Sep 2026, 14:39 UTC -- the desk clip's moment too
W, H  = 320, 240       # the default panel; CLIP_SIZE overrides it per clip
ZOOM  = 2              # integer only: nearest-neighbour keeps device pixels square
MS    = 66             # per captured frame: two 33 ms steps, about real time
BAND  = 52             # the caption band under the screen, in output pixels
CYAN  = (0, 214, 214)

# ---- the clips ----

CLIPS = {
    # v1.33.0 "Doin Time": the arrest (SQUACHSIM_JAIL=1 runs the sirens and
    # the ball drop from frame 0), the sentence in STRIPES with the ball
    # talking (=2 is already serving), a throw the chain stops short, the
    # clock over the ball once he has said his piece (~6.6 s in), the ball
    # kept as a pet (--pet 5; his first line is due six seconds in), the
    # Settings row, and the flash that came back.
    "doin-time": [
        ("clear", 4, 60, ["--noseed", "--bg", "8", "--pet", "4"], {"SQUACHSIM_JAIL": "1"}, 1500, "TEN PET THROWS IN A MINUTE. SIRENS."),
        ("clear", 20, 60, ["--noseed", "--bg", "8", "--pet", "4"], {"SQUACHSIM_JAIL": "2"}, 1600, "TEN MINUTES IN STRIPES, CHAINED TO A CRITIC"),
        ("clear", 40, 70, ["--noseed", "--bg", "5", "--pet", "4"], {"SQUACHSIM_JAIL": "2", "SQUACHSIM_THROW": "s:160:150:120:30"}, 1500, "THROW HIM ANYWAY. THE CHAIN HAS OPINIONS."),
        ("clear", 200, 50, ["--noseed", "--bg", "8", "--pet", "4"], {"SQUACHSIM_JAIL": "2"}, 1500, "THE CLOCK HANGS OVER THE BALL"),
        ("clear", 175, 70, ["--noseed", "--bg", "5", "--pet", "5"], {}, 1600, "SERVE IT ALL AND YOU CAN KEEP HIM"),
        ("settings", 10, 16, ["--pet", "5", "--scroll", "3"], {"SQUACHSIM_PAGE": "1"}, 1300, "SETTINGS > PET > BALL & CHAIN"),
        ("clear", 100, 40, ["--bg", "2", "--pet", "3"], {}, 1600, "AND 70 KB OF FLASH BACK. NOBODY NOTICED."),
    ],
    # v1.32.0 "Put On Your Brave Face": T0@$TY on the TOASTERS background he
    # is earned on (--pet 4 unlocks him; his first tip is due eight seconds
    # in), a catch popping toast that Squachy eats (SQUACHSIM_CLIPCATCH goes
    # to whichever pet is out), a hard throw that lands him on his back at
    # Squachy's feet, C1iPPY's new landing lines, the reworked counter
    # buttons, and the StickS3 taking a third row.
    "brave-face": [
        ("clear", 112, 44, ["--noseed", "--bg", "2", "--pet", "4"], {}, 1500, "TAP ONE FLYING TOASTER THREE TIMES. MEET T0@$TY."),
        ("clear", 10, 64, ["--noseed", "--bg", "0", "--pet", "4"], {"SQUACHSIM_CLIPCATCH": "1"}, 1500, "EVERY CATCH POPS TOAST. SQUACHY EATS IT."),
        ("clear", 30, 70, ["--noseed", "--bg", "8", "--pet", "4"], {"SQUACHSIM_THROW": "c:-1:0:-150:-90"}, 1500, "THROW HIM. HE LANDS LIKE A TOASTER."),
        ("clear", 30, 60, ["--noseed", "--bg", "2", "--pet", "3"], {"SQUACHSIM_THROW": "c:-1:0:-120:-70"}, 1400, "A DOZEN NEW LANDING LINES EACH. THEY COUNT, TOO."),
        ("clear", 6, 40, ["--bg", "5", "--pet", "4"], {}, 1400, "THE BUTTONS: BLACK LETTERS WITH ROOM TO BREATHE"),
        ("clear", 60, 30, ["--bg", "5", "--pet", "4"], {"_SIZE": "240x135"}, 1600, "THE STICK BORROWS A THIRD ROW ON A BUSY DAY"),
    ],
    # v1.31.0 "Need Help?": C1iPPY (earned at ten catches) and his first tip
    # (due six seconds in), the keyboard shortcut that also unlocks him, Squachy thrown (SQUACHSIM_THROW holds him for
    # the hold time, then flings), C1iPPY thrown, and the counters as XP
    # taskbar buttons with the seeded catches in range.
    "need-help": [
        ("clear", 185, 44, ["--noseed", "--bg", "0", "--pet", "3"], {}, 1500, "CATCH TEN THINGS. MEET C1iPPY."),
        ("phone", 0, 24, [], {}, 1400, "OR BACKSPACE TEN TIMES. HE NOTICES."),
        ("clear", 120, 40, ["--noseed", "--bg", "4", "--pet", "3"], {"SQUACHSIM_CLIPCATCH": "1"}, 1400, "EVERY CATCH GETS HIS EXPERT OPINION"),
        ("clear", 120, 76, ["--noseed", "--bg", "2", "--pet", "3"], {"SQUACHSIM_THROW": "s:160:120:250:40"}, 1300, "PICK SQUACHY UP. FLING. HE'LL ALLOW IT."),
        ("clear", 120, 62, ["--noseed", "--bg", "2", "--pet", "3"], {"SQUACHSIM_THROW": "c:-1:0:-120:-70"}, 1400, "C1iPPY BOUNCES BETTER. HE'S WIRE."),
        ("clear", 6, 44, ["--bg", "5", "--pet", "3"], {}, 1400, "NEARBY RETIRED. TASKBAR HIRED. NEW ONES FLASH."),
        ("settings", 10, 16, ["--scroll", "5"], {"SQUACHSIM_PAGE": "1"}, 1300, "MISS THE OLD LOOK? APPEARANCE > DETECTIONS"),
        ("clear", 200, 24, ["--bg", "5", "--pet", "3"], {"SQUACHSIM_CLASSIC": "1"}, 1300, "CLASSIC BRINGS NEARBY BACK"),
        ("clear", 40, 40, ["--noseed", "--bg", "1", "--pet", "3"], {"SQUACHSIM_HEADSUP": "1|NESSIE"}, 1600, "SQUAD HEADS-UP: A FLOCK NEAR A FRIEND? YOU KNOW."),
        ("clear", 100, 30, ["--bg", "0", "--pet", "3"], {"_SIZE": "240x135"}, 1300, "NEW BOARD: M5STACK STICKS3 (BETA)"),
        ("clear", 100, 30, ["--bg", "6", "--pet", "3"], {"_SIZE": "240x135"}, 1600, "AND THE M5STACK CARDPUTER ADV (BETA)"),
    ],
    # v1.30.0 "No Spoon": the red glyph that falls in the DIGITAL rain now and
    # then (picked ~1100 steps in, column 5, so the tap lands on it ~25 steps
    # later), the TH3 0N3 it earns, his red lenses with something serious
    # seeded nearby, bullet time and the duck (the second bullet, ~19 s in,
    # clear of --outfit's six-second party shimmer), a pill, the spoon, and
    # the payphone answering for him.
    "no-spoon": [
        ("clear", 1095, 22, ["--noseed", "--bg", "0"], {}, 1500, "ONE GLYPH IN THE RAIN FALLS RED..."),
        ("clear", 1117, 40, ["--noseed", "--bg", "0", "--tap", "1125:29:93"], {}, 1800, "TAP IT"),
        ("clear", 260, 40, ["--noseed", "--bg", "0", "--outfit", "18"], {}, 1500, "TH3 0N3"),
        ("clear", 260, 30, ["--bg", "0", "--outfit", "18"], {}, 1600, "SOMETHING SERIOUS NEARBY? THE CODE RUNS RED"),
        ("clear", 572, 52, ["--noseed", "--bg", "0", "--outfit", "18", "--tap", "586:160:110"], {}, 1500, "BULLET TIME. TAP HIM AND HE DUCKS"),
        ("clear", 260, 40, ["--noseed", "--bg", "0", "--outfit", "18", "--tap", "268:144:66"], {}, 1500, "TAP A LENS. RED OR BLUE?"),
        ("clear", 260, 40, ["--noseed", "--bg", "0", "--outfit", "18"], {"SQUACHSIM_GRAB": "160:70"}, 1500, "PICK HIM UP. THERE IS NO SPOON."),
        ("phone", 0, 50, ["--outfit", "18"], {}, 1600, "AND THE PAYPHONE RINGS FOR HIM"),
    ],
    # v1.29.0 "What Reeks": the FIRE owl's new line and the tap that answers
    # it (the clock is millis from boot, so ~910 frames in is his third quip
    # slot, the one he asks in), the SHAMBLER it earns, a Legend visiting in
    # his own aura, SQUAD's SEND, NEARBY keeping quiet about your own things,
    # and the C5's faster screen.
    "what-reeks": [
        ("clear", 915, 30, ["--noseed", "--bg", "6"], {}, 1600, "THE OWL ON THE CAMPFIRE HAS A QUESTION"),
        ("clear", 945, 60, ["--noseed", "--bg", "6", "--tap", "935:33:68"], {}, 1800, "TAP HIM WHILE HE ASKS"),
        ("clear", 260, 40, ["--noseed", "--bg", "6", "--outfit", "17"], {}, 1600, "MEET THE SHAMBLER"),
        ("clear", 260, 40, ["--noseed", "--bg", "1", "--outfit", "17"], {"SQUACHSIM_LEGEND": "1"}, 1500, "FLIES INCLUDED. AURA OPTIONAL."),
        ("clear", 400, 36, ["--noseed", "--bg", "2", "--peer", "16", "--peername", "NESSIE"], {"SQUACHSIM_PEERAURA": "1"}, 1600, "LEGENDS VISIT IN THEIR AURA NOW"),
        ("squad", 30, 12, ["--pose", "1"], {}, 1500, "SQUAD GOT A SEND BUTTON"),
        ("clear", 120, 24, ["--bg", "2"], {"SQUACHSIM_SNOOZEALL": "1"}, 1700, "YOUR OWN STUFF STOPS SHOUTING NEARBY"),
        ("clear", 200, 30, ["--noseed", "--bg", "3"], {}, 1600, "AND THE C5 DRAWS TWICE AS FAST"),
    ],
    # v1.28.0 "Over 9000": the scouter unlock (a tap on his shades with the
    # aura lit, which --tap lands on him), the outfit it earns, and the tank
    # redrawn in whole numbers. The C5 is a chip, not a screen, so it gets a
    # caption. SQUACHSIM_LEGEND lights the aura without the catches.
    "over-9000": [
        ("clear", 260, 30, ["--noseed", "--bg", "5"], {"SQUACHSIM_LEGEND": "1"}, 1500, "A LEGEND. THOSE SHADES READ MORE THAN LIGHT..."),
        ("clear", 30, 104, ["--noseed", "--bg", "5", "--tap", "40:160:88"], {"SQUACHSIM_LEGEND": "1"}, 1800, "TAP THEM. HOLD STILL. READING..."),
        ("clear", 260, 36, ["--noseed", "--bg", "1", "--outfit", "16"], {"SQUACHSIM_LEGEND": "1"}, 1600, "OVER 9000. OBVIOUSLY."),
        ("clear", 260, 36, ["--noseed", "--bg", "8", "--outfit", "16"], {}, 1400, "GOLD HAIR, TORN SLEEVE, STATIC. IT'S A LOOK."),
        ("clear", 200, 36, ["--noseed", "--bg", "3"], {}, 1600, "THE TANK DRAWS IN WHOLE NUMBERS NOW. SAME FISH."),
        ("clear", 200, 30, ["--noseed", "--bg", "7"], {}, 1800, "AND A NEW CHIP: THE ESP32-C5. FIRST RISC-V BOARD."),
    ],
    # v1.27.0 "Power-Up": the Legend's aura replaces the top hat, and the
    # YZZERD wizard, unlocked by tapping XYZZY on the TERMINAL background.
    # SQUACHSIM_LEGEND wears the Legend look without the catches; the
    # SQUACHSIM_XYZZY scenes keep the word up and tap it three times.
    "power-up": [
        ("clear", 260, 36, ["--noseed", "--bg", "5"], {"SQUACHSIM_LEGEND": "1"}, 1500, "500 CATCHES USED TO GET YOU A TOP HAT"),
        ("clear", 260, 36, ["--noseed", "--bg", "8"], {"SQUACHSIM_LEGEND": "1"}, 1500, "NOW YOU GET THIS"),
        ("clear", 40, 24, ["--noseed", "--bg", "4"], {"SQUACHSIM_XYZZY": "1"}, 1200, "SOMETHING KEEPS TYPING ON THE TERMINAL..."),
        ("clear", 400, 30, ["--noseed", "--bg", "4", "--tap", "40:64:80", "--tap", "200:256:80", "--tap", "380:64:80"],
         {"SQUACHSIM_XYZZY": "1"}, 1500, "TAP IT THREE TIMES. SOMETHING HAPPENS."),
        ("clear", 260, 36, ["--noseed", "--bg", "4", "--outfit", "15"], {}, 1500, "MEET YZZERD"),
        ("clear", 260, 36, ["--noseed", "--bg", "1", "--outfit", "15"], {"SQUACHSIM_LEGEND": "1"}, 1600, "A LEGENDARY WIZARD. ON FIRE. ON PURPOSE."),
        ("settings", 10, 14, ["--scroll", "0"], {"SQUACHSIM_PAGE": "1", "SQUACHSIM_LEGEND": "1"}, 1600, "SETTINGS > APPEARANCE > AURA"),
    ],
    # v1.26.0 "Off the Grid": LoRa on the watches (not something the emulator
    # can draw: it is not the watch build and has no LoRa radio), so the clip
    # shows what IS on every screen: PRIVACY MODE, the same LOG before and
    # after, an alert under it, and its row on the SYSTEM page.
    "off-the-grid": [
        ("clear", 200, 30, ["--noseed", "--bg", "8"], {}, 1500, "SQUACHY GREW A THIRD EAR: LORA, ON THE WATCHES"),
        ("log", 20, 20, [], {}, 1700, "FILMING? YOUR LOG LOOKS LIKE THIS..."),
        ("log", 20, 20, [], {"SQUACHSIM_PRIVACY": "1"}, 2200, "...PRIVACY MODE MAKES IT THIS"),
        ("alert", 40, 30, [], {"SQUACHSIM_PRIVACY": "1"}, 1800, "SAME ALERT. NOBODY'S NAME ON IT."),
        ("settings", 10, 14, ["--scroll", "0"], {"SQUACHSIM_PAGE": "2", "SQUACHSIM_PRIVACY": "1"}, 1800, "SETTINGS > SYSTEM > PRIVACY MODE"),
    ],
    # v1.25.0 "Trailhead": wardriving on the T-Watch S3 Plus, which the
    # emulator cannot show (it is not the watch build), so Squachy sets off
    # and the notes carry the watch. Then the two things that ARE on screen:
    # the status light's new bottom steps, and headings that no longer fold.
    "trailhead": [
        ("clear", 200, 36, ["--noseed", "--bg", "7"], {}, 1500, "SQUACHY IS GOING WARDRIVING"),
        ("clear", 200, 30, ["--noseed", "--bg", "1"], {}, 1500, "EVERY NETWORK HE HEARS, AND WHERE"),
        ("light", 10, 14, [], {"SQUACHSIM_LTBRIGHT": "1"}, 1500, "THE STATUS LIGHT GOES DIMMER NOW"),
        ("settings", 10, 14, ["--scroll", "0"], {}, 1500, "SETTINGS STOPPED FOLDING UP ON YOU"),
    ],
    # v1.24.1 "Ghost Town": a hotfix with nothing new on screen -- the fix is
    # the absence of phantom FLOCK rows, so the clip is the log as it should
    # read, and Squachy at rest.
    "ghost-town": [
        ("log", 20, 30, [], {}, 1800, "NO MORE PHANTOM FLOCKS"),
        ("clear", 200, 30, ["--noseed", "--bg", "3"], {}, 1800, "SAME SQUACHY. FEWER GHOSTS."),
    ],
    # v1.24.0 "Look Up": drones are really detected now (the page is the
    # emulator's seeded aircraft, decoded), ignored devices wear a tag, and
    # eighteen more time zones, shown on the zone card.
    "look-up": [
        ("log", 20, 40, ["--info", "7"], {}, 1800, "REAL DRONES SHOW UP NOW. PILOT INCLUDED."),
        ("log", 20, 30, [], {}, 1400, "IGNORED DEVICES WEAR A TAG NOW"),
        ("zonecard", 30, 20, [], {"SQUACHSIM_ZONE": "33"}, 1100, "18 MORE TIME ZONES. HELLO, BANGKOK."),
        ("zonecard", 30, 20, [], {"SQUACHSIM_ZONE": "26"}, 1100, "HELLO, JOHANNESBURG."),
        ("zonecard", 30, 20, [], {"SQUACHSIM_ZONE": "35"}, 1400, "HELLO, SEOUL."),
    ],
    # v1.23.0 "Locked On": the new watch-list alert. One scene per line
    # Squachy says (they change every 7 s), each started a little after its
    # line so the bubble has typed out and the sweep has come round.
    "locked-on": [
        ("watchalert", 20,  60, [], {}, 1200, "SOMETHING ON YOUR WATCH LIST IS BACK"),
        ("watchalert", 250, 50, [], {}, 1200, "THE NEARER THE MIDDLE, THE NEARER TO YOU"),
        ("watchalert", 460, 50, [], {}, 1600, "AND IT WAITS UNTIL YOU TAP. NO RUSH."),
    ],
    # v1.22.0 "Costume Drama": the wardrobe, redrawn. One outfit a scene on
    # CLEAR, long enough for each one's moving part (the propeller, the
    # headband, the blinking chest lights) to do its thing. Warm-up 200 so
    # the "every outfit unlocked" bubble --outfit brings with it has gone.
    "costume-drama": [
        ("clear", 200, 16, ["--outfit", "1",  "--noseed", "--bg", "3"],  {}, 800, "THE TANOOKI SUIT. THE WHOLE SUIT."),
        ("clear", 200, 16, ["--outfit", "3",  "--noseed", "--bg", "4"],  {}, 800, "TINFOIL HAT NOW HAS A PROPELLER. FOR SCIENCE."),
        ("clear", 200, 16, ["--outfit", "4",  "--noseed", "--bg", "10"], {}, 800, "THE NINJA IS VISIBLE NOW. BAD NINJA."),
        ("clear", 200, 16, ["--outfit", "5",  "--noseed", "--bg", "8"],  {}, 800, "THE S IS FOR SQUACHY. LAWYERS, RELAX."),
        ("clear", 200, 16, ["--outfit", "6",  "--noseed", "--bg", "5"],  {}, 700, "TALL BRO. SAME, BUT TALLER."),
        ("clear", 200, 16, ["--outfit", "7",  "--noseed", "--bg", "1"],  {}, 800, "THE SPACE SUIT HAS ARMS NOW"),
        ("clear", 200, 16, ["--outfit", "8",  "--noseed", "--bg", "0"],  {}, 700, "GOTTA DETECT FAST"),
        ("clear", 200, 16, ["--outfit", "9",  "--noseed", "--bg", "3"],  {}, 800, "A PIRATE HAT, NOT A TENT"),
        ("clear", 200, 16, ["--outfit", "10", "--noseed", "--bg", "6"],  {}, 800, "THE WOLF PELT GREW PAWS"),
        ("clear", 200, 16, ["--outfit", "11", "--noseed", "--bg", "2"],  {}, 800, "CHROME WING IS ACTUALLY CHROME"),
        ("clear", 200, 16, ["--outfit", "12", "--noseed", "--bg", "1"],  {}, 900, "THE VOID BLINKS BACK"),
    ],
    # v1.21.0 "All Ears": the watch hears again, the buzz grew up, and the
    # watch rows got a page. Shot on the watch's 240x240 like v1.20.0's; the
    # emulator is not the watch build, so the page itself is the notes' to
    # describe and the captions carry the story.
    "all-ears": [
        ("clear",  60, 36, ["--noseed", "--bg", "7"],  {}, 1600, "HE HEARS AGAIN. EVERY BOOT. WE CHECKED."),
        ("alert",  30, 16, [],                          {}, 1600, "THE BUZZ GREW A BACKBONE. MED BY DEFAULT."),
        ("clear",  60, 36, ["--noseed", "--bg", "10"], {}, 1500, "WORN ALL DAY. NOT ONE DEAF MINUTE."),
    ],
    # v1.20.0 "SquachWatch^2": he moves onto a wrist. Shot on the T-Watch S3's
    # own 240x240 panel (see CLIP_SIZE). The emulator is not the watch build,
    # so the corner clock and the WATCH settings are the notes' to describe.
    "squachwatch-squared": [
        ("clear",  60, 36, ["--noseed", "--bg", "7"],                                  {}, 1500, "SQUACHWATCH. ON A WATCH."),
        ("clear",  60, 36, ["--peer", "3", "--peername", "POOTS", "--noseed", "--bg", "6"], {}, 1600, "SQUAD VISITS STAY PUT NOW"),
        ("clear", 716, 32, ["--showoff", "--noseed", "--bg", "10"],                    {},  900, "240 BY 240. HE FITS. MOSTLY."),
    ],
    # v1.19.1 "Crash Override": the WiFi update that finishes, the label he
    # reads now, and the shark suit that survives a visit.
    "crash-override": [
        ("sysprops", 20, 24, ["--tab", "0"],                                                  {}, 1500, "THE WIFI UPDATE FINISHES NOW. ALL OF IT."),
        ("sysprops", 20, 24, ["--tab", "0"],                                                  {}, 1500, "ON 1.13 TO 1.19? DO THIS ONE BY USB OR BLUETOOTH"),
        ("clear",    60, 36, ["--peer", "14", "--peername", "STOMPY", "--noseed", "--bg", "6"], {}, 1500, "THE SHARK SUIT SURVIVES THE TRIP"),
    ],
    # v1.19.0 "Neighbourhood Watch": the regulars, the nemesis, what he
    # notices, the seven moves, the shark. The moves come from SHOW OFF: the
    # emulator runs it at half tempo, so a step is ~34 frames from frame 204.
    "neighbourhood-watch": [
        ("clear", 716, 32, ["--showoff", "--noseed", "--bg", "6"],  {},  700, "HE HEARD SOMETHING. HE ALWAYS HEARS SOMETHING."),
        ("clear", 750, 30, ["--showoff", "--noseed", "--bg", "6"],  {},  700, "HE TRIPS OVER NOTHING. DON'T MENTION IT."),
        ("clear", 922, 34, ["--showoff", "--noseed", "--bg", "6"],  {},  900, "FLICK HIM. GO ON. HE WALKS BACK. SLOWLY."),
        ("log",    20, 24, [],                                       {}, 1500, "THE RING YOU PASS EVERY DAY IS CALLED VERN NOW"),
        ("dex",    20, 24, ["--pose", "6", "--bg", "4"],             {}, 1500, "THE ONE YOU CATCH MOST IS HIS NEMESIS"),
        ("clear",  60, 36, ["--outfit", "14", "--noseed", "--bg", "10"], {}, 1300, "THE SHARK GOT AN OUTLINE. AND NOSTRILS."),
    ],
    # v1.18.0 "Field Guide": the SQUACHY-DEX, the LOG showing each device once,
    # and the tap that has to be quick.
    "field-guide": [
        ("dex",      20, 30, ["--pose", "0", "--bg", "4"], {}, 1300, "SEVENTEEN CRYPTIDS. SQUACHY HAS A BINDER."),
        ("dex",      20, 30, ["--pose", "6", "--bg", "4"], {}, 1600, "EVERY CATCH GETS A CARD. LORE INCLUDED."),
        ("dex",      20, 30, ["--pose", "5", "--bg", "4"], {}, 1500, "NOT CAUGHT? YOU GET A HINT AND A SILHOUETTE"),
        ("log",      20, 30, [],                            {}, 1300, "THE LOG SHOWS EACH DEVICE ONCE. FINALLY."),
        ("settings", 10, 24, ["--scroll", "0"],             {}, 1300, "AND A SLOW THUMB IS NOT A TAP ANY MORE"),
    ],
    # v1.17.0 "Hold Still": touch calibration, done over. The touchcal screen
    # is the real flow played by a scripted finger (see main_sim.cpp), and
    # --frames there skips into it rather than warming an animation up.
    "hold-still": [
        ("touchcal",   0, 16, [],            {},  900, "SQUACHY WANTS TO KNOW WHERE YOUR FINGER IS"),
        ("touchcal",  16, 62, [],            {},  500, "FIVE TARGETS. NONE HIDING UNDER YOUR CASE"),
        ("touchcal", 104, 44, [],            {},  500, "HOLD ONE SECOND. NOT FOREVER. ONE."),
        ("touchcal", 150, 38, [],            {}, 1200, "THEN IT GRADES ITS OWN HOMEWORK"),
        ("clear",     60, 40, ["--bg", "4"], {}, 1400, "PORTRAIT TOUCH: FINALLY NOT HAUNTED"),
    ],
    # v1.14.0 "Stoop Kid": what changed, in the order it matters to a viewer.
    "stoop-kid": [
        ("clear",    60, 36, ["--bg", "4"],            {},  700, "TEXT DRAWS SIX TIMES FASTER"),
        ("clear",    60, 36, ["--bg", "0"],            {},  700, "15 TO 21 FPS ON THE MAIN SCREEN"),
        ("alert",    30, 16, ["--lastfree"],           {}, 1900, "AUTO SNOOZE: FIVE ALERTS, THEN IT HAS TO COME CLOSER"),
        ("alert",    30, 16, ["--first", "--night"],   {}, 1500, "TWO BANNERS THAT NEVER DREW, DRAWING"),
        ("settings", 10, 12, ["--scroll", "0"],        {}, 1500, "SETTINGS > BEHAVIOR > AUTO SNOOZE"),
        ("log",      20, list(range(0, 13)), [],       {}, 1300, "EVERY LIST STOPS AT THE BOTTOM NOW"),
    ],
    # v1.16.1 "Good Company": the pet was standing on the wrong Squachy, and
    # the 3.5" reached the flasher's picker without reaching its files.
    "good-company": [
        ("clear", 400, 44, ["--peer", "2", "--peername", "GUEST", "--pet", "1"], {},  900, "A VISITOR, AND THE PET THAT FOLLOWED THE WRONG ONE"),
        ("clear", 460, 44, ["--peer", "2", "--peername", "GUEST", "--pet", "1"], {},  900, "IT RIDES ITS OWN SQUACHY'S BOUNCE AGAIN"),
        ("boot",   20, 22, [],                 {}, 1500, "AND THE 3.5 INCH IS ON THE FLASHER FOR REAL"),
    ],
    # v1.16.0 "The Big Screen": the 3.5" ships, and it is the screen itself
    # that is the news -- so the clip is shot on it.
    "the-big-screen": [
        ("boot",     20, 24, [],                        {}, 1200, "THE 3.5 INCH SHIPS, AS A BETA"),
        ("clear",    60, 40, ["--bg", "5"],             {},  900, "IT STOPPED CRASHING: A WRITE 76KB PAST THE BUFFER"),
        ("settings", 10, 14, ["--scroll", "0"],         {}, 1500, "MENUS ARE BUFFERED NOW, AND SEND NOTHING AT ALL"),
        ("alert",    20, 20, [],                        {}, 1500, "AND EVERYTHING USES THE SCREEN IT HAS"),
    ],
    # v1.15.0 "Blast Processing": the frame rate, and the mascot keeping his
    # pace in spite of it. The row-skipping push and the numbers are the
    # board's; the captions carry them.
    "blast-processing": [
        ("clear",    60, 40, ["--bg", "10"],           {},  900, "THIRTY FRAMES A SECOND ON THE 2.8 INCH"),
        ("clear",    60, 40, ["--bg", "3"],            {},  900, "ROWS THAT DID NOT CHANGE ARE NOT SENT"),
        ("settings", 10, 10, ["--scroll", "0"],        {}, 1500, "A MENU SENDS NOTHING AT ALL"),
        ("clear",    60, 46, ["--bg", "4"],            {},  900, "SQUACHY KEEPS HIS OWN PACE: 120 / 70"),
    ],
}

# A clip can be shot on another panel. The emulator takes --size WxH and every
# screen composes itself for whatever it is given, so a release about the 3.5"
# can be SHOWN on the 3.5" instead of described on a 2.8". Zoom comes down to
# 1 there: 480x320 doubled is a 960-pixel GIF, which is more than the notes
# embed at anyway.
CLIP_SIZE = {
    "all-ears":            (240, 240, 2),
    "squachwatch-squared": (240, 240, 2),
    "the-big-screen": (480, 320, 1),
    "good-company":   (480, 320, 1),
}

def clip_geom(clip):
    w, h, z = CLIP_SIZE.get(clip, (W, H, ZOOM))
    return w, h, z

def out_dir(clip):
    return os.path.join(HERE, "out", "show-" + clip)

def gif_path(clip):
    return os.path.join(HERE, "..", "docs", clip + ".gif")

# ---- render ----

def run(cmd, env):
    e = dict(os.environ, SQUACH_EPOCH=EPOCH)
    e.update(env)
    if subprocess.call(cmd, cwd=HERE, env=e, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL) != 0:
        sys.exit("render failed: " + " ".join(cmd))

def render(clip):
    cw, ch, _ = clip_geom(clip)
    sim = os.path.join(HERE, "squachsim")
    if not os.path.exists(sim):
        sys.exit("build the emulator first: make -j8 squachsim")
    out = out_dir(clip)
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)
    man = []
    for i, (screen, warm, n, extra, env, hold, caption) in enumerate(CLIPS[clip]):
        if isinstance(n, list):
            # A scroll sweep: one frame per value.
            for k, sc in enumerate(n):
                raw = os.path.join(out, "scene%d_%d.raw" % (i, k))
                run([sim, screen, os.path.join(out, "x.png"), "--frames", str(warm),
                     "--sequence", "1", "--raw", raw, "--scroll", str(sc),
                     "--size", "%dx%d" % (cw, ch)] + extra, env)
                if os.path.getsize(raw) != cw * ch * 3:
                    sys.exit("scene %d/%d: bad frame size" % (i, k))
                last = (k == len(n) - 1)
                man.append({"raw": os.path.basename(raw), "index": 0,
                            "ms": (MS * 2) if not last else MS + hold, "caption": caption})
            continue
        raw = os.path.join(out, "scene%d.raw" % i)
        # A scene can be shot on another panel ("_SIZE": "240x135" in its
        # env, for the StickS3); encode() letterboxes it into the clip.
        env = dict(env)
        sw, sh = cw, ch
        if "_SIZE" in env:
            sw, sh = (int(v) for v in env.pop("_SIZE").split("x"))
        run([sim, screen, os.path.join(out, "x.png"), "--frames", str(warm),
             "--sequence", str(n), "--raw", raw,
             "--size", "%dx%d" % (sw, sh)] + extra, env)
        if os.path.getsize(raw) != sw * sh * 3 * n:
            sys.exit("scene %d: %d bytes, expected %d" % (i, os.path.getsize(raw), sw * sh * 3 * n))
        for k in range(n):
            man.append({"raw": os.path.basename(raw), "index": k, "w": sw, "h": sh,
                        "ms": MS if k < n - 1 else MS + hold, "caption": caption})
    json.dump(man, open(os.path.join(out, "manifest.json"), "w"))
    print("%d frames rendered into %s" % (len(man), out))

# ---- encode ----

def caption_band(text, width):
    """The band under the screen: the caption in Bangers, leaning forward,
    the way the v1.13.0 clip had it."""
    from PIL import Image, ImageDraw, ImageFont
    band = Image.new("RGB", (width, BAND), (0, 0, 0))
    if not text:
        return band
    size = 30
    f = ImageFont.truetype(FONT, size)
    probe = ImageDraw.Draw(band)
    while size > 14:
        f = ImageFont.truetype(FONT, size)
        a, b, c, d = probe.textbbox((0, 0), text, font=f)
        if c - a <= width - 40:
            break
        size -= 2
    tw, th = c - a, d - b
    # Render upright on a transparent layer, then shear it for the lean.
    layer = Image.new("RGBA", (tw + 24, BAND), (0, 0, 0, 0))
    ImageDraw.Draw(layer).text((12 - a, (BAND - th) // 2 - b), text, font=f, fill=CYAN + (255,))
    shear = 0.18
    layer = layer.transform(layer.size, Image.AFFINE, (1, shear, -shear * BAND / 2, 0, 1, 0),
                            resample=Image.BICUBIC)
    band.paste(layer, ((width - layer.width) // 2, 0), layer)
    return band

def encode(clip):
    from PIL import Image
    cw, ch, zoom = clip_geom(clip)
    out = out_dir(clip)
    mp = os.path.join(out, "manifest.json")
    if not os.path.exists(mp):
        sys.exit("no frames -- run --render-only under WSL first")
    man = json.load(open(mp))
    raws, ims = {}, []
    bands = {}
    for f in man:
        if f["raw"] not in raws:
            raws[f["raw"]] = open(os.path.join(out, f["raw"]), "rb").read()
        b = raws[f["raw"]]
        sw, sh = f.get("w", cw), f.get("h", ch)
        off = f["index"] * sw * sh * 3
        im = Image.frombytes("RGB", (sw, sh), b[off:off + sw * sh * 3])
        if zoom > 1:
            im = im.resize((sw * zoom, sh * zoom), Image.NEAREST)
        if (sw, sh) != (cw, ch):
            # A smaller panel, centred on black with a one-pixel cyan edge
            # so it reads as a different screen rather than a cropped one.
            box = Image.new("RGB", (cw * zoom, ch * zoom), (0, 0, 0))
            x0, y0 = (box.width - im.width) // 2, (box.height - im.height) // 2
            from PIL import ImageDraw
            ImageDraw.Draw(box).rectangle([x0 - 3, y0 - 3, x0 + im.width + 2, y0 + im.height + 2], outline=CYAN, width=2)
            box.paste(im, (x0, y0))
            im = box
        if f["caption"] not in bands:
            bands[f["caption"]] = caption_band(f["caption"], im.width)
        page = Image.new("RGB", (im.width, im.height + BAND), (0, 0, 0))
        page.paste(im, (0, 0))
        page.paste(bands[f["caption"]], (0, im.height))
        ims.append(page)
    # One palette for the whole clip, so nothing shimmers between scenes.
    sheet = Image.new("RGB", (ims[0].width, ims[0].height * len(ims)))
    for i, im in enumerate(ims):
        sheet.paste(im, (0, ims[0].height * i))
    ref = sheet.quantize(colors=256, dither=Image.NONE)
    frames = [im.quantize(palette=ref, dither=Image.NONE) for im in ims]
    gif = gif_path(clip)
    os.makedirs(os.path.dirname(gif), exist_ok=True)
    frames[0].save(gif, save_all=True, append_images=frames[1:],
                   duration=[f["ms"] for f in man], loop=0, optimize=True, disposal=1)
    total = sum(f["ms"] for f in man) / 1000.0
    print("%d frames, %.1fs, %dx%d -> %s (%d KB)"
          % (len(frames), total, frames[0].width, frames[0].height,
             os.path.normpath(gif), os.path.getsize(gif) // 1024))

if __name__ == "__main__":
    clip = "stoop-kid"
    if "--clip" in sys.argv:
        clip = sys.argv[sys.argv.index("--clip") + 1]
    if clip not in CLIPS:
        sys.exit("no clip called %s; one of: %s" % (clip, ", ".join(CLIPS)))
    if "--render-only" in sys.argv:   render(clip)
    elif "--encode-only" in sys.argv: encode(clip)
    else:                             render(clip); encode(clip)
