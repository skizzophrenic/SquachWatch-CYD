# Ideas

Things worth building that nobody has started. Each one carries what it would
cost, because on this board that is the deciding factor: there is about 310 KB
of firmware space and only about 52 KB of working memory free (both after
v1.12.0 gave back 108 KB and 6 KB), and the screen already takes 50 ms a frame.

Nothing here is a commitment. Cross one off or add one whenever.

## Games

The board already has most of the machinery a small game needs, which is why
these are cheap:

- **Rewards.** Outfits can be handed out for anything, not only detection
  counts: the werewolf, the gold toaster, the starfield eye and the lodge are
  all one-line unlocks, with the celebration screen already written.
- **A screen.** New full screens are a switch case and a draw function.
- **Touch.** Taps, holds and swipes are all handled.
- **Saving.** Settings storage keeps small things across restarts, and the
  black box (branch, untested) can keep bigger ones.
- **Detections and the clock.** Both are live, so a game can be about what is
  really around you, and about the real day.

### 1. DETECTION BINGO — the best of these

A card of detection types. Every type you actually detect marks its square.
Lines and a full card hand out rewards.

**Why it fits:** it makes the thing the board already does into a game, it
needs no new art, and it rewards going places. It also gives the rarer types
(RAVEN, ALPR, DEAUTH) a point, which today are just numbers that never move.

**Shape:**
- A 4x4 card, sixteen squares from the eighteen types.
- The card is drawn so it is winnable: mostly types this board has seen before
  (their lifetime counts are already stored), plus two or three it has not,
  as the stretch.
- A square marks on a first sighting, so sitting next to one device all day
  marks one square.
- A line is worth something small (a Squachy line, a toast, a stat). A full
  card unlocks an outfit — BINGO CARD, a loud holiday shirt, or a visor.
- A fresh card each week, with a streak count for weeks completed. Or the
  player asks for a new card, which resets the streak.
- Tapping a square says what that type is, which doubles as a way to learn
  the types.

**Cost:** about 20 bytes saved (the card, the marks, the day it was issued),
one screen of maybe 3-6 KB of firmware, no extra working memory, no per-frame
cost when the screen is closed. The marking itself is a few lines where a
detection is first logged.

**Open questions:** whether a full card is realistic outside a city, and
whether to count detections from squad members' boards too (their hellos
already carry what they have seen).

### 2. HUNT, scored

The HUNT screen already shows a live signal-strength gauge for one device.
Turn a hunt into a round: the clock starts when you pick a target, and stops
when you get within a set strength. It keeps your best time per type.

**Cost:** tiny, since the screen exists. A timer, one best-time table, and an
end-of-round card. Maybe 2 KB.

**Payoff:** it is the closest thing here to a real-world game, and it makes a
good clip — walking around while the bar climbs.

### 3. WHACK-A-TRACKER

Trackers pop up around the screen and you tap them before they leave. Uses
the detection icons that already exist. A round is 30 seconds.

**Cost:** 3-4 KB, a handful of bytes for the high score. No new art.

**Payoff:** pure filler, but it is the sort of thing people show other people.
Squachy can heckle you while you play.

### 4. SQUACHY SAYS

Four corners light in a sequence and you repeat it. Gets longer each round.

**Cost:** about 2 KB. It needs nothing but the screen and taps.

**Payoff:** universally understood, works on the desk while you sit there,
and an outfit at round 10 is a real reward.

### 5. A CLASSIC — snake or pong

Pong against Squachy (he leans and misses on purpose sometimes), or snake
that eats detections.

**Cost:** 3-5 KB each.

**Payoff:** low, honestly. They are fine, but they say nothing about what this
board is for, and everything above says something.

### 6. MORE CATCH EGGS

The pattern is already proven four times: something rare crosses a background
and catching it unlocks an outfit — the gold toaster, the starfield eye, the
lodge, the werewolf. Each new one is small and self-contained.

Candidates: a fish in the aquarium, a shooting star, a face in the terminal
log, something in the fire.

**Cost:** 1-2 KB each, no new screens, no saved state beyond the unlock bit.

### 7. TWO-BOARD GAMES OVER SQUACHMESH

Tic-tac-toe or battleship between two boards in range.

**Cost:** the honest one. The message format has no spare bytes, so this needs
its own frame type, and every change to the radio protocol needs two boards to
test. Days, not hours.

**Payoff:** high for two people who both own one, low for everybody else.
Worth keeping in mind for when squad messaging is finished, not before.

## Companions

### THE YETI, as a second pet

The ski hill's yeti turns up on the main screen, at his own size, shouts
something short, and wanders off again. Rarely, he drags a skier on from the
side and eats him.

**Why it is nearly free:** almost every piece exists.

- His sprite is `snowYeti()` in theme.cpp, with five poses: run, eat, winded,
  knocked down, recoil. No new art, and at his own size no scaling either.
- His voice is already written and already right -- GRAAAH, COME HERE, MINE,
  RRRAAA, HUNGRY, STOP RUNNING. All caps, one to three words, no articles,
  himself in the third person.
- The skier is `snowSkier()`, the same rider he chases on the hill.
- The pet slot exists: `Pet::tick()` draws VAPOR SHAGGY on the main screen,
  with a Settings row and an unlock bit behind it.
- The eat already has dialogue: on the hill the rider says hi. / sir? / I'm
  mostly bone / we can talk while the yeti says hm. / let me see / checks
  out / no lift pass.

**What it needs**

- PET becomes a picker -- OFF / SHAGGY / YETI -- instead of a switch.
- A small sequence: walk on, stand, say something, leave.
- The quip table below.
- The rare eat, and an unlock.

**The quips.** Fourteen characters is the limit before the small font stops
reading at a glance -- the hill's own comment says so.

- Arriving: YETI HERE. / YETI! YETI! / SNOW? NO SNOW.
- Idle: YETI BORED. / WHERE SNOW? / ROOM QUIET. / YETI HUNGRY.
- At Squachy: SMALL FRIEND. / YOU TALK MUCH. / STOP WAVE.
- A camera: EYE! BAD EYE! / SMASH IT? / EYE LOOK. RAA.
- A tracker: TINY BEEP. NO. / BEEP AGAIN?
- A plate reader: CAR WATCHER! / PLATE THIEF!
- A quiet room: NOBODY. GOOD. / YETI NAP?
- At night: DARK GOOD.
- Leaving: YETI GO. / BYE. HUNGRY. / YETI BACK SOON

Squachy answering dryly is where the laugh is: WHERE SNOW? / "Florida."
SMASH IT? / "no." The banter engine already runs two-character exchanges.

**The rare one.** He drags a skier on by one leg -- I'M HANGRY!! -- lifts him
(sir?), eats him (CHOMP, and Squachy: oh no.), says MUCH BETTER, and wanders
off leaving one ski on the floor. The ski stays until the screen changes.

Keep it rare: one appearance in ten or twenty. The hill already works this
way, and the comment there is worth repeating -- most chases end with the
skier getting away, and "the eat is the rare one, which is the only thing
that makes it land".

**How he is unlocked.** Rescue three skiers from him on the ski hill.

There is already a window to do it in, and it is a generous one. When a chase
ends in a catch the yeti does not gulp: he hoists the rider off his skis
(0.3 s), raises him to eye level (0.76 s), holds him there and looks at him
(to 1.7 s), turns him over, and only chomps at 2.1 s. The pause is deliberate
-- theme.cpp calls it the whole joke -- so there are about **1.8 seconds**
between the grab and the jaw. Tap the yeti in that window and he drops the
rider, who skis off (a "thanks!" would sit fine in the existing chatter).

How often a chance comes: a chase starts every 16-34 seconds while the ski
hill is showing, and one chase in six ends in a catch. So about one chance
every two and a half minutes, and three rescues is roughly eight minutes of
watching the hill -- longer if you fumble one, which is fine.

Taps already reach a background through `Theme::backgroundTap()`, the same
path the lodge knock and the toaster catch use. That background can hold a
second egg (the lodge knock is the first) -- `eggsHere()` already returns up
to two.

Worth persisting the rescue count (one byte) rather than keeping it in RAM,
so a restart mid-hunt does not start you over. Settings::Hunt has room for it
beside the eye streak.

**And the cheat unlocks him too.** Holding DESK on the main screen for four
seconds already calls `unlockAllOutfits()`, which hands over the pet as well
-- its comment says a costume set that stops short of the one companion would
be a strange place to draw the line. The same applies to a second companion,
so whatever holds the yeti's unlock bit gets set there in the same breath.

**Cost:** no new art at all. About 450 bytes for the lines, 1-2 KB for the
plain version, another 1-2 KB for the rare eat, a few dozen bytes of working
memory, and nothing at all while he is switched off.

**Open questions:** one pet at a time or both at once; main screen only, or
the desk as well; and whether eating somebody wants its own switch for people
who would rather it did not happen.

## Not games

- **Detection streaks.** Days in a row with at least one detection, and the
  longest run ever. Almost free, and it gives the daily lines something to
  talk about.
- **A day chart.** Detections per hour for the last month, drawn on the desk.
  Needs the black box, then it is tiny.
- **Egg progress that survives a restart.** The unlocks themselves are saved,
  but the progress towards them is not: the lodge knocks (5 needed) and the
  starfield eye streak (2 in a row) are plain variables in memory, so a
  restart -- or a crash, or a flat battery -- starts the hunt over. Three
  counters fit in one 32-bit settings entry (knocks, eye streak, and the
  yeti rescues above), written a second after the last change the way bingo
  writes its card, and cleared when the unlock fires. One entry of the 376
  free, a handful of writes in a device's life.
- **Devices seen before.** "This tag has been near you on four different
  days." The genuinely useful one, and the one to be careful with: many
  trackers change their address, so it has to be tested before it is claimed.

## Faster frames on the ESP32-S3 boards

The overlapped frame push (`frame_push.h`, v1.15.0) is off on both S3 boards:
it drives the ESP32's SPI registers directly, and the S3 lays them out
differently -- the watch's first frame through it came out white. They use the
library's plain `pushSprite()` instead. On the Freenove S3 2.8" that is 40 ms a
frame on the wire at 40 MHz against about 31 ms of actual bytes, the rest spent
converting a line while the bus waits: 14-17 fps on the main screen. Porting
the push to the S3's registers is worth roughly 5-8 fps there, and the watch
gets it too. The clock is not the way: 80 MHz garbles that panel.

## Rules of thumb for anything here

- **Working memory, not firmware space, is the limit.** A game that needs a
  second screen buffer is not cheap.
- **A game must not stop detection.** The radios keep running, or the board
  stops being what it is.
- **It has to survive a restart** if it takes more than a minute to play.
- **The reward should be an outfit or a line from Squachy.** Those are already
  what the board gives out.
